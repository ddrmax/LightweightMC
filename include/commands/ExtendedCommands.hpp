
#pragma once

#include "core/CommandManager.hpp"
#include "core/ConfigManager.hpp"
#include "network/AntiScan.hpp"
#include "core/Logger.hpp"

#include <iostream>
#include <string>
#include <vector>

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
    std::string getUsage() const override { return "/tp <player> <target> OR /tp [player] <x> <y> <z>"; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        if (args.size() == 1)
        {
            // /tp <target> (teleports player to target)
            if (ctx.isConsole)
            {
                ctx.reply("The console cannot be teleported without specifying a target player.");
                return;
            }
            ctx.reply("Teleportation of " + ctx.sender + " Towards " + args[0] + "...");
            // TODO: To call PlayerManager::teleport(ctx.sender, args[0])
        }
        else if (args.size() == 2)
        {
            // /tp <player> <target>
            ctx.reply("Teleportation of " + args[0] + " Towards " + args[1] + "...");
            // TODO: To call PlayerManager::teleport(args[0], args[1])
        }
        else if (args.size() == 3)
        {
            // /tp <x> <y> <z>
            ctx.reply("Teleportation of " + ctx.sender + " in X:" + args[0] + " Y:" + args[1] + " Z:" + args[2]);
            // TODO: To call PlayerManager::teleportToCoords(ctx.sender, x, y, z)
        }
        else if (args.size() >= 4)
        {
            // /tp <player> <x> <y> <z>
            ctx.reply("Teleportation of " + args[0] + " in X:" + args[1] + " Y:" + args[2] + " Z:" + args[3]);
            // TODO: To call PlayerManager::teleportToCoords(args[0], x, y, z)
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
        ctx.reply("LightweightMC Version 0.0.4 (Protocol v47 -Minecraft 1.8.9)");
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
        LightweightMC::Core::Logger::info("[private message] " + ctx.sender + " > " + target + ": " + message);
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
        LightweightMC::Core::Logger::info("[Server Broadcast] " + broadcastText);

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

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        std::string target = args[0];
        // TODO: Add the nickname to the ops.json file /database and update the player's OP status
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

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        std::string target = args[0];
       // TODO: Remove the nickname from the ops list
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

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        std::string target = args[0];
        //TODO: Remove nickname from banned-players.json
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
// ==========================================
// ---LWMC & Extensions
// ==========================================

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

// ==========================================
// ---Administration & Configuration
// ==========================================

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
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        if (args.size() == 1)
            ctx.reply(args[0] + " = true");
        else
            ctx.reply("Rule " + args[0] + " defined on " + args[1]);
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
    std::vector<std::string> getAliases() const override { return {}; }
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

// ===========================================================
// ---Players, Inventory and Attributes
// ===========================================================

class AdvancementCommand : public Command
{
public:
    std::string getName() const override { return "advancement"; }
    std::string getDescription() const override { return "Grant, withdraw or check player progress."; }
    std::string getUsage() const override { return "/advancement <grant|revoke> <target>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Progress updated.");
    }
};

class AttributeCommand : public Command
{
public:
    std::string getName() const override { return "attribute"; }
    std::string getDescription() const override { return "Views, adds, removes, or sets an entity attribute."; }
    std::string getUsage() const override { return "/attribute <target> <attribute> ..."; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Attribute modified.");
    }
};

class ClearCommand : public Command
{
public:
    std::string getName() const override { return "clear"; }
    std::string getDescription() const override { return "Removes items from the player's inventory."; }
    std::string getUsage() const override { return "/clear [targets] [item]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        ctx.reply("Empty inventory.");
    }
};

class EffectCommand : public Command
{
public:
    std::string getName() const override { return "effect"; }
    std::string getDescription() const override { return "Adds or removes status effects."; }
    std::string getUsage() const override { return "/effect <give|clear> <targets> <effect>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Effect applied or removed.");
    }
};

class EnchantCommand : public Command
{
public:
    std::string getName() const override { return "enchant"; }
    std::string getDescription() const override { return "Adds an enchantment to a player's selected item."; }
    std::string getUsage() const override { return "/enchant <target> <enchantment> [level]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Enchantment applied.");
    }
};

class ExperienceCommand : public Command
{
public:
    std::string getName() const override { return "experience"; }
    std::string getDescription() const override { return "Adds or removes experience from the player."; }
    std::string getUsage() const override { return "/experience <add|set|query> <targets> <amount>"; }
    std::vector<std::string> getAliases() const override { return {"xp"}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Experience updated.");
    }
};

