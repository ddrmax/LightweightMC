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

    void setDefaultsUnlocked()
    {
        m_parser.set("server", "port", "25565");
        m_parser.set("server", "max-players", "100");
        m_parser.set("server", "motd", "LightweightMC C++ Engine");

        m_parser.set("security", "enable-antiscan", "true");
        m_parser.set("security", "blocklist-url", "https://raw.githubusercontent.com/pebblehost/hunter/master/ips.txt");
    }

    IniParser m_parser;
    std::string m_filePath{"server.properties"};
    mutable std::shared_mutex m_mutex;
};
