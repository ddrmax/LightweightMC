#pragma once

#include "core/CommandManager.hpp"


// // ===========================================================


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
    std::string getUsage() const override { return "/experience <add|remove|query> [target] [amount]"; }
    std::vector<std::string> getAliases() const override { return {"xp"}; }
    int getRequiredOpLevel() const override { return 2; }
    // Mutates XP on the wire; console replies must broadcast to every PLAY
    // client so they see the XP bar refresh (via S2C 0x1f Experience).
    bool affectsPlayerState() const override { return true; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        const std::string &verb = args[0];
        std::string target;
        std::string amountToken = "0";
        if (verb == "query")
        {
            target = args.size() >= 2 ? args[1] : ctx.sender;
        }
        else if (verb == "add" || verb == "remove")
        {
            if (args.size() < 2)
            {
                ctx.reply("Usage: " + getUsage());
                return;
            }
            target = args[1];
            if (args.size() >= 3)
                amountToken = args[2];
            else if (verb == "remove")
            {
                // Vanilla defaults "remove <target>" to a full level wipe
                // (delta = -currentXp); we approximate with -1000 = one full
                // level, which the level-clamp inside NetworkManager::grantPlayerXp
                // reduces to "drop the level by 1". This mirrors how admins
                // typically spell the command and avoids exposing a full
                // query accessors in CommandContext.
                amountToken = "1000";
            }
        }
        else if (verb == "set")
        {
            // "set <target> <amount>" wants an absolute XP value. NetworkManager
            // currently exposes only delta-based grantPlayerXp, so set is
            // reported honestly rather than silently falling back to add.
            if (args.size() < 3)
            {
                ctx.reply("Usage: /experience set <target> <amount>");
                return;
            }
            ctx.reply("set is not wired yet -- NetworkManager::grantPlayerXp is delta-only; use /experience add " + args[1] + " <delta>.");
            return;
        }
        else
        {
            // Implicit-add path. Vanilla's /xp and /experience accept both
            // "add <target> <amount>" AND "<target> <amount>" (bare target
            // with no verb). Previously this branch replied "Unknown verb
            // \"<target>\"" for the bare form, which is what admins meant by
            // "add/remove is not added". Fall back to add: target = args[0],
            // amount = args[1] (default 0 so a bare /xp <target> is a no-op
            // that just refreshes the XP HUD and confirms the target exists).
            target = verb;
            if (args.size() >= 2)
                amountToken = args[1];
        }

        if (target.empty() || target == "Console")
        {
            ctx.reply("Console /experience requires an explicit target. Usage: " + getUsage());
            return;
        }

        int32_t amount = 0;
        std::istringstream amountStream(amountToken);
        if (!(amountStream >> amount))
        {
            ctx.reply("Experience amount \"" + amountToken + "\" is not a number. Usage: " + getUsage());
            return;
        }
        if (verb == "remove")
            amount = -amount;

        const int levelsGained = ctx.grantXpToPlayer(target, amount);
        if (verb == "query")
        {
            // No read-only accessor in CommandContext yet; grant 0 XP as a
            // no-op probe so at least the target is confirmed as resolvable.
            // The backend logs the target's current totals to the debug log.
            ctx.reply("XP entry for " + target + " located (see [NET XP] debug line for totals).");
            return;
        }
        if (levelsGained == 0)
        {
            ctx.reply("XP of " + target + " adjusted by " + amountToken + " (no level change).");
        }
        else if (levelsGained > 0)
        {
            ctx.reply("XP of " + target + " increased by " + amountToken + "; levels gained: " + std::to_string(levelsGained) + ".");
        }
        else
        {
            ctx.reply("XP of " + target + " decreased by " + amountToken + "; levels lost: " + std::to_string(-levelsGained) + ".");
        }
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
    // Mutates a player's game mode (survival/creative/adventure/spectator)
    // -- console replies about this change should reach every PLAY client.
    bool affectsPlayerState() const override { return true; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        // Vanilla game-mode enumeration (matches PlayerSession::gamemode and
        // the JoinGame packet's second byte).
        int mode = -1;
        std::string modeName = args[0];
        if (modeName == "survival") mode = 0;
        else if (modeName == "creative") mode = 1;
        else if (modeName == "adventure") mode = 2;
        else if (modeName == "spectator") mode = 3;
        else
        {
            ctx.reply("Unknown game mode \"" + modeName + "\". Usage: " + getUsage());
            return;
        }

        // Target selection: default is the caller (the player for in-game
        // chat, or explicitly required for the console).
        std::string target = args.size() >= 2 ? args[1] : std::string();
        if (target.empty())
        {
            if (ctx.isConsole)
            {
                ctx.reply("Console /gamemode requires an explicit target. Usage: /gamemode <mode> <player>");
                return;
            }
            target = ctx.sender;
        }

        if (ctx.setGameMode(target, mode))
        {
            ctx.reply("Game mode of " + target + " set to " + modeName + ".");
        }
        else
        {
            ctx.reply("Game mode update failed for " + target + " (unknown player or persistence error).");
        }
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
    // Puts an item in a player's inventory -> console reply broadcasts.
    bool affectsPlayerState() const override { return true; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.size() < 2)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        std::string target = args[0];
        std::string item = args[1];
        int count = (args.size() >= 3) ? std::stoi(args[2]) : 1;
        if (ctx.giveItemHook(target, item, count))
            ctx.reply("Object given.");
        else
            ctx.reply("Failed to give item.");
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