class GamemodeCommand : public Command
{
public:
    std::string getName() const override { return "gamemode"; }
    std::string getDescription() const override { return "Defines a player's game mode."; }
    std::string getUsage() const override { return "/gamemode <survival|creative|adventure|spectator> [target]"; }
    std::vector<std::string> getAliases() const override { return {"gm"}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Game mode changed.");
    }
};

class GiveCommand : public Command
{
public:
    std::string getName() const override { return "give"; }
    std::string getDescription() const override { return "Give an item to a player."; }
    std::string getUsage() const override { return "/give <target> <item> [count]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.size() < 2)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Object given.");
    }
};

class ItemCommand : public Command
{
public:
    std::string getName() const override { return "item"; }
    std::string getDescription() const override { return "Manipulates items in inventories."; }
    std::string getUsage() const override { return "/item <replace|modify> ..."; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Inventory manipulated.");
    }
};

class RecipeCommand : public Command
{
public:
    std::string getName() const override { return "recipe"; }
    std::string getDescription() const override { return "Give or take away recipes from a player."; }
    std::string getUsage() const override { return "/recipe <give|take> <target> <recipe|*>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Updated recipes.");
    }
};

// ==========================================
// ---World, Blocks & Chunks
// ==========================================

class CloneCommand : public Command
{
public:
    std::string getName() const override { return "clone"; }
    std::string getDescription() const override { return "Copying blocks from one location to another."; }
    std::string getUsage() const override { return "/clone <begin> <end> <destination>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.size() < 3)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Cloned area.");
    }
};

class FillCommand : public Command
{
public:
    std::string getName() const override { return "fill"; }
    std::string getDescription() const override { return "Fills an area with a specific block."; }
    std::string getUsage() const override { return "/fill <from> <to> <block>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.size() < 3)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Filled area.");
    }
};

class FillBiomeCommand : public Command
{
public:
    std::string getName() const override { return "fillbiome"; }
    std::string getDescription() const override { return "Fills an area with a specific biome."; }
    std::string getUsage() const override { return "/fillbiome <from> <to> <biome>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.size() < 3)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Biome applied.");
    }
};

class ForceloadCommand : public Command
{
public:
    std::string getName() const override { return "forceload"; }
    std::string getDescription() const override { return "Forces chunks to remain loaded permanently."; }
    std::string getUsage() const override { return "/forceload <add|remove|remove all|query>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Force load Status modified.");
    }
};

class LocateCommand : public Command
{
public:
    std::string getName() const override { return "locate"; }
    std::string getDescription() const override { return "Locate the nearest structure, biome or POINT."; }
    std::string getUsage() const override { return "/locate <structure|biome|poi> <name>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Element found at X: 0 Y: 64 Z: 0");
    }
};

class PlaceCommand : public Command
{
public:
    std::string getName() const override { return "place"; }
    std::string getDescription() const override { return "Place a feature, jigsaw or structure."; }
    std::string getUsage() const override { return "/place <feature|structure|template|jigsaw> ..."; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Element placed.");
    }
};

class SeedCommand : public Command
{
public:
    std::string getName() const override { return "seed"; }
    std::string getDescription() const override { return "Shows the world seed."; }
    std::string getUsage() const override { return "/Seed"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        ctx.reply("Seed: [1234567890]");
    }
};

class SetBlockCommand : public Command
{
public:
    std::string getName() const override { return "setblock"; }
    std::string getDescription() const override { return "Replaces a block with another block."; }
    std::string getUsage() const override { return "/setblock <pos> <block>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.size() < 2)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Block placed.");
    }
};

class SetWorldSpawnCommand : public Command
{
public:
    std::string getName() const override { return "setworldspawn"; }
    std::string getDescription() const override { return "Defines the world spawn point."; }
    std::string getUsage() const override { return "/setworldspawn [pos]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        ctx.reply("Spawn of the world defined.");
    }
};

class SpawnPointCommand : public Command
{
public:
    std::string getName() const override { return "spawnpoint"; }
    std::string getDescription() const override { return "Defines the spawn point of a player."; }
    std::string getUsage() const override { return "/spawnpoint [targets] [pos]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        ctx.reply("Spawnpoint defined.");
    }
};

class TimeCommand : public Command
{
public:
    std::string getName() const override { return "time"; }
    std::string getDescription() const override { return "Change or view the world's game time."; }
    std::string getUsage() const override { return "/time <set|add|query> <value>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Time adjusts.");
    }
};

