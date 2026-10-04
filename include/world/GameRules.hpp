#pragma once

#include "storage/Database.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace LightweightMC::World
{
    // -------------------------------------------------------------
    // GameRules — world-level game rule registry (Java Edition).
    //
    // Mirrors Mojang's net.minecraft.world.level.GameRule: every rule
    // has a type (Bool or Int), a vanilla default value, and a current
    // value. The singleton owns the in-memory state and persists each
    // change through Storage::Database into the gamerules table of
    // server_data.db (see NetworkManager's Database instance).
    //
    // Usage:
    //   GameRules::getInstance().init(&db);          // once, at startup
    //   bool v = GameRules::getInstance().getBool("doFireTick");
    //   GameRules::getInstance().setInt("randomTickSpeed", 100);
    // -------------------------------------------------------------
    class GameRules
    {
    public:
        static GameRules &getInstance()
        {
            static GameRules instance;
            return instance;
        }

        GameRules(const GameRules &) = delete;
        GameRules &operator=(const GameRules &) = delete;

        // Load persisted values from the database. Rules absent from the
        // database keep their vanilla defaults. Safe to call once after
        // the Database has been initialized.
        void init(Storage::Database *db);

        // --- Getters (return false if the rule does not exist) ---
        bool getBool(const std::string &name, bool &out) const;
        bool getInt(const std::string &name, int64_t &out) const;

        // --- Setters (validate type + value, persist on success) ---
        bool setBool(const std::string &name, bool value);
        bool setInt(const std::string &name, int64_t value);

        // --- Introspection ---
        bool exists(const std::string &name) const;
        Storage::GameRuleType typeOf(const std::string &name) const;
        bool isBool(const std::string &name) const;
        bool isInt(const std::string &name) const;

        // Current value as the string Mojang would display in chat.
        std::string valueAsString(const std::string &name) const;

        // Vanilla default value as a string (for /gamerule <rule> help).
        std::string defaultValueAsString(const std::string &name) const;

        size_t ruleCount() const { return m_rules.size(); }
        const std::unordered_map<std::string, Storage::GameRuleData> &rules() const { return m_rules; }

    private:
        GameRules();
        ~GameRules() = default;

        void registerDefaults();

        mutable std::mutex m_mutex;
        std::unordered_map<std::string, Storage::GameRuleData> m_rules;
        Storage::Database *m_db{nullptr};
    };

} // namespace LightweightMC::World