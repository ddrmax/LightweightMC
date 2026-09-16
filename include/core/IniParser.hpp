#pragma once

#include "core/Logger.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <concepts>

class IniParser
{
public:
    struct Entry
    {
        enum class Type
        {
            CommentOrEmpty,
            Section,
            KeyValue
        };
        Type type;
        std::string rawContent;
        std::string section;
        std::string key;
        std::string value;
    };

    IniParser() = default;

    bool load(const std::string &filePath)
    {
        m_filePath = filePath;
        std::ifstream file(filePath);
        if (!file.is_open())
            return false;

        m_entries.clear();
        m_lookup.clear();

        std::string currentSection = "";
        std::string line;

        while (std::getline(file, line))
        {
            std::string_view sv = trim(line);

            if (sv.empty() || sv.front() == '#' || sv.front() == ';')
            {
                m_entries.push_back({Entry::Type::CommentOrEmpty, line, currentSection, "", ""});
                continue;
            }

            if (sv.front() == '[' && sv.back() == ']')
            {
                currentSection = std::string(trim(sv.substr(1, sv.size() - 2)));
                m_entries.push_back({Entry::Type::Section, line, currentSection, "", ""});
                continue;
            }

            auto pos = sv.find('=');
            if (pos != std::string_view::npos)
            {
                std::string key = std::string(trim(sv.substr(0, pos)));
                std::string value = std::string(trim(sv.substr(pos + 1)));

                m_entries.push_back({Entry::Type::KeyValue, line, currentSection, key, value});
                m_lookup[currentSection][key] = m_entries.size() - 1;
            }
            else
            {
                m_entries.push_back({Entry::Type::CommentOrEmpty, line, currentSection, "", ""});
            }
        }

        return true;
    }

    bool save(const std::string &filePath = "")
    {
        std::string targetPath = filePath.empty() ? m_filePath : filePath;
        if (targetPath.empty())
            return false;

        std::ofstream file(targetPath, std::ios::trunc);
        if (!file.is_open())
            return false;

        for (const auto &entry : m_entries)
        {
            switch (entry.type)
            {
            case Entry::Type::CommentOrEmpty:
            case Entry::Type::Section:
                file << entry.rawContent << "\n";
                break;
            case Entry::Type::KeyValue:
                file << entry.key << " = " << entry.value << "\n";
                break;
            }
        }

        return true;
    }

    void set(const std::string &section, const std::string &key, const std::string &value)
    {
        auto secIt = m_lookup.find(section);
        if (secIt != m_lookup.end())
        {
            auto keyIt = secIt->second.find(key);
            if (keyIt != secIt->second.end())
            {
                size_t index = keyIt->second;
                m_entries[index].value = value;
                return;
            }
        }

        bool sectionExists = (m_lookup.find(section) != m_lookup.end());
        if (!sectionExists && !section.empty())
        {
            m_entries.push_back({Entry::Type::Section, "[" + section + "]", section, "", ""});
        }

        m_entries.push_back({Entry::Type::KeyValue, key + " = " + value, section, key, value});
        m_lookup[section][key] = m_entries.size() - 1;
    }

    std::string getString(const std::string &section, const std::string &key, const std::string &defaultValue = "") const
    {
        auto secIt = m_lookup.find(section);
        if (secIt != m_lookup.end())
        {
            auto keyIt = secIt->second.find(key);
            if (keyIt != secIt->second.end())
            {
                return m_entries[keyIt->second].value;
            }
        }
        return defaultValue;
    }

    template <typename T>
        requires std::integral<T> || std::floating_point<T>
    T get(const std::string &section, const std::string &key, T defaultValue = T{}) const
    {
        std::string valStr = getString(section, key);
        if (valStr.empty())
            return defaultValue;
        std::stringstream ss(valStr);
        T result;
        if (ss >> result)
            return result;
        return defaultValue;
    }

    bool getBool(const std::string &section, const std::string &key, bool defaultValue = false) const
    {
        std::string val = getString(section, key);
        if (val.empty())
            return defaultValue;
        std::transform(val.begin(), val.end(), val.begin(), [](unsigned char c)
                       { return std::tolower(c); });
        return (val == "true" || val == "1" || val == "yes" || val == "on");
    }

private:
    static constexpr std::string_view trim(std::string_view str)
    {
        constexpr std::string_view whitespace = " \t\r\n";
        const auto start = str.find_first_not_of(whitespace);
        if (start == std::string_view::npos)
            return {};
        const auto end = str.find_last_not_of(whitespace);
        return str.substr(start, end - start + 1);
    }

    std::string m_filePath;
    std::vector<Entry> m_entries;
    std::unordered_map<std::string, std::unordered_map<std::string, size_t>> m_lookup;
};