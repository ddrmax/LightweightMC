// Vanilla-style token auth for MC 1.8+ with an offline fast path and a
// primary + trusted-backup verify-server chain. The offline path stays
// byte-identical to what LoginHandler.cpp used to do (LoginStart 0x00
// -> LoginSuccess 0x02 with a locally-minted UUID), so the existing
// clients keep working without any configuration change.
//
// The token path mirrors what vanilla MC 1.8 clients do when their
// "Auth Server" config points at a community relay: Login (Access Token)
// packet 0x01 arrives first, we hand the token to `auth-verify-url` via
// curl (same synchronous pattern AntiScan.cpp already uses for the
// blocklist), and on success we reply Login (Successful) 0x02 with the
// username + UUID the relay returned. On failure we retry every URL in
// [auth] auth-verify-backup-urls in order; only if the whole chain fails
// does [auth] auth-strict decide whether to send CannotConnect 0x03 or
// to fall through to the offline fast path (the "real server, offline
// toggle" behaviour the user asked for).
//
// A verify relay that returns a session_key field additionally arms an
// AES-128 CFB-128 cipher for the session; the cipher is enabled ONLY for
// the MODERN/LATEST protocol eras since LEGACY (1.8) never encrypts the
// wire post-login.

#include "network/Authentication.hpp"
#include "network/AesCfb.hpp"
#include "core/ConfigManager.hpp"
#include "core/Logger.hpp"
#include "core/utils.hpp"

#include <sys/stat.h>
#include <sys/types.h>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <mutex>
#include <cctype>

namespace LightweightMC::Network
{
    namespace Auth
    {
        namespace
        {
            Mode g_mode{Mode::OFFLINE};
            std::string g_verifyUrl{};
            std::vector<std::string> g_backupUrls{};
            unsigned g_cacheTtl{86400};
            bool g_strict{false};
            std::string g_sessionKeyField{"session_key"};
            bool g_masterPing{false};
            std::vector<std::string> g_masterUrls{};

            std::unordered_map<std::string, CachedToken> g_cache{};
            std::mutex g_cacheMutex{};

            static AesCfb128 g_cipher{};
            bool g_cipherArmed{false};

            void readConfig()
            {
                const auto &cfg = ConfigManager::getInstance();
                const std::string mode = cfg.getString("auth", "auth-mode", "offline");
                if (mode == "token") g_mode = Mode::TOKEN;
                else g_mode = Mode::OFFLINE;

                g_verifyUrl = cfg.getString("auth", "auth-verify-url", "");
                const std::string backups = cfg.getString("auth", "auth-verify-backup-urls", "");
                g_backupUrls.clear();
                std::stringstream ss(backups);
                std::string item;
                while (std::getline(ss, item, ','))
                {
                    while (!item.empty() && item.front() == ' ') item.erase(item.begin());
                    while (!item.empty() && item.back() == ' ') item.pop_back();
                    if (!item.empty()) g_backupUrls.push_back(item);
                }

                g_cacheTtl = cfg.template get<unsigned>("auth", "auth-cache-ttl", static_cast<unsigned>(86400));
                g_strict = cfg.getBool("auth", "auth-strict", false);
                g_sessionKeyField = cfg.getString("auth", "aes-session-key-field", "session_key");
                g_masterPing = cfg.getBool("auth", "master-server-ping", false);
                const std::string ms = cfg.getString("auth", "master-server-urls", "");
                g_masterUrls.clear();
                std::stringstream ss2(ms);
                while (std::getline(ss2, item, ','))
                {
                    while (!item.empty() && item.front() == ' ') item.erase(item.begin());
                    while (!item.empty() && item.back() == ' ') item.pop_back();
                    if (!item.empty()) g_masterUrls.push_back(item);
                }
            }

            bool tryUrl(const std::string &url,
                        const std::string &token,
                        std::string &username,
                        std::string &uuid,
                        std::string &sessionKey,
                        int &httpStatus)
            {
                if (url.empty())
                    return false;
                char tmpFile[128];
                std::snprintf(tmpFile, sizeof(tmpFile),
                              "/tmp/lwmc_auth_verify_%d.json",
                              static_cast<int>(::getpid()) ^ static_cast<int>(token.size()));
                // --max-time 4: total-request timeout (curl aborts on slow
                // relays); -f: HTTP 4xx/5xx make curl exit non-zero so a 500
                // response correctly signals "verify chain broken" rather
                // than parsing an HTML error page as JSON.
                std::string command = "curl -s -f --max-time 4 --connect-timeout 3 '" + url + token + "' -o " + tmpFile;
                const int rc = std::system(command.c_str());
                if (rc != 0)
                {
                    httpStatus = (rc == -1) ? -1 : 500;
                    LightweightMC::Core::Logger::warn("Auth verify URL returned non-200: " + url);
                    return false;
                }
                std::ifstream f(tmpFile);
                if (!f.is_open())
                    return false;
                std::stringstream buf;
                buf << f.rdbuf();
                const std::string body = buf.str();
                ::remove(tmpFile);
                username = extractJsonField(body, "username");
                uuid = extractJsonField(body, "id");
                sessionKey = extractJsonField(body, g_sessionKeyField);
                if (username.empty() || uuid.empty())
                {
                    LightweightMC::Core::Logger::warn("Auth verify JSON malformed (missing username/id): " + url);
                    return false;
                }
                httpStatus = 200;
                return true;
            }
        } // anonymous


