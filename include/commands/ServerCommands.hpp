#pragma once

#include "core/CommandManager.hpp"
#include "core/ConfigManager.hpp"
#include "network/AntiScan.hpp"
#include "core/Logger.hpp"
#include "core/utils.hpp"
#include <limits>

// --- Server & session commands (anti-scan, teleport, version, list,
// --- msg, say, op/deop, whitelist, kick, ban/pardon, save-all) ---

// ---Command /antiscan (alias: hunter) ---
class AntiScanCommand : public Command
{
public:
    std::string getName() const override { return "antiscan"; }
    std::string getDescription() const override { return "Manages AntiScan protection and blocklist."; }
    std::string getUsage() const override { return "/antiscan <reload|stats|block|unblock> [ip]"; }
    std::vector<std::string> getAliases() const override { return {"hunter"}; }
    int getRequiredOpLevel() const override { return 3; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        const std::string &subCommand = args[0];

        if (subCommand == "reload")
        {
            if (AntiScan::reload())
            {
                ctx.reply("AntiScan: blocklist reloaded (" + std::to_string(AntiScan::getBlockedCount()) + " Ips).");
            }
            else
            {
                ctx.reply("AntiScan: error during reload.");
            }
        }
        else if (subCommand == "stats")
        {
            ctx.reply("AntiScan: " + std::to_string(AntiScan::getBlockedCount()) + " IP(s) currently blocked.");
        }
        else if (subCommand == "block" && args.size() >= 2)
        {
            AntiScan::addCustomIp(args[1]);
            ctx.reply("Ip " + args[1] + " added to the blocklist.");
        }
        else if (subCommand == "unblock" && args.size() >= 2)
        {
            // AntiScan::unblockIP(args[1]);
            // ctx.reply("IP " + args[1] + " removed from the blocklist.");
        }
        else
        {
            ctx.reply("Unknown subcommand. Use:" + getUsage());
        }
    }
};

