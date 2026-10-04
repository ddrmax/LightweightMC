#pragma once

#include "core/Logger.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <sstream>
#include <functional>

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
        else if (replyCallback)
        {
            // Send a chat packet to the player who issued the command.
            replyCallback(message);
        }
    }

    // Optional callback used by /op and /deop to change a player's operator
    // status. It must update the live session (if the player is online) and
    // persist the new level to the database. Returns true on success.
    // Defaults to a no-op so commands can be exercised from the console.
    std::function<bool(const std::string &playerName, int opLevel)> setOpLevel = [](const std::string &, int) -> bool
    {
        return false;
    };

    // Optional callback used by reply() to deliver chat messages to the
    // player who issued the command. Defaults to a no-op so commands can be
    // exercised from the console (or logged only).
    std::function<void(const std::string &)> replyCallback = [](const std::string &) {};
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