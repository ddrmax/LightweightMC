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


// ---Command /gotor ---
// Multiworld dimension-plane switch. Pre-1.12 vanilla Minecraft's Overworld/
// Nether/End occupy IDs 0/-1/1; the Aether special dimension is registered at
// id 100 and custom planes created via /world create get IDs 101+.
// /gotor <worldName> teleports the caller (or an explicitly named player) to
// the surface of that plane's WorldStorage, clears loadedChunks so the client
// re-streams the new plane's terrain, and pushes an S2C Position packet so the
// client animates the jump. Delegates to NetworkManager::setPlayerWorldId via
// the ctx.switchPlayerWorld hook wired in src/core/Server.cpp.
class GotorCommand : public Command
{
public:
    std::string getName() const override { return "gotor"; }
    std::string getDescription() const override
    {
        return "Switches a player to another registered dimension plane.";
    }
    std::string getUsage() const override
    {
        return "/gotor <worldName> OR /gotor <player> <worldName>";
    }
    std::vector<std::string> getAliases() const override { return {"gowe", "gworld"}; }
    int getRequiredOpLevel() const override { return 2; }
    bool affectsPlayerState() const override { return true; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        std::string player;
        std::string worldName;
        if (args.size() == 1)
        {
            // /gotor <worldName>: move the caller (chat context only -- the
            // console has no world position of its own).
            if (ctx.isConsole)
            {
                ctx.reply("The console cannot travel without an explicit target player.");
                return;
            }
            player = ctx.sender;
            worldName = args[0];
        }
        else // args.size() >= 2
        {
            player = args[0];
            worldName = args[1];
        }

        if (ctx.switchPlayerWorld(player, worldName))
            ctx.reply("Travelled to plane: " + worldName);
        else
            ctx.reply("Cannot travel to '" + worldName +
                      "' -- plane is not registered or player is not online.");
    }
};


// ---Command /world ---
// Subcommand tree for multiworld management:
//   /world list                      -> enumerate registered plane names
//   /world create <name> <style>     -> allocate a new plane (id >= 101)
//   /world info <name>               -> report the id + style of a registered plane
// Wire-through CommandContext.createWorld / listWorlds closures in Server.cpp.
class WorldCommand : public Command
{
public:
    std::string getName() const override { return "world"; }
    std::string getDescription() const
    {
        return "Manages registered dimension planes (list | create | info).";
    }
    std::string getUsage() const override
    {
        return "/world <list|create <name> <style>|info <name>>";
    }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 3; }
    bool affectsPlayerState() const override { return false; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        const std::string &sub = args[0];
        if (sub == "list")
        {
            const std::vector<std::string> names = ctx.listWorlds();
            if (names.empty())
            {
                ctx.reply("No planes registered (server not yet started).");
                return;
            }
            std::string joined;
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                if (i != 0) joined += ", ";
                joined += names[i];
            }
            ctx.reply("Registered planes: " + joined);
        }
        else if (sub == "create")
        {
            if (args.size() < 3)
            {
                ctx.reply("Usage: /world create <name> <style> (style = overworld|superflat|test-grid|nether|aether)");
                return;
            }
            const int32_t id = ctx.createWorld(args[1], args[2]);
            if (id < 0)
                ctx.reply("Could not create plane '" + args[1] + "'.");
            else
                ctx.reply("Created plane '" + args[1] + "' as id=" + std::to_string(id) + " style=" + args[2]);
        }
        else if (sub == "info")
        {
            if (args.size() < 2)
            {
                ctx.reply("Usage: /world info <name>");
                return;
            }
            int32_t id = 0;
            std::string style;
            std::string dbPath;
            if (ctx.describeWorld(args[1], id, style, dbPath))
            {
                ctx.reply("Plane '" + args[1] + "' -> id=" + std::to_string(id) +
                          " style=" + style + " db=" + dbPath);
            }
            else
            {
                ctx.reply("Plane '" + args[1] + "' is not registered. Use /world list.");
            }
        }
        else
        {
            ctx.reply("Unknown subcommand. Use: " + getUsage());
        }
    }
};