// ---Command /tp ---
class TeleportCommand : public Command
{
public:
    std::string getName() const override { return "tp"; }
    std::string getDescription() const override { return "Teleports a player to another player or coordinates."; }
    std::string getUsage() const override
    {
        return "/tp <player> <target> OR /tp [player] <x> <y> <z> ('~' = keep current axis)";
    }
    int getRequiredOpLevel() const override { return 2; }
    // Teleports a player -> console replies broadcast to all PLAY clients.
    bool affectsPlayerState() const override { return true; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        LightweightMC::Core::Logger::debug("[CMD /tp] enter args=" + std::to_string(args.size()) +
                                           " sender=" + ctx.sender +
                                           " console=" + std::string(ctx.isConsole ? "1" : "0"));
        if (args.empty())
        {
            LightweightMC::Core::Logger::debug("[CMD /tp] branch=usage (no args) — no packet emitted");
            ctx.reply("Usage: " + getUsage());
            return;
        }

        // Small helper: parse a coordinate string; on failure return false
        // so we can print a friendly usage error without throwing.
        // "~" is accepted as a wildcard meaning "keep current axis value";
        // it is encoded as a NaN sentinel that NetworkPlayerMeta.cpp's
        // teleportToCoords translates back into the session's coordinate.
        const auto parseCoord = [](const std::string &s, double &out) -> bool
        {
            if (s == "~")
            {
                out = std::numeric_limits<double>::quiet_NaN();
                return true;
            }
            try
            {
                size_t consumed = 0;
                out = std::stod(s, &consumed);
                // Reject trailing garbage like "12abc".
                return consumed == s.size() && !s.empty();
            }
            catch (const std::exception &)
            {
                return false;
            }
        };

        if (args.size() == 1)
        {
            // /tp <target>  -> move the caller to target's current position.
            if (ctx.isConsole)
            {
                LightweightMC::Core::Logger::debug("[CMD /tp] branch=console-no-self — refuses (console has no world position)");
                ctx.reply("The console cannot be teleported without specifying a target player.");
                return;
            }
            LightweightMC::Core::Logger::debug("[CMD /tp] branch=self->target target=" + args[0]);
            if (ctx.teleportPlayerToTarget(ctx.sender, args[0]))
            {
                ctx.reply("Teleportation of " + ctx.sender + " Towards " + args[0] + ".");
            }
            else
            {
                LightweightMC::Core::Logger::debug("[CMD /tp] self->target teleport FAILED (target offline?)");
                ctx.reply("Cannot teleport " + ctx.sender + " to " + args[0] + " — target player is not online.");
            }
        }
        else if (args.size() == 2)
        {
            // /tp <player> <target>
            LightweightMC::Core::Logger::debug("[CMD /tp] branch=player->target player=" + args[0] +
                                               " target=" + args[1]);
            if (ctx.teleportPlayerToTarget(args[0], args[1]))
            {
                ctx.reply("Teleportation of " + args[0] + " Towards " + args[1] + ".");
            }
            else
            {
                LightweightMC::Core::Logger::debug("[CMD /tp] player->target teleport FAILED");
                ctx.reply("Cannot teleport " + args[0] + " to " + args[1] + " — one of them is not online.");
            }
        }
        else if (args.size() == 3)
        {
            // /tp <x> <y> <z>
            double x = 0.0, y = 0.0, z = 0.0;
            if (!parseCoord(args[0], x) || !parseCoord(args[1], y) || !parseCoord(args[2], z))
            {
                LightweightMC::Core::Logger::debug("[CMD /tp] branch=self->coords parse-error");
                ctx.reply("Teleport coordinates must be numbers. Usage: " + getUsage());
                return;
            }
            LightweightMC::Core::Logger::debug("[CMD /tp] branch=self->coords x=" + std::to_string(x) +
                                               " y=" + std::to_string(y) + " z=" + std::to_string(z));
            if (ctx.teleportPlayerToCoords(ctx.sender, x, y, z))
            {
                ctx.reply("Teleportation of " + ctx.sender + " in X:" + args[0] + " Y:" + args[1] + " Z:" + args[2]);
            }
            else
            {
                LightweightMC::Core::Logger::debug("[CMD /tp] self->coords teleport FAILED (self offline?)");
                ctx.reply("Cannot teleport " + ctx.sender + " — sender is not online.");
            }
        }
        else // args.size() >= 4
        {
            // /tp <player> <x> <y> <z>
            double x = 0.0, y = 0.0, z = 0.0;
            if (!parseCoord(args[1], x) || !parseCoord(args[2], y) || !parseCoord(args[3], z))
            {
                LightweightMC::Core::Logger::debug("[CMD /tp] branch=player->coords parse-error");
                ctx.reply("Teleport coordinates must be numbers. Usage: " + getUsage());
                return;
            }
            LightweightMC::Core::Logger::debug("[CMD /tp] branch=player->coords player=" + args[0] +
                                               " x=" + std::to_string(x) + " y=" + std::to_string(y) +
                                               " z=" + std::to_string(z));
            if (ctx.teleportPlayerToCoords(args[0], x, y, z))
            {
                ctx.reply("Teleportation of " + args[0] + " in X:" + args[1] + " Y:" + args[2] + " Z:" + args[3]);
            }
            else
            {
                LightweightMC::Core::Logger::debug("[CMD /tp] player->coords teleport FAILED");
                ctx.reply("Cannot teleport " + args[0] + " — that player is not online.");
            }
        }
    }
};

// ---Command /version ---
class VersionCommand : public Command
{
public:
    std::string getName() const override { return "version"; }
    std::string getDescription() const override { return "Shows Minecraft server and protocol version."; }
    std::string getUsage() const override { return "/version"; }
    std::vector<std::string> getAliases() const override { return {"about", "ver"}; }
    int getRequiredOpLevel() const override { return 0; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        ctx.reply("LightweightMC Version " LWMC_VER " (Protocol v47 -Minecraft 1.8.9)");
    }
};

