#pragma once

#include "core/CommandManager.hpp"


// // ==========================================


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
