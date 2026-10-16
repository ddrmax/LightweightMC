#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Vanilla-style auth module for MC 1.8+. Two modes:
//
//   auth-mode = offline        (default)  -- no HTTP verify, LoginStart
//                                            accepted, offline UUID minted
//                                            client-side. This is the "real
//                                            server" offline toggle the user
//                                            asked for: the login packet 0x01
//                                            Login (Access Token) flow stays
//                                            wired up so the wire protocol
//                                            matches vanilla, only the HTTP
//                                            verify step is skipped.
//
//   auth-mode = token          -- primary verify URL + backup trusted-relay
//                                            list. If primary is down / returns
//                                            HTTP 500 / malformed JSON, retry
//                                            against the backup list. Token is
//                                            cached for auth-cache-ttl seconds.
//                                            Optionally, if the relay returns
//                                            a session_key field, an AES-128
//                                            CFB-128 cipher is enabled for the
//                                            session (MODERN/LATEST eras only;
//                                            LEGACY 1.8 does not encrypt).
//
// Config keys live in the [auth] INI section:
//   auth-mode                  = offline | token
//   auth-verify-url            = https://...
//   auth-verify-backup-urls    = https://...,https://...,https://...
//   auth-cache-ttl             = 86400   (seconds; 0 disables caching)
//   auth-strict                = false   (reject on total verify-chain failure)
//   aes-session-key-field      = session_key
//   master-server-ping         = false   (outbound ping on boot; opt-in)
//   master-server-urls         = <csv of master-server list URLs>
#include "network/AesCfb.hpp"
#include "core/ConfigManager.hpp"
#include "core/Logger.hpp"

namespace LightweightMC::Network
{
    namespace Auth
    {
        enum class Mode
        {
            OFFLINE,
            TOKEN,
        };

        struct CachedToken
        {
            std::string username;
            std::string uuid;
            std::string sessionKey;
            std::chrono::steady_clock::time_point expiresAt{};
        };

        // Boot-time init: reads [auth] config keys, initializes the module.
        void init();

        // Current auth-mode value (cache of the parsed enum; hot path).
        Mode mode();

        // Verify a login access token synchronously against primary + backup
        // verify URLs (skipped when mode()==OFFLINE). Returns true on success
        // and fills (username, uuid, sessionKey). sessionKey is optional and
        // only populated when the relay's JSON includes the field named by
        // [auth] aes-session-key-field.
        bool verifyToken(const std::string &token,
                         std::string &username,
                         std::string &uuid,
                         std::string &sessionKey);

        // Whether verify-chain failure should REJECT the login (true) or
        // fall back to offline-mode (false). Sourced from [auth] auth-strict.
        bool strict();

        // Fire the outbound master-server ping on the boot path (opt-in via
        // [auth] master-server-ping = true). Sends a vanilla GET / HTTP/1.1
        // request on the game port to each master-server-urls entry via
        // curl --max-time 4; used for community listings like Bluetymaps /
        // addserver.org.
        void pingMasterServers(uint16_t port);

        // Extract a JSON string field's value from a top-level JSON object.
        // Only sufficient for the flat shape community relays emit
        // (no nested objects with the same key). Returns empty on miss.
        std::string extractJsonField(const std::string &json, const std::string &field);

        // Split a comma-separated string of URLs, trimming whitespace.
        std::vector<std::string> splitUrlList(const std::string &csv);

        // Access to the AES-128 CFB cipher instance armed by the last
        // verifyToken that returned a session_key; null when the field was
        // absent or auth-mode = offline. Callers do not own the pointer.
        AesCfb128 *lastCipher();

    } // namespace Auth
} // namespace LightweightMC::Network