        void init()
        {
            readConfig();
            LightweightMC::Core::Logger::info(
                g_mode == Mode::OFFLINE
                    ? std::string("[Auth] Mode: OFFLINE (login accepted, no HTTP verify)")
                    : std::string("[Auth] Mode: TOKEN (primary + backup verify chain)"));
            if (g_mode == Mode::TOKEN && g_verifyUrl.empty())
            {
                LightweightMC::Core::Logger::warn(
                    "[Auth] WARNING: auth-mode=token but auth-verify-url is empty; "
                    "falls back to offline behavior on every login.");
            }
        }

        Mode mode() { return g_mode; }
        bool strict() { return g_strict; }

        std::string extractJsonField(const std::string &json, const std::string &field)
        {
            // Minimal "flat JSON" extractor for community relay responses.
            // Community relays emit a shallow shape like
            //   {"username":"Bob","id":"...","session_key":"..."}
            // so a linear regex-free scan of `"field"\s*:\s*"value"`
            // is enough and does not require pulling in a JSON parser.
            const std::string pattern = "\"" + field + "\"";
            const auto pos = json.find(pattern);
            if (pos == std::string::npos) return {};
            auto colon = json.find(':', pos + pattern.size());
            if (colon == std::string::npos) return {};
            auto open = json.find('"', colon + 1);
            if (open == std::string::npos) return {};
            auto close = json.find('"', open + 1);
            if (close == std::string::npos) return {};
            return json.substr(open + 1, close - open - 1);
        }

        std::vector<std::string> splitUrlList(const std::string &csv)
        {
            std::vector<std::string> out;
            std::stringstream ss(csv);
            std::string item;
            while (std::getline(ss, item, ','))
            {
                while (!item.empty() && item.front() == ' ') item.erase(item.begin());
                while (!item.empty() && item.back() == ' ') item.pop_back();
                if (!item.empty()) out.push_back(item);
            }
            return out;
        }

        AesCfb128 *lastCipher()
        {
            return g_cipherArmed ? &g_cipher : nullptr;
        }


        bool verifyToken(const std::string &token,
                         std::string &username,
                         std::string &uuid,
                         std::string &sessionKey)
        {
            username.clear();
            uuid.clear();
            sessionKey.clear();
            if (g_mode == Mode::OFFLINE)
                return false;
            if (token.empty())
                return false;

            const auto now = std::chrono::steady_clock::now();
            {
                std::lock_guard<std::mutex> lock(g_cacheMutex);
                auto it = g_cache.find(token);
                if (it != g_cache.end())
                {
                    if (g_cacheTtl == 0 || it->second.expiresAt > now)
                    {
                        username = it->second.username;
                        uuid = it->second.uuid;
                        sessionKey = it->second.sessionKey;
                        return true;
                    }
                    g_cache.erase(it);
                }
            }

            const auto armCipherFromHex = [](const std::string &sk)
            {
                auto hex = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return 0;
                };
                std::vector<std::uint8_t> key(16, 0);
                for (std::size_t i = 0; i < 16 && (2 * i + 1) < sk.size(); ++i)
                    key[i] = static_cast<std::uint8_t>((hex(sk[2 * i]) << 4) | hex(sk[2 * i + 1]));
                g_cipher.reset();
                g_cipher.setKey(key.data());
                g_cipher.setIv(key.data());
                g_cipherArmed = true;
            };

            int status = 0;
            if (tryUrl(g_verifyUrl, token, username, uuid, sessionKey, status))
            {
                {
                    std::lock_guard<std::mutex> lock(g_cacheMutex);
                    CachedToken entry{username, uuid, sessionKey, now + std::chrono::seconds(g_cacheTtl)};
                    g_cache[token] = entry;
                }
                if (!sessionKey.empty())
                {
                    armCipherFromHex(sessionKey);
                    LightweightMC::Core::Logger::info(
                        "[Auth] verify relay returned session_key; AES-128 CFB cipher is now available for this session.");
                }
                return true;
            }

            // Backup relays (trusted community verify servers).
            for (const auto &backup : g_backupUrls)
            {
                std::string u2, u3, s3;
                int s2 = 0;
                if (tryUrl(backup, token, u2, u3, s3, s2))
                {
                    LightweightMC::Core::Logger::info(
                        "[Auth] verify succeeded via backup relay " + backup +
                        " (primary returned HTTP " + std::to_string(status) + ")");
                    username = std::move(u2);
                    uuid = std::move(u3);
                    sessionKey = std::move(s3);
                    {
                        std::lock_guard<std::mutex> lock(g_cacheMutex);
                        CachedToken entry{username, uuid, sessionKey, now + std::chrono::seconds(g_cacheTtl)};
                        g_cache[token] = entry;
                    }
                    if (!sessionKey.empty())
                        armCipherFromHex(sessionKey);
                    return true;
                }
            }

            LightweightMC::Core::Logger::warn(
                "[Auth] verify chain exhausted (primary + backups) for token; login will be rejected.");
            return false;
        }

        void pingMasterServers(uint16_t port)
        {
            if (!g_masterPing || g_masterUrls.empty())
                return;
            const auto &cfg = ConfigManager::getInstance();
            const std::string motd = cfg.getString("server", "motd", "LightweightMC");
            const std::string name = cfg.getString("server", "server-name", motd);
            for (const auto &ms : g_masterUrls)
            {
                const std::string cmd = "curl -s --max-time 4 '" + ms +
                                        "?ip=127.0.0.1&port=" + std::to_string(port) +
                                        "&name=" + name + "&motd=" + motd + "' > /dev/null";
                const int rc = std::system(cmd.c_str());
                (void)rc;
                LightweightMC::Core::Logger::info("[Auth] Pinged master-server listing: " + ms);
            }
        }
    } // namespace Auth
} // namespace LightweightMC::Network

