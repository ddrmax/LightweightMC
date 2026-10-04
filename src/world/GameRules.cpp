#include "world/GameRules.hpp"
#include "core/Logger.hpp"

namespace LightweightMC::World
{
    namespace
    {
        using Storage::GameRuleType;

        // Convenience registration helpers (mirrors Mojang's RuleType).
        inline void regBool(std::unordered_map<std::string, Storage::GameRuleData> &m, const char *name, bool def)
        {
            Storage::GameRuleData r;
            r.name = name;
            r.type = GameRuleType::Bool;
            r.boolValue = def;
            m[name] = std::move(r);
        }

        inline void regInt(std::unordered_map<std::string, Storage::GameRuleData> &m, const char *name, int64_t def)
        {
            Storage::GameRuleData r;
            r.name = name;
            r.type = GameRuleType::Int;
            r.intValue = def;
            m[name] = std::move(r);
        }
    } // namespace

    GameRules::GameRules()
    {
        registerDefaults();
    }

    void GameRules::registerDefaults()
    {
        // ---------------------------------------------------------
        // Vanilla Java Edition defaults (1.21.x), per minecraft.wiki
        // /w/Game_rule. Categories: Chat, Drops, Misc, Player,
        // Spawning, World Updates.
        // ---------------------------------------------------------

        // --- Chat ---
        regBool(m_rules, "command_block_output", true);
        regBool(m_rules, "log_admin_commands", true);
        regBool(m_rules, "send_command_feedback", true);
        regBool(m_rules, "show_advancement_messages", true);
        regBool(m_rules, "show_death_messages", true);

        // --- Drops ---
        regBool(m_rules, "block_drops", true);
        regBool(m_rules, "block_explosion_drop_decay", true);
        regBool(m_rules, "entity_drops", true);
        regBool(m_rules, "mob_drops", true);
        regBool(m_rules, "mob_explosion_drop_decay", true);
        regBool(m_rules, "projectiles_can_break_blocks", true);
        regBool(m_rules, "tnt_explosion_drop_decay", false);

        // --- Misc ---
        regBool(m_rules, "command_blocks_work", true);
        regBool(m_rules, "global_sound_events", true);
        regInt(m_rules, "max_block_modifications", 32768);
        regInt(m_rules, "max_command_forks", 65536);
        regInt(m_rules, "max_command_sequence_length", 65536);
        regBool(m_rules, "reduced_debug_info", false);
        regBool(m_rules, "tnt_explodes", true);

        // --- Player ---
        regBool(m_rules, "forgive_dead_players", true);
        regInt(m_rules, "max_entity_cramming", 24);
        regBool(m_rules, "mob_griefing", true);
        regBool(m_rules, "raids", true);
        regBool(m_rules, "universal_anger", false);
        regBool(m_rules, "allow_entering_nether_using_portals", true);
        regBool(m_rules, "drowning_damage", true);
        regBool(m_rules, "elytra_movement_check", true);
        regBool(m_rules, "ender_pearls_vanish_on_death", true);
        regBool(m_rules, "fall_damage", true);
        regBool(m_rules, "fire_damage", true);
        regInt(m_rules, "fire_spread_radius_around_player", 128);
        regBool(m_rules, "freeze_damage", true);
        regBool(m_rules, "immediate_respawn", false);
        regBool(m_rules, "keep_inventory", false);
        regBool(m_rules, "limited_crafting", false);
        regBool(m_rules, "locator_bar", true);
        regBool(m_rules, "natural_health_regeneration", true);
        regBool(m_rules, "player_movement_check", true);
        regInt(m_rules, "players_nether_portal_creative_delay", 0);
        regInt(m_rules, "players_nether_portal_default_delay", 80);
        regInt(m_rules, "players_sleeping_percentage", 100);
        regBool(m_rules, "pvp", true);
        regInt(m_rules, "respawn_radius", 10);
        regBool(m_rules, "spectators_generate_chunks", true);

        // --- Spawning ---
        regBool(m_rules, "spawn_mobs", true);
        regBool(m_rules, "spawn_monsters", true);
        regBool(m_rules, "spawn_patrols", true);
        regBool(m_rules, "spawn_phantoms", true);
        regBool(m_rules, "spawn_wandering_traders", true);
        regBool(m_rules, "spawn_wardens", true);
        regBool(m_rules, "spawner_blocks_work", true);

        // --- World Updates ---
        regBool(m_rules, "advance_time", true);
        regBool(m_rules, "advance_weather", true);
        regBool(m_rules, "lava_source_conversion", false);
        regInt(m_rules, "max_snow_accumulation_height", 1);
        regInt(m_rules, "random_tick_speed", 3);
        regBool(m_rules, "spread_vines", true);
        regBool(m_rules, "water_source_conversion", true);

        // --- Legacy / renamed rules still accepted by the server (1.13-1.20) ---
        regBool(m_rules, "doFireTick", true);
        regInt(m_rules, "functioncommandlimit", 10000);
        regBool(m_rules, "recipesunlock", true);
        regBool(m_rules, "respawnblocksexplode", true);
        regBool(m_rules, "showbordereffect", true);
        regBool(m_rules, "showcoordinates", true);
        regBool(m_rules, "showdaysplayed", false);
        regBool(m_rules, "showrecipemessages", true);
        regBool(m_rules, "showtags", true);
    }