// ---Command /list ---
class ListCommand : public Command
{
public:
    std::string getName() const override { return "list"; }
    std::string getDescription() const override { return "Shows the list of online players."; }
    std::string getUsage() const override { return "/list"; }
    std::vector<std::string> getAliases() const override { return {"Online", "Who"}; }
    int getRequiredOpLevel() const override { return 0; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        // TODO: Replace with call to PlayerManager
        size_t onlineCount = 0;
        size_t maxPlayers = 20;

        ctx.reply("There is " + std::to_string(onlineCount) + "/" + std::to_string(maxPlayers) + " online players.");
        // TODO: List player names
    }
};

// ---Command /msg (alias: tell, w, pm) ---
class MsgCommand : public Command
{
public:
    std::string getName() const override { return "msg"; }
    std::string getDescription() const override { return "Send a private message to a player."; }
    std::string getUsage() const override { return "/msg <player> <message>"; }
    std::vector<std::string> getAliases() const override { return {"Tell", "W", "Pm"}; }
    int getRequiredOpLevel() const override { return 0; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.size() < 2)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        const std::string &target = args[0];

        // Reconstruction of the message from the 2nd argument
        std::string message;
        for (size_t i = 1; i < args.size(); ++i)
        {
            if (i > 1)
                message += " ";
            message += args[i];
        }

        ctx.reply("[Me -> " + target + "] " + message);
        // Chat-only command: no game-state change. Echo to the server
        // console only when the request originated there so in-game chat
        // traffic does not spam the log.
        if (ctx.isConsole)
        {
            LightweightMC::Core::Logger::info("[private message] " + ctx.sender + " > " + target + ": " + message);
        }
        // TODO: Send the packet to the `target` recipient
    }
};

// ---Command /say (message as server) ---
class SayCommand : public Command
{
public:
    std::string getName() const override { return "say"; }
    std::string getDescription() const override { return "Sends a global message broadcast as a server."; }
    std::string getUsage() const override { return "/say <message>"; }
    std::vector<std::string> getAliases() const override { return {"Broadcast", "Bc"}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        std::string message;
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (i > 0)
                message += " ";
            message += args[i];
        }

        std::string broadcastText = "[" + ctx.sender + "] " + message;
        // Chat broadcast: no game-state change. Log on the server console
        // only when the request came from the console, so in-game chat
        // broadcasts stay inside the chat stream.
        if (ctx.isConsole)
        {
            LightweightMC::Core::Logger::info("[Server Broadcast] " + broadcastText);
        }

        // TODO: NetworkManager::broadcastChatMessage(broadcastText)
        ctx.reply("Broadcast sent : " + message);
    }
};