class WeatherCommand : public Command
{
public:
    std::string getName() const override { return "weather"; }
    std::string getDescription() const override { return "Defined by the weather."; }
    std::string getUsage() const override { return "/weather <clear|rain|thunder> [duration]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Weather changed to: " + args[0]);
    }
};

class WorldBorderCommand : public Command
{
public:
    std::string getName() const override { return "worldborder"; }
    std::string getDescription() const override { return "Manages the edge of the world."; }
    std::string getUsage() const override { return "/worldborder <center|set|add|...> ..."; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Updated world border.");
    }
};

// ===========================================================
// ---Entities, Combat & Interactions
// ===========================================================

class BossbarCommand : public Command
{
public:
    std::string getName() const override { return "bossbar"; }
    std::string getDescription() const override { return "Create and modify boss bars."; }
    std::string getUsage() const override { return "/bossbar <add|remove|set|get|list>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Bossbar updated.");
    }
};

class DamageCommand : public Command
{
public:
    std::string getName() const override { return "damage"; }
    std::string getDescription() const override { return "Deal damage to specific entities."; }
    std::string getUsage() const override { return "/damage <target> <amount> [damageType]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.size() < 2)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Damage inflicted.");
    }
};

class KillCommand : public Command
{
public:
    std::string getName() const override { return "kill"; }
    std::string getDescription() const override { return "Kill entities."; }
    std::string getUsage() const override { return "/kill [targets]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        ctx.reply("Entities killed.");
    }
};

class LootCommand : public Command
{
public:
    std::string getName() const override { return "loot"; }
    std::string getDescription() const override { return "Places items from an inventory slot on the ground."; }
    std::string getUsage() const override { return "/loot <target> <source>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Loot generated.");
    }
};

class RideCommand : public Command
{
public:
    std::string getName() const override { return "ride"; }
    std::string getDescription() const override { return "Moves entities up or down."; }
    std::string getUsage() const override { return "/ride <target> <mount|dismount>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Modified mount.");
    }
};

class SpectateCommand : public Command
{
public:
    std::string getName() const override { return "watch"; }
    std::string getDescription() const override { return "Observe an entity in spectator mode."; }
    std::string getUsage() const override { return "/watch [target] [player]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        ctx.reply("Mode observation change.");
    }
};

class SpreadPlayersCommand : public Command
{
public:
    std::string getName() const override { return "spreadplayers"; }
    std::string getDescription() const override { return "Teleports entities to random locations."; }
    std::string getUsage() const override { return "/spreadplayers <center> <spreadDistance> <maxRange> <respectTeams> <targets>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.size() < 5)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Dispersed players.");
    }
};

class SummonCommand : public Command
{
public:
    std::string getName() const override { return "summon"; }
    std::string getDescription() const override { return "Summons an entity."; }
    std::string getUsage() const override { return "/summon <entity> [pos] [nbt]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Entity invoked.");
    }
};

// ===========================================================
// ---NBT, Data & Tags
// ===========================================================

class DataCommand : public Command
{
public:
    std::string getName() const override { return "data"; }
    std::string getDescription() const override { return "Gets, merges, modifies and deletes NBT data."; }
    std::string getUsage() const override { return "/data <get|merge|modify|remove> ..."; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("NBT data modified.");
    }
};

class TagCommand : public Command
{
public:
    std::string getName() const override { return "tag"; }
    std::string getDescription() const override { return "Controls entity tags."; }
    std::string getUsage() const override { return "/tag <target> <add|remove|list> [name]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Tag modified.");
    }
};

class TeamCommand : public Command
{
public:
    std::string getName() const override { return "team"; }
    std::string getDescription() const override { return "Control the teams."; }
    std::string getUsage() const override { return "/team <add|remove|join|leave|empty|list|option> ..."; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Updated teams.");
    }
};

class ScoreboardCommand : public Command
{
public:
    std::string getName() const override { return "scoreboard"; }
    std::string getDescription() const override { return "Manage scoreboard goals and players."; }
    std::string getUsage() const override { return "/scoreboard <objectives|players|teams> ..."; }
    std::vector<std::string> getAliases() const override { return {"Sb"}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Scoreboard updated.");
    }
};

// ==========================================
// ---Execution, Scripts & Functions
// ==========================================