    void GameRules::init(Storage::Database *db)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_db = db;

        if (!m_db)
            return;

        // Overlay persisted values on top of the vanilla defaults.
        std::unordered_map<std::string, Storage::GameRuleData> persisted;
        if (m_db->loadAllGameRules(persisted))
        {
            for (auto &[name, data] : persisted)
            {
                auto it = m_rules.find(name);
                if (it != m_rules.end())
                {
                    // Only trust the stored value if the rule type matches.
                    if (data.type == it->second.type)
                        it->second = std::move(data);
                }
            }
        }

        Core::Logger::info("[GAMERULES] Loaded " + std::to_string(m_rules.size()) + " game rules (" +
                           std::to_string(persisted.size()) + " persisted overrides).");
    }

    bool GameRules::getBool(const std::string &name, bool &out) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_rules.find(name);
        if (it == m_rules.end() || it->second.type != Storage::GameRuleType::Bool)
            return false;
        out = it->second.boolValue;
        return true;
    }

    bool GameRules::getInt(const std::string &name, int64_t &out) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_rules.find(name);
        if (it == m_rules.end() || it->second.type != Storage::GameRuleType::Int)
            return false;
        out = it->second.intValue;
        return true;
    }

    bool GameRules::setBool(const std::string &name, bool value)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_rules.find(name);
        if (it == m_rules.end())
            return false;
        if (it->second.type != Storage::GameRuleType::Bool)
            return false; // type mismatch: rule is an integer

        it->second.boolValue = value;

        if (m_db)
            m_db->saveGameRule(it->second);
        return true;
    }

    bool GameRules::setInt(const std::string &name, int64_t value)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_rules.find(name);
        if (it == m_rules.end())
            return false;
        if (it->second.type != Storage::GameRuleType::Int)
            return false; // type mismatch: rule is a boolean

        it->second.intValue = value;

        if (m_db)
            m_db->saveGameRule(it->second);
        return true;
    }

    bool GameRules::exists(const std::string &name) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_rules.find(name) != m_rules.end();
    }

    Storage::GameRuleType GameRules::typeOf(const std::string &name) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_rules.find(name);
        if (it == m_rules.end())
            return Storage::GameRuleType::Bool; // unreachable for callers that check exists() first
        return it->second.type;
    }

    bool GameRules::isBool(const std::string &name) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_rules.find(name);
        return it != m_rules.end() && it->second.type == Storage::GameRuleType::Bool;
    }

    bool GameRules::isInt(const std::string &name) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_rules.find(name);
        return it != m_rules.end() && it->second.type == Storage::GameRuleType::Int;
    }

    std::string GameRules::valueAsString(const std::string &name) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_rules.find(name);
        if (it == m_rules.end())
            return "?";
        if (it->second.type == Storage::GameRuleType::Bool)
            return it->second.boolValue ? "true" : "false";
        return std::to_string(it->second.intValue);
    }

    std::string GameRules::defaultValueAsString(const std::string &name) const
    {
        // Reconstruct the vanilla default from a fresh registry (no DB overlay).
        GameRules fresh;
        return fresh.valueAsString(name);
    }

} // namespace LightweightMC::World
