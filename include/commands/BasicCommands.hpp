#pragma once

#include "core/CommandManager.hpp"
#include "core/ConfigManager.hpp"
#include "network/AntiScan.hpp"
#include "core/Logger.hpp"

// --- Command /help ---
class HelpCommand : public Command
{
public:
    std::string getName() const override { return "help"; }
    std::string getDescription() const override { return "Displays the list of available commands."; }
    std::string getUsage() const override { return "/help"; }
    std::vector<std::string> getAliases() const override { return {"?"}; }
    int getRequiredOpLevel() const override { return 0; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        ctx.reply("--- List of available commands ---");

        const auto &commands = CommandManager::getInstance().getCommands();
        for (const auto &[name, cmd] : commands)
        {
            if (ctx.opLevel >= cmd->getRequiredOpLevel())
            {
                ctx.reply("/" + cmd->getName() + " - " + cmd->getDescription());
            }
        }
    }
};

// --- Command /stop ---
class StopCommand : public Command
{
public:
    StopCommand(std::atomic<bool> &runningRef) : m_running(runningRef) {}

    std::string getName() const override { return "stop"; }
    std::string getDescription() const override { return "Shuts down the server properly."; }
    std::string getUsage() const override { return "/stop"; }
    std::vector<std::string> getAliases() const override
    {
        return {"end", "shutdown", "exit"};
    }
    int getRequiredOpLevel() const override { return 4; } // OP required

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        LightweightMC::Core::Logger::info(std::string("[Server] Stop command issued by ") + ctx.sender);
        ctx.reply("Arrêt du serveur en cours...");

        m_running = false; // Interrupts the main server loop
    }

private:
    std::atomic<bool> &m_running;
};

// --- Command /reload ---
class ReloadCommand : public Command
{
public:
    std::string getName() const override { return "reload"; }
    std::string getDescription() const override { return "Reloads configuration files and modules."; }
    std::string getUsage() const override { return "/reload"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 4; } // OP requis

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        LightweightMC::Core::Logger::info(std::string("[Server] Reloading configuration and modules (triggered by ") + ctx.sender + ")...");

        // 1. Reloading the server.properties file
        if (ConfigManager::getInstance().load("server.properties"))
        {
            ctx.reply("Config server.properties reloaded.");
        }
        else
        {
            ctx.reply("Error reloading server.properties.");
        }

        // 2. Reloading the AntiScan blocklist
        if (ConfigManager::getInstance().getBool("security", "enable-antiscan", true))
        {
            if (AntiScan::reload())
            {
                ctx.reply("AntiScan module reloaded successfully (" + std::to_string(AntiScan::getBlockedCount()) + " IPs).");
            }
            else
            {
                ctx.reply("Error reloading AntiScan module.");
            }
        }

        ctx.reply("Reloading completed.");
    }
};