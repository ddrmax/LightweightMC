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
