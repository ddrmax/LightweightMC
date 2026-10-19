#pragma once
// =============================================================================
// Ban / whitelist bookkeeping (Phase 10 subphase 10.1/10.3)
// -----------------------------------------------------------------------------
// Keeps the in-memory ban + whitelist state that NetworkManager, LoginHandler
// and the console/chat admin commands consult. Files on disk follow vanilla
// 1.8 server filenames so external tools (or another LWMC instance exporting
// its state) can import them unchanged:
//   banned-players.json -> [{"id":"<username>","reason":"..."}, ...]
//   banned-ips.json     -> [{"ip":"<dotted quad>","reason":"..."}, ...]
//   whitelist.json      -> [{"id":"<username>"}, ...]
// JSON is hand-rolled (std only) because the repo has no third-party json
// dependency; values are emitted with quotes/backslashes escaped.
// =============================================================================

#include <string>
#include <unordered_set>

namespace LightweightMC::Network
{

    // Ban / whitelist state -----------------------------------------------
    // Usernames and IPs are stored lower-cased for matching (login usernames
    // arrive already-lower from LoginStart parsing; IP dotted quads never
    // contain letters so case does not matter but the helper still lowercases
    // defensively).
    bool isPlayerBanned(const std::string &username);
    bool isIpBanned(const std::string &ip);
    void banPlayer(const std::string &username, const std::string &reason = "Banned by an administrator.");
    void unbanPlayer(const std::string &username);
    void banIp(const std::string &ip, const std::string &reason = "Banned by an administrator.");
    void unbanIp(const std::string &ip);

    // Whitelist gate. `enabled` mirrors vanilla whitelist-mode semantics:
    // when false every login is accepted (the default); when true only a
    // name listed in whitelist.json may connect, except players who are
    // already banned (bans always win).
    bool setWhitelistEnabled(bool enabled);
    bool getWhitelistEnabled();
    void whitelistAdd(const std::string &username);
    void whitelistRemove(const std::string &username);
    bool isWhitelisted(const std::string &username);

    // Sidecar load/save helpers (vanilla-compatible filenames). Paths are
    // resolved relative to the process cwd like SQLite world dbs.
    void loadBanSidecars();   // loads banned-players.json + banned-ips.json
    void saveBanSidecars();   // writes both back from memory
    void loadWhitelistSidecar();
    void saveWhitelistSidecar();

} // namespace LightweightMC::Network
