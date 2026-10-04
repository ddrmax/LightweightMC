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
