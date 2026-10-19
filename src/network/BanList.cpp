// =============================================================================
// Ban / whitelist bookkeeping — Phase 10 subphase 10.1 + 10.3 (vanilla-compat
// sidecars; see include/network/BanList.hpp for the file-format contract).
// =============================================================================

#include "network/BanList.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace LightweightMC::Network
{
    namespace
    {
        std::unordered_set<std::string> bannedPlayers{};
        std::unordered_map<std::string, std::string> banReasonsPlayer{};
        std::unordered_set<std::string> bannedIps{};
        std::unordered_map<std::string, std::string> banReasonsIp{};
        std::unordered_set<std::string> whitelistUsers{};
        bool whitelistEnabled{false};

        std::string lowerCase(const std::string &in)
        {
            std::string out(in);
            for (char &c : out)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }

        std::string jsonEscape(const std::string &in)
        {
            std::string out;
            out.reserve(in.size() + 8);
            for (char c : in)
            {
                if (c == '"' || c == '\\')
                {
                    out.push_back('\\');
                    out.push_back(c);
                }
                else if (c == '\n')
                    out += R"(\n)";
                else if (static_cast<unsigned char>(c) < 0x20U)
                    out.push_back(' ');
                else
                    out.push_back(c);
            }
            return out;
        }

        // scanStringObjects — tolerates extra keys newer vanilla writers add.
        void scanStringObjects(const std::string &text,
                               const std::vector<std::string> &wantedKeys,
                               std::vector<std::unordered_map<std::string, std::string>> &outRecords)
        {
            size_t i = 0;
            const size_t n = text.size();
            auto skipWs = [&](size_t &idx) {
                while (idx < n && std::isspace(static_cast<unsigned char>(text[idx])))
                    ++idx;
            };

            skipWs(i);
            if (i >= n || text[i] != '[')
                return;
            ++i; // [

            while (true)
            {
                skipWs(i);
                if (i >= n)
                    return;
                if (text[i] == ']')
                    return;
                if (text[i] == ',')
                {
                    ++i;
                    continue;
                }
                if (text[i] != '{')
                    return; // malformed -> stop gracefully
                ++i;       // {

                std::unordered_map<std::string, std::string> record;
                while (true)
                {
                    skipWs(i);
                    if (i >= n)
                        return;
                    if (text[i] == '}')
                    {
                        ++i;
                        break;
                    }
                    if (text[i] != '"')
                        return; // not a key string -> bail

                    // read key string literal (escapes preserved literally like values)
                    std::string rawKey;
                    bool inEsc{false};
                    for (++i; i < n && (inEsc || text[i] != '"'); ++i)
                    {
                        if (text[i] == '\\')
                            inEsc = !inEsc;
                        if (!inEsc)
                            rawKey.push_back(text[i]);
                    }
                    if (i >= n || text[i] != '"')
                        return; // missing terminator -> bail
                    ++i;       // closing quote of key

                    skipWs(i);
                    if (i >= n || text[i] != ':')
                        return; // unexpected token shape
                    ++i;
                    skipWs(i);
                    if (i >= n)
                        return;

                    // read the value string literal (non-string values are skipped).
                    std::string val;
                    if (text[i] == '"')
                    {
                        inEsc = false;
                        for (++i; i < n && (inEsc || text[i] != '"'); ++i)
                        {
                            if (text[i] == '\\')
                            {
                                if (!inEsc)
                                {
                                    inEsc = true;
                                    continue;
                                }
                                inEsc = false; // keep escaped char literally
                            }
                            if (!inEsc)
                                val.push_back(text[i]);
                        }
                        if (i >= n || text[i] != '"')
                            return;
                        ++i;
                    }
                    else
                    {
                        while (i < n && text[i] != ',' && text[i] != '}')
                            ++i;
                    }

                    for (const std::string &key : wantedKeys)
                    {
                        if (rawKey == key)
                        {
                            record[rawKey] = val;
                            break;
                        }
                    }

                    skipWs(i);
                    if (i < n && text[i] == ',')
                    {
                        ++i;
                        continue;
                    }
                } // object loop
                outRecords.push_back(std::move(record));

                skipWs(i);
                if (i < n && text[i] == ',')
                {
                    ++i;
                }
            } // outer array loop
        }
    } // anonymous namespace

    // ------------------------------------------------------------------- API

    bool isPlayerBanned(const std::string &username)
    {
        return bannedPlayers.find(lowerCase(username)) != bannedPlayers.end();
    }

    bool isIpBanned(const std::string &ip)
    {
        return bannedIps.find(lowerCase(ip)) != bannedIps.end();
    }

    void banPlayer(const std::string &username, const std::string &reason)
    {
        const std::string key = lowerCase(username);
        bannedPlayers.insert(key);
        banReasonsPlayer[key] = reason;
        saveBanSidecars();
    }

    void unbanPlayer(const std::string &username)
    {
        const std::string key = lowerCase(username);
        bannedPlayers.erase(key);
        banReasonsPlayer.erase(key);
        saveBanSidecars();
    }

    void banIp(const std::string &ip, const std::string &reason)
    {
        const std::string key = lowerCase(ip);
        bannedIps.insert(key);
        banReasonsIp[key] = reason;
        saveBanSidecars();
    }

    void unbanIp(const std::string &ip)
    {
        const std::string key = lowerCase(ip);
        bannedIps.erase(key);
        banReasonsIp.erase(key);
        saveBanSidecars();
    }

    bool setWhitelistEnabled(bool enabled)
    {
        whitelistEnabled = enabled;
        return whitelistEnabled;
    }

    bool getWhitelistEnabled()
    {
        return whitelistEnabled;
    }

    void whitelistAdd(const std::string &username)
    {
        whitelistUsers.insert(lowerCase(username));
        saveWhitelistSidecar();
    }

    void whitelistRemove(const std::string &username)
    {
        whitelistUsers.erase(lowerCase(username));
        saveWhitelistSidecar();
    }

    bool isWhitelisted(const std::string &username)
    {
        return whitelistUsers.find(lowerCase(username)) != whitelistUsers.end();
    }

    // ------------------------------------------------- sidecars on disk ------

    void loadBanSidecars()
    {
        for (const char *path : {"banned-players.json", "banned-ips.json"})
        {
            std::ifstream in(path);
            if (!in)
                continue; // absent file = empty list (vanilla behaviour)
            const std::string body((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());

            std::vector<std::unordered_map<std::string, std::string>> recs;
            scanStringObjects(body, {"id", "ip"}, recs);
            for (const auto &rec : recs)
            {
                if (std::string_view(path) == "banned-players.json")
                {
                    const auto it = rec.find("id");
                    if (it != rec.end())
                        banPlayer(it->second, "");
                    continue;
                }
                const auto it = rec.find("ip");
                if (it != rec.end())
                    banIp(it->second, "");
            }
        }
    }

    void saveBanSidecars()
    {
        for (const char *path : {"banned-players.json", "banned-ips.json"})
        {
            std::ofstream out(path);
            if (!out)
                continue;
            out << "[";
            const bool isPlayers(std::string_view(path) == "banned-players.json");
            const auto &set = isPlayers ? bannedPlayers : bannedIps;
            const auto &reasons = isPlayers ? banReasonsPlayer : banReasonsIp;
            bool first{true};
            for (const std::string &key : set)
            {
                if (!first)
                    out << ",";
                first = false;
                const auto reason = reasons.find(key);
                out << "{\"" << (isPlayers ? "id" : "ip") << "\":\"" << jsonEscape(key)
                    << "\",\"reason\":\"" << jsonEscape(reason == reasons.end() ? std::string{} : reason->second) << "\"}";
            }
            out << "]";
        }
    }

    void loadWhitelistSidecar()
    {
        std::ifstream in("whitelist.json");
        if (!in)
            return;
        const std::string body((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        std::vector<std::unordered_map<std::string, std::string>> recs;
        scanStringObjects(body, {"id"}, recs);
        for (const auto &rec : recs)
        {
            const auto it = rec.find("id");
            if (it != rec.end())
                whitelistUsers.insert(lowerCase(it->second));
        }
    }

    void saveWhitelistSidecar()
    {
        std::ofstream out("whitelist.json");
        if (!out)
            return;
        out << "[";
        bool first{true};
        for (const std::string &key : whitelistUsers)
        {
            if (!first)
                out << ",";
            first = false;
            out << "{\"id\":\"" << jsonEscape(key) << "\"}";
        }
        out << "]";
    }

} // namespace LightweightMC::Network
