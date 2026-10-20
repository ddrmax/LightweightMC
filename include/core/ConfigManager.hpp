#pragma once

#include "IniParser.hpp"
#include "core/Logger.hpp"

#include <string>
#include <shared_mutex>
#include <mutex>

class ConfigManager
{
public:
    static ConfigManager &getInstance()
    {
        static ConfigManager instance;
        return instance;
    }

    ConfigManager(const ConfigManager &) = delete;
    ConfigManager &operator=(const ConfigManager &) = delete;

    bool load(const std::string &filePath = "server.properties")
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_filePath = filePath;

        if (!m_parser.load(filePath))
        {
            LightweightMC::Core::Logger::warn(std::string("[ConfigManager] Warning: Could not open ") + filePath + ". Generating default settings.");
            setDefaultsUnlocked();
            m_parser.save(filePath);
            return false;
        }

        // Older builds shipped a smaller server.properties schema (fewer
        // [world] keys, no multiworld activation flags, etc.). Backfill any
        // canonical keys that are missing on disk so subsequent readers see
        // a fully-formed file rather than silently relying on the per-call-
        // site fallback default passed to getBool / getString / get<T>.
        // Existing user-set values are never overwritten -- reconciliation
        // only writes a default when the key is not already present.
        const unsigned migrated = reconcileDefaultsUnlocked();
        if (migrated > 0)
        {
            LightweightMC::Core::Logger::info(
                std::string("[ConfigManager] Upgraded ") + filePath +
                " from an older schema to the current one (" +
                std::to_string(migrated) + " key(s) added).");
            m_parser.save(filePath);
        }

        LightweightMC::Core::Logger::info(std::string("[ConfigManager] Configuration loaded from ") + filePath);
        return true;
    }

    bool save()
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        return m_parser.save(m_filePath);
    }

    std::string getString(const std::string &section, const std::string &key, const std::string &defaultValue = "") const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_parser.getString(section, key, defaultValue);
    }

    template <typename T>
    T get(const std::string &section, const std::string &key, T defaultValue = T{}) const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_parser.get<T>(section, key, defaultValue);
    }

    bool getBool(const std::string &section, const std::string &key, bool defaultValue = false) const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_parser.getBool(section, key, defaultValue);
    }

    void set(const std::string &section, const std::string &key, const std::string &value)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_parser.set(section, key, value);
    }

