#include "core/CommandManager.hpp"
#include "core/Logger.hpp"

void CommandManager::registerCommand(std::shared_ptr<Command> cmd)
{
    if (!cmd)
        return;

    std::string primaryName = cmd->getName();

    if (m_commands.find(primaryName) != m_commands.end())
    {
        LightweightMC::Core::Logger::warn("[CommandManager] Overwriting existing command or alias: " + primaryName);
    }
    else
    {
        LightweightMC::Core::Logger::info("[CommandManager] Registered command: /" + primaryName);
    }

    // Saving the main command
    m_primaryCommands[primaryName] = cmd;
    m_commands[primaryName] = cmd;

    // Registering aliases
    for (const auto &alias : cmd->getAliases())
    {
        if (alias.empty())
            continue;

        if (m_commands.find(alias) != m_commands.end())
        {
            LightweightMC::Core::Logger::warn("[CommandManager] Alias /" + alias + " conflicts with an existing command or alias.");
            continue;
        }

        m_commands[alias] = cmd;
        LightweightMC::Core::Logger::info("[CommandManager] Registered alias: /" + alias + " -> /" + primaryName);
    }
}

bool CommandManager::executeCommand(const CommandContext &ctx, const std::string &commandLine)
{
    if (commandLine.empty())
        return false;

    std::string cleanLine = commandLine;
    if (cleanLine.front() == '/')
    {
        cleanLine = cleanLine.substr(1);
    }

    std::stringstream ss(cleanLine);
    std::string cmdName;
    ss >> cmdName;

    std::vector<std::string> args;
    std::string arg;
    while (ss >> arg)
    {
        args.push_back(arg);
    }

    // Direct search in the map (finds the main name or alias)
    auto it = m_commands.find(cmdName);
    if (it == m_commands.end())
    {
        ctx.reply("Unknown command. Type \"/help\" for help.");
        LightweightMC::Core::Logger::warn("[CommandManager] Unknown command attempt: " + cmdName + " by " + ctx.sender);
        return false;
    }

    auto &cmd = it->second;

    if (ctx.opLevel < cmd->getRequiredOpLevel())
    {
        ctx.reply("You do not have permission to execute this command.");
        LightweightMC::Core::Logger::warn("[CommandManager] Permission denied for " + ctx.sender + " on command /" + cmdName);
        return false;
    }

    try
    {
        cmd->execute(ctx, args);
    }
    catch (const std::exception &e)
    {
        LightweightMC::Core::Logger::error("[CommandManager] Exception caught executing /" + cmdName + ": " + e.what());
        ctx.reply("An internal error occurred while executing this command.");
        return false;
    }
    catch (...)
    {
        LightweightMC::Core::Logger::error("[CommandManager] Unknown exception caught executing /" + cmdName);
        ctx.reply("An internal error occurred while executing this command.");
        return false;
    }

    return true;
}