// ---Command /op ---
class OpCommand : public Command
{
public:
    std::string getName() const override { return "op"; }
    std::string getDescription() const override { return "Gives administrator rights to a player."; }
    std::string getUsage() const override { return "/op <player>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 3; }
    // Mutates a player's operator level -> console replies should broadcast.
    bool affectsPlayerState() const override { return true; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        std::string target = args[0];
        if (!ctx.setOpLevel(target, 3))
        {
            ctx.reply("The player " + target + " is not online or could not be updated.");
            return;
        }

        LightweightMC::Core::Logger::info(ctx.sender + " made " + target + " an operator (level 3).");
        ctx.reply("The player " + target + " is now an operator.");
    }
};

// ---/drop command ---
class DeopCommand : public Command
{
public:
    std::string getName() const override { return "deop"; }
    std::string getDescription() const override { return "Removes administrator rights from a player."; }
    std::string getUsage() const override { return "/deop <player>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 3; }
    // Mutates a player's operator level -> console replies should broadcast.
    bool affectsPlayerState() const override { return true; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        std::string target = args[0];
        if (!ctx.setOpLevel(target, 0))
        {
            ctx.reply("The player " + target + " is not online or could not be updated.");
            return;
        }

        LightweightMC::Core::Logger::info(ctx.sender + " removed operator rights from " + target + ".");
        ctx.reply("The player " + target + " is no longer an operator.");
    }
};

// ---Command /whitelist ---
class WhitelistCommand : public Command
{
public:
    std::string getName() const override { return "whitelist"; }
    std::string getDescription() const override { return "Manage server whitelist."; }
    std::string getUsage() const override { return "/whitelist <add|remove|on|off> [player]"; }
    std::vector<std::string> getAliases() const override { return {"Wl"}; }
    int getRequiredOpLevel() const override { return 3; }
    // Whitelist mutations touch player joinability -> broadcast on console.
    bool affectsPlayerState() const override { return true; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        std::string subCmd = args[0];
        if (subCmd == "On")
        {
            // TODO: Enable whitelist in config
            ctx.reply("The whitelist is now activated.");
        }
        else if (subCmd == "Off")
        {
            // TODO: Disable whitelist
            ctx.reply("The whitelist is now deactivated.");
        }
        else if (subCmd == "Add" && args.size() >= 2)
        {
            std::string target = args[1];
            // TODO: Add the player to whitelist.json
            ctx.reply("Adding" + target + "to the whitelist.");
        }
        else if (subCmd == "Remove" && args.size() >= 2)
        {
            std::string target = args[1];
            // TODO: Remove player from whitelist.json
            ctx.reply("Removal of " + target + " from the whitelist.");
        }
        else
        {
            ctx.reply("Usage: " + getUsage());
        }
    }
};

// ---Command /kick ---
class KickCommand : public Command
{
public:
    std::string getName() const override { return "kick"; }
    std::string getDescription() const override { return "Kicks a player from the server."; }
    std::string getUsage() const override { return "/kick <player> [reason]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }
    // Disconnects a connected player -> broadcast on console.
    bool affectsPlayerState() const override { return true; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        std::string target = args[0];
        std::string reason = "Kicked out by an administrator.";
        if (args.size() > 1)
        {
            reason = "";
            for (size_t i = 1; i < args.size(); ++i)
            {
                if (i > 1)
                    reason += " ";
                reason += args[i];
            }
        }

        // TODO: Find the player's clientFd, send packet 0x40 (Disconnect) and close the connection
        ctx.reply("The player" + target + " was expelled for: " + reason);
    }
};

// ---Command /ban ---
class BanCommand : public Command
{
public:
    std::string getName() const override { return "ban"; }
    std::string getDescription() const override { return "Bans a player from the server."; }
    std::string getUsage() const override { return "/ban <player> [reason]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 3; }
    // Bans a player (and disconnects if online) -> broadcast on console.
    bool affectsPlayerState() const override { return true; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        std::string target = args[0];
        std::string reason = "Banned by an administrator.";
        if (args.size() > 1)
        {
            reason = "";
            for (size_t i = 1; i < args.size(); ++i)
            {
                if (i > 1)
                    reason += " ";
                reason += args[i];
            }
        }

        // TODO: Add the nickname to banned-players.json and kicked if online
        ctx.reply("The player" + target + "was banned.");
    }
};

// ---Command /ban-ip ---
class BanIpCommand : public Command
{
public:
    std::string getName() const override { return "ban-ip"; }
    std::string getDescription() const override { return "Bans a player's IP address."; }
    std::string getUsage() const override { return "/ban-ip <player|IP> [reason]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 3; }
    // IP ban can drop connected players sharing that IP -> broadcast.
    bool affectsPlayerState() const override { return true; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        std::string target = args[0];
        // TODO:Resolve IP if passed pseudo, add to AntiScan/banned-ips.json and disconnect socket
        ctx.reply("IP ban applied for " + target);
    }
};

// ---Command /pardon ---
class PardonCommand : public Command
{
public:
    std::string getName() const override { return "pardon"; }
    std::string getDescription() const override { return "Unban a player."; }
    std::string getUsage() const override { return "/pardon <pseudo>"; }
    std::vector<std::string> getAliases() const override { return {"Humban"}; }
    int getRequiredOpLevel() const override { return 3; }
    // Removes a ban for a player -> broadcast on console.
    bool affectsPlayerState() const override { return true; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        std::string target = args[0];
        // TODO: Remove nickname from banned-players.json
        ctx.reply("The player " + target + " was unbanned.");
    }
};

// ---Command /pardon-ip ---
class PardonIpCommand : public Command
{
public:
    std::string getName() const override { return "pardon-ip"; }
    std::string getDescription() const override { return "Unbans an IP address."; }
    std::string getUsage() const override { return "/pardon-ip <IP>"; }
    std::vector<std::string> getAliases() const override { return {"Unban ip"}; }
    int getRequiredOpLevel() const override { return 3; }
    // Removes an IP ban so a player can rejoin -> broadcast on console.
    bool affectsPlayerState() const override { return true; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        std::string ip = args[0];
        // TODO: Remove IP from banned-ips.json and AntiScan
        ctx.reply("IP address " + ip + " was unbanned.");
    }
};

// ---Command /save-all ---
class SaveAllCommand : public Command
{
public:
    std::string getName() const override { return "save-all"; }
    std::string getDescription() const override { return "Forces immediate saving of the world to disk."; }
    std::string getUsage() const override { return "/save-all"; }
    std::vector<std::string> getAliases() const override { return {"Save"}; }
    int getRequiredOpLevel() const override { return 4; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        LightweightMC::Core::Logger::info("World backup requested by " + ctx.sender);

        // TODO: WorldStorage::getInstance().flush() or synchronization of the SQLite database
        ctx.reply("World backup completed successfully.");
    }
};

class PluginsCommand : public Command
{
public:
    std::string getName() const override { return "plugins"; }
    std::string getDescription() const override { return "List all installed plugins."; }
    std::string getUsage() const override { return "/Plugins"; }
    std::vector<std::string> getAliases() const override { return {"Pl"}; }
    int getRequiredOpLevel() const override { return 0; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        // TODO: Query the PluginManager
        ctx.reply("Plugins (0): ");
    }
};

class LwmcCommand : public Command
{
public:
    std::string getName() const override { return "lwmc"; }
    std::string getDescription() const override { return "Diagnostic commands and advanced server management."; }
    std::string getUsage() const override { return "/lwmc <entity list|track>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 3; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        if (args[0] == "Entity" && args.size() >= 2 && args[1] == "List")
        {
            // TODO: List loaded entities
            ctx.reply("Loaded entities: 0");
        }
        else if (args[0] == "Track" && args.size() >= 2)
        {
            std::string type = args[1];
            // TODO: Start resource tracking for type
            ctx.reply("Active tracking for type: " + type);
        }
        else
        {
            ctx.reply("Usage: " + getUsage());
        }
    }
};

class BanlistCommand : public Command
{
public:
    std::string getName() const override { return "banlist"; }
    std::string getDescription() const override { return "Shows the ban list."; }
    std::string getUsage() const override { return "/banlist [ips|players]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 3; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        ctx.reply("There are 0 ban(s).");
    }
};

class DatapackCommand : public Command
{
public:
    std::string getName() const override { return "data pack"; }
    std::string getDescription() const override { return "Checks loaded data packs."; }
    std::string getUsage() const override { return "/datapack <list|enable|disable>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        ctx.reply("No specific datapack loaded.");
    }
};

class DefaultGamemodeCommand : public Command
{
public:
    std::string getName() const override { return "defaultgamemode"; }
    std::string getDescription() const override { return "Sets the default game mode."; }
    std::string getUsage() const override { return "/defaultgamemode <survival|creative|adventure|spectator>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Default game mode set to: " + args[0]);
    }
};

class DifficultyCommand : public Command
{
public:
    std::string getName() const override { return "difficulty"; }
    std::string getDescription() const override { return "Sets the difficulty level."; }
    std::string getUsage() const override { return "/difficulty <peaceful|easy|normal|hard>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Difficulty set to: " + args[0]);
    }
};

class GameruleCommand : public Command
{
public:
    std::string getName() const override { return "gamerule"; }
    std::string getDescription() const override { return "Sets or consults the value of a game rule."; }
    std::string getUsage() const override { return "/gamerule <rule> [value]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        auto &gr = LightweightMC::World::GameRules::getInstance();

        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        const std::string &ruleName = args[0];

        if (!gr.exists(ruleName))
        {
            ctx.reply("Unknown game rule '" + ruleName + "'");
            return;
        }

        // Query only (no value supplied).
        if (args.size() == 1)
        {
            ctx.reply(ruleName + " = " + gr.valueAsString(ruleName));
            return;
        }

        // Set a new value.
        const std::string &valueStr = args[1];

        if (gr.isBool(ruleName))
        {
            bool parsed;
            if (!parseGameRuleBool(valueStr, parsed))
            {
                ctx.reply("Invalid boolean value '" + valueStr + "' for game rule '" + ruleName + "'");
                return;
            }
            gr.setBool(ruleName, parsed);
            LightweightMC::Core::Logger::info("[GameRule] " + ctx.sender + " set '" + ruleName + "' = " + (parsed ? "true" : "false"));
            ctx.reply("Set " + ruleName + " to " + (parsed ? "true" : "false"));
        }
        else
        {
            int64_t parsed;
            if (!parseGameRuleInt(valueStr, parsed))
            {
                ctx.reply("Invalid integer value '" + valueStr + "' for game rule '" + ruleName + "'");
                return;
            }
            gr.setInt(ruleName, parsed);
            LightweightMC::Core::Logger::info("[GameRule] " + ctx.sender + " set '" + ruleName + "' = " + std::to_string(parsed));
            ctx.reply("Set " + ruleName + " to " + std::to_string(parsed));
        }
    }

private:
    static bool parseGameRuleBool(const std::string &s, bool &out)
    {
        if (s == "true" || s == "True")
        {
            out = true;
            return true;
        }
        if (s == "false" || s == "False")
        {
            out = false;
            return true;
        }
        return false;
    }