private:
    ConfigManager() = default;

    // Canonical (current-schema) defaults. Both the "generate a fresh file"
    // path (setDefaultsUnlocked) and the "backfill missing keys from an older
    // file" path (reconcileDefaultsUnlocked) share this list so there is
    // exactly one source of truth for what a current server.properties is
    // expected to contain.
    template <typename Fn>
    void forEachCanonicalDefault(Fn &&fn) const
    {
        auto emit = [&](const char *sec, const char *key, const char *val)
        {
            fn(sec, key, val);
        };

        // [server]
        emit("server", "port", "25565");
        emit("server", "max-players", "100");
        emit("server", "motd", "LightweightMC C++ Engine");

        // [security]
        emit("security", "enable-antiscan", "true");
        emit("security", "blocklist-url", "https://raw.githubusercontent.com/pebblehost/hunter/master/ips.txt");

        // [server]
        // Display name in the server-browser / master-server status datagram.
        // Falls back to the motd text when unset, so the vanilla default
        // keeps the existing behaviour without a new required key.
        emit("server", "server-name", "LightweightMC C++ Engine");

        // [auth]
        // Vanilla-1.8 auth module (Phase 5). Two modes:
        //   auth-mode = offline   (default) -- LoginStart -> LoginSuccess
        //                                        with a locally-minted UUID,
        //                                        no HTTP. This is the "real
        //                                        server, offline toggle"
        //                                        behaviour: wire-format stays
        //                                        vanilla-correct, only the
        //                                        verify hop is skipped.
        //   auth-mode = token     -- Login (Access Token) packet 0x01 arrives
        //                                        first; server verifies via
        //                                        auth-verify-url, then falls
        //                                        back through auth-verify-
        //                                        backup-urls when the primary
        //                                        relay is down or returns
        //                                        HTTP 500 / malformed JSON.
        emit("auth", "auth-mode", "offline");
        // Primary community verify relay. Empty means "do not attempt
        // HTTP verify" (even if auth-mode=token, no HTTP).
        emit("auth", "auth-verify-url", "");
        // Comma-separated trusted backup relays (tried in order when the
        // primary returns non-200 or malformed JSON). Community relay list
        // from minecraft.wiki/Verifying_login_tokens.
        emit("auth", "auth-verify-backup-urls",
             "https://auth.qa.theserverpvp.com/api/verify?token=,"
             "https://player-api.z0r3k4t.wtf/api/verify?token=,"
             "https://session.forgecdn.net/api/verify?token=");
        // Cache token -> (username, uuid) for auth-cache-ttl seconds
        // (0 disables caching; default 24h).
        emit("auth", "auth-cache-ttl", "86400");
        // Reject login on total verify-chain failure. Default false
        // accepts as an offline login (real-server offline fallback)
        // so a transient relay outage does not kick players out.
        emit("auth", "auth-strict", "false");
        // JSON field name in a relay's verify response whose value is a
        // 16-byte hex session key that arms an AES-128 CFB cipher for the
        // session (only enabled for MODERN/LATEST eras).
        emit("auth", "aes-session-key-field", "session_key");
        // Fire-and-forget master-server ping on boot. Off by default so
        // the server does not generate outbound listing traffic unless
        // the user explicitly opts in.
        emit("auth", "master-server-ping", "false");
        // Master-server listing endpoints (comma-separated). Only used
        // when master-server-ping = true.
        emit("auth", "master-server-urls", "");
        // Phase 13: Base64 PNG/JPEG server icon (max 128x128) appended to
        // the \xFE\xFD status datagram as &icon=<base64> for listings that
        // render it. Empty (default) = no &icon field emitted at all, so
        // servers that never set an icon stay byte-identical to before.
        emit("auth", "server-icon", "");

        // [world]
        // Generation preset selector: overworld / nether / aether / superflat / test-grid.
        emit("world", "gen-style", "overworld");
        // Superflat layer preset, only consulted when gen-style=superflat
        // (minecraft:flat | minecraft:stone | minecraft:water).
        emit("world", "superflat-preset", "minecraft:flat");
        // 64-bit world seed: every generation preset mixes this value into
        // its LCG/Perlin stream so the same seed reproduces the same world
        // across runs; 0 mirrors Java Minecraft's "no seed given" default.
        emit("world", "world-seed", "0");

        // 0.0.16 phase 8: world-model trigger. Selects the storage backend
        // that /import-world and /export-world speak. Accepted values:
        //   sqlite  (default) -- the per-plane world-<name>.db SQLite blobs
        //              the server already uses for terrain and containers;
        //              import copies a whole .db file onto the plane's row
        //              set, export writes one out.
        //   anvil   -- the Minecraft "Anvil" region format (.mca): each 16x16
        //              region tile is an LZ4-compressed 16-bit block array
        //              (the same codec as `RegionFile::compressLZ4` in
        //              include/storage/RegionFile.hpp).
        //   legacy  -- the pre-Anvil Java 1.7/1.8 region file (.mcc /
        //              legacy region): gzip-framed per-block 16-bit entries
        //              that map one region tile to a set of chunk blobs.
        // The trigger drives the file-format selector on NetworkManager::
        // importWorldFromPath / exportWorldToPath so `/export-world out.mca`
        // on a sqlite-mode server can still produce an Anvil file via the
        // format-hint argument, and `/import-world mymap.mca overworld` on a
        // legacy-mode server still reads the file as Anvil (suffix beats
        // trigger). Unknown values log a warning and fall back to sqlite.
        emit("world", "world-model", "sqlite");

        // Multiworld: standard dimension-plane activation flags. Each flag
        // gates whether NetworkManager::start() allocates a WorldStorage
        // instance (and pre-generates the surrounding chunks) for that plane.
        // Defaults are all true so a fresh server boots with every vanilla
        // dimension live; flipping a flag to false stops the server serving
        // chunks for that plane while leaving the SQLite world_registry row
        // intact so a later reactivation resumes without re-allocating ids.
        // Overworld (id 0) is the fallback plane every other accessor falls
        // back to, so the server refuses to boot when it is disabled.
        emit("world", "overworld-active", "true");
        emit("world", "nether-active", "true");
        emit("world", "end-active", "true");

        // Aether ("Sky world") is a Minecraft-Legends-style special dimension
        // rather than a standard plane: it lives in the same SQLite world_registry
        // (so /gotor aether can still find it by name) but is only loaded into
        // memory when the aether-active flag is true. The plane's id is
        // configurable via the aether-plane-id key (default 100); NetworkManager::start
        // reads this on boot and allocates the WorldStorage under that id. Pick an
        // id >= 100 (never 0 / -1 / 1 which are the vanilla pre-1.12 slots).
        // Setting a value >= 101 pushes kFirstCustomWorldId higher via
        // `Database::registerWorld` -- which allocates as `max(world_registry.id) + 1`
        // and thus reserves the configured slot automatically for Aether without
        // colliding with `/world create`. Special-dimension planes may be added by
        // future Minecraft Legends / Minecraft Adventure crossovers using
        // another id in the reserved 100-series.
        emit("world", "aether-active", "true");
        emit("world", "aether-plane-id", "100");
        // Per-plane inventory policy and forced gamemode on entry for Aether
        // (0.0.15 plane-scoped inventory chain). Values:
        //   aether-inventory-policy = standard   # standard | separate | restricted
        //   aether-force-gamemode   = none       # none | survival | creative | adventure | spectator
        // "standard" keeps vanilla Minecraft's cross-plane blob sharing (the
        // flat players.inventory_blob column); "separate" opens a per-plane
        // blob on player_inventories(uuid, world_id) so a creative sandbox's
        // loot never leaks back to the mining chain and vice-versa; "restricted"
        // uses the same storage but treats it as one-way (arriving never
        // inherits the source's inventory and leaving never carries items
        // back -- intended for PVP / PVE training maps). "none" keeps whatever
        // game mode the player carried in; survival/creative/adventure/spectator
        // pin session.gamemode on arrival so a creative sandbox stays creative
        // even when the player was on survival elsewhere.
        emit("world", "aether-inventory-policy", "standard");
        emit("world", "aether-force-gamemode", "none");

        // 0.0.15 embedded planes: Creative (id 99), Mining (id 98),
        // Twilight (id 97). These follow the same special-dimension pattern
        // as Aether -- hard-coded default ids in the 100-series band so
        // Database::registerWorld's `max(id) + 1` allocator skips them when
        // /world create allocates 101+, and per-plane *-active /
        // *-plane-id / *-inventory-policy / *-force-gamemode keys mirroring
        // the aether-* set.
        //
        // Creative is a flat buildable sandbox that mirrors the overworld's
        // Perlin height (STONE sub-surface so redstone dust reads clearly
        // against the ground, GRASS_BLOCK top, no ore/trees/caves/
        // structures) and defaults to SEPARATE inventory + forced
        // CREATIVE game mode so players never leak their mining loot into
        // a sandbox. Mining is the resource-dense plane (overworld-shape
        // terrain with a pinned PLAINS surface, 20x vanilla ore density
        // in 10x clusters, no trees/caves/structures). Twilight is the
        // giant-tree forest (flat PLAINS surface + 5 mixed-species giants
        // per chunk via combined17(LOG/LOG2, meta) + combined17(LEAVES/
        // LEAVES2, meta) so trunk and canopy always share a species).
        emit("world", "creative-active", "true");
        emit("world", "creative-plane-id", "99");
        emit("world", "creative-inventory-policy", "separate");
        emit("world", "creative-force-gamemode", "creative");
        emit("world", "mining-active", "true");
        emit("world", "mining-plane-id", "98");
        emit("world", "mining-inventory-policy", "standard");
        emit("world", "mining-force-gamemode", "none");
        emit("world", "twilight-active", "true");
        emit("world", "twilight-plane-id", "97");
        emit("world", "twilight-inventory-policy", "standard");
        emit("world", "twilight-force-gamemode", "none");
    }

    // Overwrite every canonical key with its default. Used when the config
    // file could not be opened and we need to emit a fresh skeleton.
    void setDefaultsUnlocked()
    {
        forEachCanonicalDefault([this](const char *sec, const char *key, const char *val)
        {
            m_parser.set(sec, key, val);
        });
    }

    // Add only the canonical keys that are absent from the loaded file, so a
    // server.properties produced by an older build is migrated in place to
    // the current schema. Returns how many keys were added (0 means the file
    // was already on the current schema, in which case no rewrite is needed).
    unsigned reconcileDefaultsUnlocked()
    {
        unsigned added = 0;
        forEachCanonicalDefault([this, &added](const char *sec, const char *key, const char *val)
        {
            if (m_parser.hasKey(sec, key))
                return;
            m_parser.set(sec, key, val);
            ++added;
        });
        return added;
    }

    IniParser m_parser;
    std::string m_filePath{"server.properties"};
    mutable std::shared_mutex m_mutex;
};
