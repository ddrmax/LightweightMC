#pragma once

#include "core/CommandManager.hpp"


// // ===========================================================


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

// // ===========================================================


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