    static bool parseGameRuleInt(const std::string &s, int64_t &out)
    {
        if (s.empty())
            return false;
        try
        {
            size_t pos = 0;
            long long v = std::stoll(s, &pos);
            if (pos != s.size())
                return false;
            out = static_cast<int64_t>(v);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
};

class PublishCommand : public Command
{
public:
    std::string getName() const override { return "publish"; }
    std::string getDescription() const override { return "Opens a single player world to a local network."; }
    std::string getUsage() const override { return "/publish [port]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 4; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        ctx.reply("The server is already in dedicated multiplayer mode.");
    }
};

class SaveOffCommand : public Command
{
public:
    std::string getName() const override { return "save-off"; }
    std::string getDescription() const override { return "Disable automatic server backups."; }
    std::string getUsage() const override { return "/save-off"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 4; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        ctx.reply("Autosave disabled.");
    }
};

class SaveOnCommand : public Command
{
public:
    std::string getName() const override { return "save-on"; }
    std::string getDescription() const override { return "Enables automatic server backups."; }
    std::string getUsage() const override { return "/save-on"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 4; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        ctx.reply("Autosave enabled.");
    }
};

class SetIdleTimeoutCommand : public Command
{
public:
    std::string getName() const override { return "setidletimeout"; }
    std::string getDescription() const override { return "Sets the time before inactive players are kicked out."; }
    std::string getUsage() const override { return "/setidletimeout <minutes>"; }
    std::vector<std::string> getAliases() const override
    {
        return {};

        // --- Command /tick (mockup) ---
        class TickCommand : public Command
        {
        public:
            std::string getName() const override { return "tick"; }
            std::string getDescription() const override { return "Controls the global tick speed and game time progression."; }
            std::string getUsage() const override { return "/tick <show|set> ..."; }
            int getRequiredOpLevel() const override { return 4; }

            void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
            {
                (void)args;
                // TODO: Implement tick rate management.
                ctx.reply("Tick control updated.");
            }
        };
    }
    int getRequiredOpLevel() const override { return 3; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Idle timeout set to " + args[0] + " minute(s).");
    }
};