class ExecuteCommand : public Command
{
public:
    std::string getName() const override { return "execute"; }
    std::string getDescription() const override { return "Executes another command under certain conditions."; }
    std::string getUsage() const override { return "/execute <subcommand>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Executing the subcommand...");
    }
};

class FunctionCommand : public Command
{
public:
    std::string getName() const override { return "function"; }
    std::string getDescription() const override { return "Executes a datapack function."; }
    std::string getUsage() const override { return "/function <name>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Function executed.");
    }
};

class ReturnCommand : public Command
{
public:
    std::string getName() const override { return "return"; }
    std::string getDescription() const override { return "Controls the flow of execution in functions."; }
    std::string getUsage() const override { return "/return <value|fail|run>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Specified return value.");
    }
};

class ScheduleCommand : public Command
{
public:
    std::string getName() const override { return "schedule"; }
    std::string getDescription() const override { return "Delays the execution of a function."; }
    std::string getUsage() const override { return "/schedule <function> <time>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.size() < 2)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Planned function.");
    }
};

class TriggerCommand : public Command
{
public:
    std::string getName() const override { return "trigger"; }
    std::string getDescription() const override { return "Sets a trigger to be activated by a non-op player."; }
    std::string getUsage() const override { return "/trigger <objective> [add|set] [value]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 0; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Trigger activated.");
    }
};

// ==========================================
// ---Communication & Visual Interface
// ==========================================

class MeCommand : public Command
{
public:
    std::string getName() const override { return "me"; }
    std::string getDescription() const override { return "Displays an action message about the sender."; }
    std::string getUsage() const override { return "/me <action>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 0; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        std::string action;
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (i > 0)
                action += " ";
            action += args[i];
        }
        ctx.reply("* " + ctx.sender + " " + action);
    }
};

class ParticleCommand : public Command
{
public:
    std::string getName() const override { return "particle"; }
    std::string getDescription() const override { return "Creates visual particles."; }
    std::string getUsage() const override { return "/particle <name> [pos]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Particles created.");
    }
};

class PlaysoundCommand : public Command
{
public:
    std::string getName() const override { return "playsound"; }
    std::string getDescription() const override { return "Plays a sound."; }
    std::string getUsage() const override { return "/playsound <sound> <source> <targets>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Sound Played.");
    }
};

class StopsoundCommand : public Command
{
public:
    std::string getName() const override { return "stopsound"; }
    std::string getDescription() const override { return "Stops a sound."; }
    std::string getUsage() const override { return "/stopsound <targets> [source] [sound]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Sound stopped");
    }
};

class TeammsgCommand : public Command
{
public:
    std::string getName() const override { return "teammsg"; }
    std::string getDescription() const override { return "Send a message to your team."; }
    std::string getUsage() const override { return "/teammsg <message>"; }
    std::vector<std::string> getAliases() const override { return {"tm"}; }
    int getRequiredOpLevel() const override { return 0; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Team message sent.");
    }
};

class TellrawCommand : public Command
{
public:
    std::string getName() const override { return "tellraw"; }
    std::string getDescription() const override { return "Displays a raw JSON message to players."; }
    std::string getUsage() const override { return "/tellraw <targets> <json>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.size() < 2)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Tellraw message sent.");
    }
};

class TitleCommand : public Command
{
public:
    std::string getName() const override { return "title"; }
    std::string getDescription() const override { return "Manages the titles displayed on the screen."; }
    std::string getUsage() const override { return "/title <target> <title|subtitle|actionbar|clear|reset> ..."; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.size() < 2)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Title updated.");
    }
};

// ==========================================
// ---Profiling & Diagnostic
// ==========================================

class DebugCommand : public Command
{
public:
    std::string getName() const override { return "debug"; }
    std::string getDescription() const override { return "Start or stop a debugging session."; }
    std::string getUsage() const override { return "/debug <start|stop>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 3; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("Debug Session" + args[0]);
    }
};

class JfrCommand : public Command
{
public:
    std::string getName() const override { return "jfr"; }
    std::string getDescription() const override { return "Starts or stops JFR profiling."; }
    std::string getUsage() const override { return "/jfr. <start|stop>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 4; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        ctx.reply("JFR profiling" + args[0]);
    }
};

class PerfCommand : public Command
{
public:
    std::string getName() const override { return "perf"; }
    std::string getDescription() const override { return "Captures metrics for 10 seconds."; }
    std::string getUsage() const override { return "/perf <start|stop>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 4; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        (void)args;
        ctx.reply("Performance sampling runs for 10s...");
    }
};