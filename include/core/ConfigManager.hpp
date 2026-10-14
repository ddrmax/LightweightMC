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
