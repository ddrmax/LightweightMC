#pragma once

#include "core/Logger.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <sstream>

struct CommandContext
{
    std::string sender;
    bool isConsole{false};
    int opLevel{0};

    void reply(const std::string &message) const
    {
        if (isConsole)
        {
            LightweightMC::Core::Logger::info(std::string("[Command] ") + message);
        }
        else
        {
            // Network transmission to the player
        }
    }
};

class Command
{
public:
    virtual ~Command() = default;
    virtual std::string getName() const = 0;
    virtual std::string getDescription() const = 0;
    virtual std::string getUsage() const = 0;
    virtual std::vector<std::string> getAliases() const { return {}; } // List of aliases
    virtual int getRequiredOpLevel() const { return 0; }
    virtual void execute(const CommandContext &ctx, const std::vector<std::string> &args) = 0;
};

class CommandManager
{
public:
    static CommandManager &getInstance()
    {
        static CommandManager instance;
        return instance;
    }

    CommandManager(const CommandManager &) = delete;
    CommandManager &operator=(const CommandManager &) = delete;

    void registerCommand(std::shared_ptr<Command> cmd);
    bool executeCommand(const CommandContext &ctx, const std::string &commandLine);

    // Returns the map of main commands (excluding aliases) for /help.
    const std::unordered_map<std::string, std::shared_ptr<Command>> &getCommands() const
    {
        return m_primaryCommands;
    }

private:
    CommandManager() = default;

    // m_commands contains the primary name AND all aliases pointing to the same std::shared_ptrs.
    std::unordered_map<std::string, std::shared_ptr<Command>> m_commands;
    // Filtered map containing only the primary name (for help display)
    std::unordered_map<std::string, std::shared_ptr<Command>> m_primaryCommands;
};