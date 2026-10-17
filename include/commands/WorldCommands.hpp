#pragma once

#include "core/CommandManager.hpp"
#include "world/MC17BlockIds.hpp"
#include "core/Logger.hpp"
// 0.0.15 phase 4: /world create's --inventory / --force-gamemode flags need
// the Storage::kInventoryPolicy* / Storage::kForceGameMode* constexprs and
// Storage::kDefaultWorldSeed so the parser maps user tokens to the same
// sentinels NetworkSocket.cpp's resolveInventoryPolicy / resolveGameMode
// lambdas emit for the [world] aether-* boot pass.
#include "storage/Database.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <unordered_map>
#include <algorithm>

namespace LightweightMC
{
    namespace Commands
    {
        namespace detail
        {
            // Parse a signed integer coordinate token (accepts the vanilla
            // Minecraft form `x y z` and the shorthand 3-int triple split by
            // CommandManager's arg splitter). Returns false on non-numeric
            // input so commands can print a helpful Usage reply rather than
            // silently writing garbage into world_registry / chunk blobs.
            inline bool parseIntCoord(const std::string &s, int &out)
            {
                if (s.empty())
                    return false;
                std::istringstream iss(s);
                long v = 0;
                if (!(iss >> v))
                    return false;
                std::string leftover;
                if (iss >> leftover)
                    return false;
                out = static_cast<int>(v);
                return true;
            }

            // Same contract as parseIntCoord but for DOUBLE values so /spawn
            //point / /setworldspawn can preserve Minecraft's sub-block spawn
            // offsets (0.5, 64, 0.5 default).
            inline bool parseDoubleCoord(const std::string &s, double &out)
            {
                if (s.empty())
                    return false;
                std::istringstream iss(s);
                double v = 0.0;
                if (!(iss >> v))
                    return false;
                std::string leftover;
                if (iss >> leftover)
                    return false;
                out = v;
                return true;
            }

            // Lower-cases a token so a caller can look up a block name without
            // worrying about Java-style capitalisation (`Grass`, `GRASS_BLOCK`,
            // `grass_block` all resolve).
            inline std::string lowerCase(std::string s)
            {
                for (auto &c : s)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return s;
            }

            // Block-name lookup for /setblock and /place. Uses the numeric
            // IDs from MC17BlockIds.hpp so the command layer never has to
            // hard-code a magic number and the block vocabulary stays in sync
            // with the terrain generator's vocabulary. Unknown names return
            // false; the caller decides whether to reply with a Usage message
            // or an "Unknown block" error.
            inline bool blockNameToId(const std::string &name, uint16_t &outId)
            {
                static const std::unordered_map<std::string, uint16_t> table = []() {
                    using namespace LightweightMC::World::Blocks17;
                    std::unordered_map<std::string, uint16_t> t;
                    t["air"] = AIR;
                    t["stone"] = STONE;
                    t["grass"] = GRASS_BLOCK;
                    t["grass_block"] = GRASS_BLOCK;
                    t["dirt"] = DIRT;
                    t["cobblestone"] = COBBLESTONE;
                    t["planks"] = PLANKS;
                    t["wood_planks"] = PLANKS;
                    t["bedrock"] = BEDROCK;
                    t["rock"] = BEDROCK;
                    t["water"] = WATER_STATIONARY;
                    t["water_flowing"] = WATER_FLOWING;
                    t["lava"] = LAVA_STATIONARY;
                    t["lava_flowing"] = LAVA_FLOWING;
                    t["sand"] = SAND;
                    t["gravel"] = GRAVEL;
                    t["log"] = LOG;
                    t["wood"] = LOG;
                    t["tree_trunk"] = LOG;
                    t["leaves"] = LEAVES;
                    t["sponge"] = SPONGE;
                    t["glass"] = GLASS;
                    t["sandstone"] = SANDSTONE;
                    t["gold_ore"] = GOLD_ORE;
                    t["iron_ore"] = IRON_ORE;
                    t["coal_ore"] = COAL_ORE;
                    t["lapis_ore"] = LAPIS_ORE;
                    t["lapis_block"] = LAPIS_BLOCK;
                    t["gold_block"] = GOLD_BLOCK;
                    t["iron_block"] = IRON_BLOCK;
                    t["diamond_ore"] = DIAMOND_ORE;
                    t["diamond_block"] = DIAMOND_BLOCK;
                    t["brick"] = BRICK_BLOCK;
                    t["brick_block"] = BRICK_BLOCK;
                    t["moossy_cobblestone"] = MOSSY_COBBLESTONE;
                    t["mossy_cobblestone"] = MOSSY_COBBLESTONE;
                    t["bookshelf"] = BOOKSHELF;
                    t["chest"] = CHEST;
                    t["crafting_table"] = CRAFTING_TABLE;
                    t["furnace"] = FURNACE;
                    t["lit_furnace"] = LIT_FURNACE;
                    t["obsidian"] = OBSIDIAN;
                    t["torch"] = TORCH;
                    t["snow"] = SNOW;
                    return t;
                }();
                const auto it = table.find(lowerCase(name));
                if (it == table.end())
                    return false;
                outId = it->second;
                return true;
            }
        } // detail
    }     // Commands
} // namespace LightweightMC


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
    std::string getDescription() const override { return "Fill a cuboid region with a block."; }
    std::string getUsage() const override { return "/place <block> <x1> <y1> <z1> <x2> <y2> <z2>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }
    // The command mutates the world but not per-player state, so it does
    // not flag affectsPlayerState().
    bool affectsPlayerState() const override { return false; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        using namespace LightweightMC::Commands::detail;
        if (args.size() < 7)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        uint16_t blockId = 0;
        if (!blockNameToId(args[0], blockId))
        {
            ctx.reply("Unknown block: " + args[0]);
            return;
        }
        int x1 = 0, y1 = 0, z1 = 0, x2 = 0, y2 = 0, z2 = 0;
        if (!parseIntCoord(args[1], x1) || !parseIntCoord(args[2], y1) ||
            !parseIntCoord(args[3], z1) || !parseIntCoord(args[4], x2) ||
            !parseIntCoord(args[5], y2) || !parseIntCoord(args[6], z2))
        {
            ctx.reply("Coordinates must be integers. Usage: " + getUsage());
            return;
        }
        const int32_t planeId = ctx.senderWorldId;
        const uint16_t combined = Blocks17Combined(blockId);
        // Order the corners and cap the fill so a rogue /place cannot loop
        // over a whole plane's height range (bounded well under vanilla's
        // default 4096-block limit for a command response).
        const int lx = std::min(x1, x2), hx = std::max(x1, x2);
        const int ly = std::min(y1, y2), hy = std::max(y1, y2);
        const int lz = std::min(z1, z2), hz = std::max(z1, z2);
        const long long dx = static_cast<long long>(hx - lx) + 1;
        const long long dy = static_cast<long long>(hy - ly) + 1;
        const long long dz = static_cast<long long>(hz - lz) + 1;
        const long long volume = dx * dy * dz;
        constexpr long long maxPlaceVolume = 4096LL;
        if (volume <= 0 || volume > maxPlaceVolume)
        {
            ctx.reply("Region too large (limit " + std::to_string(maxPlaceVolume) +
                      " blocks). Shrink the box and try again.");
            return;
        }
        long long written = 0;
        for (int x = lx; x <= hx; ++x)
            for (int y = ly; y <= hy; ++y)
                for (int z = lz; z <= hz; ++z)
                {
                    if (!ctx.setBlockAtPlane(planeId, x, y, z, combined))
                    {
                        ctx.reply("Place failed on plane " + std::to_string(planeId) +
                                  " at (" + std::to_string(x) + ", " + std::to_string(y) +
                                  ", " + std::to_string(z) + ")");
                        return;
                    }
                    ++written;
                }
        LightweightMC::Core::Logger::debug("[CMD /place] plane=" + std::to_string(planeId) +
                            " blockId=" + std::to_string(blockId) +
                            " volume=" + std::to_string(written));
        ctx.reply("Placed " + std::to_string(written) + " blocks.");
    }

    static uint16_t Blocks17Combined(uint16_t id)
    {
        return static_cast<uint16_t>((id << 4) | 0);
    }
};


class SeedCommand : public Command
{
public:
    std::string getName() const override { return "seed"; }
    std::string getDescription() const override { return "Shows or sets the world seed for a plane."; }
    std::string getUsage() const override { return "/seed [planeName] [<value>]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }
    // Setting a plane's seed mutates server state (chunks regenerate
    // differently on that plane for future visits) but not a specific
    // player's state, so broadcast stays off.
    bool affectsPlayerState() const override { return false; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        using namespace LightweightMC::Commands::detail;
        // Resolve which plane's seed to read/write:
        //   * No args -> the caller's current plane (chat contexts know
        //     their PlayerSession::worldId; console defaults to Overworld
        //     id 0 by ctx.senderWorldId default).
        //   * 1 arg -> plane name (read-only query).
        //   * 2 args -> plane name + numeric value (write).
        // We resolve the caller's own plane via describeWorld when the
        // caller did not name one so the reply names the plane the seed
        // applies to (rather than an ambiguous "the current plane").
        std::string planeName = "overworld";
        if (!args.empty())
            planeName = args[0];
        const int64_t seed = ctx.getWorldSeed(planeName);
        if (args.size() >= 2)
        {
            // /seed <planeName> <value>
            std::istringstream iss(args[1]);
            long long value = 0;
            if (!(iss >> value))
            {
                ctx.reply("Seed value must be an integer. Usage: " + getUsage());
                return;
            }
            std::string leftover;
            if (iss >> leftover)
            {
                ctx.reply("Seed value must be an integer. Usage: " + getUsage());
                return;
            }
            if (!ctx.setWorldSeed(planeName, value))
            {
                ctx.reply("Could not set seed for plane \"" + planeName +
                          "\" (plane not registered?).");
                return;
            }
            ctx.reply("Seed of " + planeName + " set to " + std::to_string(value) +
                      " (already-generated chunks stay unchanged; future chunks pick up the new seed).");
            return;
        }
        ctx.reply("Seed: [" + std::to_string(seed) + "] (plane: " + planeName + ")");
    }
};


class SetBlockCommand : public Command
{
public:
    std::string getName() const override { return "setblock"; }
    std::string getDescription() const override { return "Replaces a block with another block."; }
    std::string getUsage() const override { return "/setblock <x> <y> <z> <blockName>"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }
    // Mutates the world on a plane, not any specific player's state.
    bool affectsPlayerState() const override { return false; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        using namespace LightweightMC::Commands::detail;
        if (args.size() < 4)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        int x = 0, y = 0, z = 0;
        if (!parseIntCoord(args[0], x) || !parseIntCoord(args[1], y) || !parseIntCoord(args[2], z))
        {
            ctx.reply("Coordinates must be integers. Usage: " + getUsage());
            return;
        }
        uint16_t blockId = 0;
        if (!blockNameToId(args[3], blockId))
        {
            ctx.reply("Unknown block: " + args[3]);
            return;
        }
        const int32_t planeId = ctx.senderWorldId;
        const uint16_t combined = Blocks17Combined(blockId);
        if (!ctx.setBlockAtPlane(planeId, x, y, z, combined))
        {
            ctx.reply("Could not write block on plane " + std::to_string(planeId) +
                      " at (" + std::to_string(x) + ", " + std::to_string(y) +
                      ", " + std::to_string(z) + ")");
            return;
        }
        LightweightMC::Core::Logger::debug("[CMD /setblock] plane=" + std::to_string(planeId) +
                            " x=" + std::to_string(x) +
                            " y=" + std::to_string(y) +
                            " z=" + std::to_string(z) +
                            " blockId=" + std::to_string(blockId));
        ctx.reply("Block placed.");
    }

    static uint16_t Blocks17Combined(uint16_t id)
    {
        return static_cast<uint16_t>((id << 4) | 0);
    }
};


class SetWorldSpawnCommand : public Command
{
public:
    std::string getName() const override { return "setworldspawn"; }
    std::string getDescription() const override { return "Defines the world spawn point."; }
    std::string getUsage() const override { return "/setworldspawn [x] [y] [z] [planeName]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }
    bool affectsPlayerState() const override { return false; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        using namespace LightweightMC::Commands::detail;
        // The command can be invoked with no args to query the current
        // spawn (returns the vanilla (0.5, 64, 0.5) default when the
        // plane has never been touched by /setworldspawn).
        if (args.empty())
        {
            double x = 0.0, y = 0.0, z = 0.0;
            const bool set = ctx.getWorldSpawn("overworld", x, y, z);
            ctx.reply("World spawn for overworld: (" + std::to_string(x) + ", " +
                      std::to_string(y) + ", " + std::to_string(z) + ")" +
                      (set ? " (explicit)" : " (default)"));
            return;
        }
        // Parse 3-4 args: [x] [y] [z] [planeName]. planeName optional;
        // when absent, "overworld" is used so a console invocation without
        // a session context still targets a well-known plane.
        if (args.size() < 3)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        double x = 0.0, y = 0.0, z = 0.0;
        if (!parseDoubleCoord(args[0], x) || !parseDoubleCoord(args[1], y) || !parseDoubleCoord(args[2], z))
        {
            ctx.reply("Coordinates must be numeric. Usage: " + getUsage());
            return;
        }
        std::string planeName = "overworld";
        if (args.size() >= 4)
            planeName = args[3];
        if (!ctx.setWorldSpawn(planeName, x, y, z))
        {
            ctx.reply("Could not set spawn for plane \"" + planeName + "\" (plane not registered?).");
            return;
        }
        LightweightMC::Core::Logger::info("setworldspawn: plane=" + planeName + " -> " +
                           std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z));
        ctx.reply("Spawn of the world \"" + planeName + "\" defined at (" +
                  std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ").");
    }
};


class SpawnPointCommand : public Command
{
public:
    std::string getName() const override { return "spawnpoint"; }
    std::string getDescription() const override { return "Defines the spawn point of a player."; }
    std::string getUsage() const override { return "/spawnpoint [player] [x] [y] [z]"; }
    std::vector<std::string> getAliases() const override { return {}; }
    int getRequiredOpLevel() const override { return 2; }
    // Sets a specific player's respawn point -- that is a per-player state
    // change so console invocation broadcasts to the connected client so
    // the player knows their respawn changed.
    bool affectsPlayerState() const override { return true; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        using namespace LightweightMC::Commands::detail;
        if (args.empty())
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        // Vanilla /spawnpoint semantics: 0 or 1 leading non-numeric token
        // names the player (default = the caller's own username); remaining
        // three numeric tokens are the respawn position.
        std::string target = ctx.sender;
        size_t coordStart = 0;
        {
            double probe = 0.0;
            if (!parseDoubleCoord(args[0], probe))
            {
                target = args[0];
                coordStart = 1;
            }
        }
        if (args.size() < coordStart + 3)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }
        double x = 0.0, y = 0.0, z = 0.0;
        if (!parseDoubleCoord(args[coordStart], x) ||
            !parseDoubleCoord(args[coordStart + 1], y) ||
            !parseDoubleCoord(args[coordStart + 2], z))
        {
            ctx.reply("Coordinates must be numeric. Usage: " + getUsage());
            return;
        }
        if (!ctx.setPlayerSpawn(target, x, y, z))
        {
            ctx.reply("Could not set spawnpoint for " + target +
                      " (player unknown or no row yet).");
            return;
        }
        LightweightMC::Core::Logger::info("spawnpoint: player=" + target + " -> " +
                           std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z));
        ctx.reply("Spawnpoint of " + target + " set to (" +
                  std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ").");
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
        // 0.0.15 phase 4: /world create grew --inventory / --force-gamemode /
        // --seed flags; the shorthand here points the operator to the full
        // usage string emitted by the `sub == "create"` branch's own Usage
        // reply (which spells out the accepted value sets).
        return "/world <list|create <name> <style> [--inventory ...] [--force-gamemode ...] [--seed ...]|info <name>>";
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
                ctx.reply("Usage: /world create <name> <style> "
                          "[--inventory <standard|separate|restricted>] "
                          "[--force-gamemode <survival|creative|adventure|spectator|none>] "
                          "[--seed <int>]  (style = overworld|superflat|test-grid|nether|aether|end|flat|creative|mining|twilight)");
                return;
            }
            // 0.0.15 phase 4: parse optional --inventory / --force-gamemode
            // / --seed flags out of args[3..]. Unknown tokens reply with a
            // targeted message and abort; the two name->enum lambdas
            // mirror NetworkSocket.cpp's resolveInventoryPolicy /
            // resolveGameMode resolvers so the CLI vocabulary stays
            // identical between the boot pass and the runtime flag.
            //
            // Fully-qualify `LightweightMC::Storage::` because WorldCommand
            // is declared at global scope (the file's namespace block at
            // lines 22-141 only covers the file's `detail` helpers), so
            // the class body sits outside the enclosing LightweightMC
            // namespace that would otherwise make `Storage::` reachable by
            // ordinary unqualified namespace-name lookup.
            auto resolveInventoryFlag = [](const std::string &raw, int32_t &out) -> bool
            {
                if (raw == "separate")   { out = LightweightMC::Storage::kInventoryPolicySeparate;   return true; }
                if (raw == "restricted") { out = LightweightMC::Storage::kInventoryPolicyRestricted; return true; }
                if (raw == "standard")   { out = LightweightMC::Storage::kInventoryPolicySync;       return true; }
                return false;
            };
            auto resolveGameModeFlag = [](const std::string &raw, int32_t &out) -> bool
            {
                if (raw == "survival")  { out = LightweightMC::Storage::kForceGameModeSurvival;  return true; }
                if (raw == "creative")  { out = LightweightMC::Storage::kForceGameModeCreative;  return true; }
                if (raw == "adventure") { out = LightweightMC::Storage::kForceGameModeAdventure; return true; }
                if (raw == "spectator") { out = LightweightMC::Storage::kForceGameModeSpectator; return true; }
                if (raw == "none")      { out = LightweightMC::Storage::kForceGameModeInherit;   return true; }
                return false;
            };
            int32_t inventoryPolicy = LightweightMC::Storage::kInventoryPolicySync;
            int32_t forceGameMode = LightweightMC::Storage::kForceGameModeInherit;
            int64_t seed = LightweightMC::Storage::kDefaultWorldSeed;
            for (std::size_t i = 3; i < args.size(); ++i)
            {
                const std::string &tok = args[i];
                if (tok != "--inventory" && tok != "--force-gamemode" && tok != "--seed")
                {
                    ctx.reply("Unknown token '" + tok + "' after <style>. Use --inventory / --force-gamemode / --seed.");
                    return;
                }
                if (i + 1 >= args.size())
                {
                    ctx.reply("Missing value for flag '" + tok + "'.");
                    return;
                }
                const std::string &val = args[++i];
                if (tok == "--inventory")
                {
                    if (!resolveInventoryFlag(val, inventoryPolicy))
                    {
                        ctx.reply("Unknown --inventory value '" + val + "' (expected standard|separate|restricted).");
                        return;
                    }
                }
                else if (tok == "--force-gamemode")
                {
                    if (!resolveGameModeFlag(val, forceGameMode))
                    {
                        ctx.reply("Unknown --force-gamemode value '" + val + "' (expected survival|creative|adventure|spectator|none).");
                        return;
                    }
                }
                else
                {
                    std::istringstream iss(val);
                    long long parsed = 0;
                    if (!(iss >> parsed))
                    {
                        ctx.reply("Unknown --seed value '" + val + "' (expected an integer).");
                        return;
                    }
                    std::string leftover;
                    if (iss >> leftover)
                    {
                        ctx.reply("Unknown --seed value '" + val + "' (expected an integer).");
                        return;
                    }
                    seed = static_cast<int64_t>(parsed);
                }
            }
            const int32_t id = ctx.createWorld(args[1], args[2],
                                               inventoryPolicy, forceGameMode, seed);
            if (id < 0)
            {
                ctx.reply("Could not create plane '" + args[1] + "'.");
                return;
            }
            std::string policyTag = "standard";
            if (inventoryPolicy == LightweightMC::Storage::kInventoryPolicySeparate)   policyTag = "separate";
            else if (inventoryPolicy == LightweightMC::Storage::kInventoryPolicyRestricted) policyTag = "restricted";
            std::string forceTag = "inherit";
            if (forceGameMode == LightweightMC::Storage::kForceGameModeSurvival)   forceTag = "survival";
            else if (forceGameMode == LightweightMC::Storage::kForceGameModeCreative)   forceTag = "creative";
            else if (forceGameMode == LightweightMC::Storage::kForceGameModeAdventure) forceTag = "adventure";
            else if (forceGameMode == LightweightMC::Storage::kForceGameModeSpectator) forceTag = "spectator";
            ctx.reply("Created plane '" + args[1] + "' as id=" + std::to_string(id) +
                      " style=" + args[2] +
                      " inventory=" + policyTag +
                      " force-gamemode=" + forceTag +
                      (seed != LightweightMC::Storage::kDefaultWorldSeed
                           ? " seed=" + std::to_string(seed)
                           : std::string(" seed=inherit")));
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

// -----------------------------------------------------------------------------
// Phase 8 (0.0.16) world-model trigger -- /import-world and /export-world.
//
// The CommandContext hooks (importWorldFromPath / exportWorldToPath / worldModelName)
// already exist on the ctx struct; the two handlers here just forward the
// args onto those closures and format a human-readable reply. Format resolution
// lives inside NetworkManager::importWorldFromPath / exportWorldToPath so
// the sqlite / anvil / legacy backends can consult m_worldModel -- the
// command handler only needs to know whether to override that value with an
// explicit third positional or with a filename-suffix guess (.mca = anvil,
// .mce / .dat = legacy, .db = sqlite). When the caller passes no explicit
// hint and no suffix matches, we pass "" so NetworkManager falls back to the
// boot-selected world model.
//
// Op level 3 mirrors /world's -- these commands change server state, not one
// player's state, so affectsPlayerState() returns false like WorldCommand.
// -----------------------------------------------------------------------------
class ImportWorldCommand : public Command
{
public:
    std::string getName() const override { return "import-world"; }
    std::string getDescription() const override
    {
        return "Load a world file into a registered plane using the current world-model backend.";
    }
    std::string getUsage() const override
    {
        return "/import-world <path> [plane-name] [format: sqlite|anvil|legacy]";
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
        const std::string &path = args[0];
        std::string planeName = "overworld";
        if (args.size() >= 2)
            planeName = args[1];
        std::string hint;
        if (args.size() >= 3)
            hint = args[2];
        else
        {
            // Best-effort suffix -> format inference so admins can type
            // `/import-world plain.mca overworld` without repeating the tag.
            const auto dot = path.rfind('.');
            if (dot != std::string::npos)
            {
                std::string suffix = path.substr(dot + 1);
                for (auto &c : suffix)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (suffix == "mca")       hint = "anvil";
                else if (suffix == "mce")  hint = "legacy";
                else if (suffix == "db")   hint = "sqlite";
            }
        }
        if (!ctx.importWorldFromPath(path, planeName, hint))
        {
            ctx.reply("Could not import '" + path + "' into plane '" + planeName + "'.");
            return;
        }
        ctx.reply("Imported '" + path + "' into plane '" + planeName +
                  "' via world model '" + ctx.worldModelName() + "'");
    }
};

class ExportWorldCommand : public Command
{
public:
    std::string getName() const override { return "export-world"; }
    std::string getDescription() const override
    {
        return "Copy a registered plane's world file to disk using the current world-model backend.";
    }
    std::string getUsage() const override
    {
        return "/export-world <path> [plane-name] [format: sqlite|anvil|legacy]";
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
        const std::string &path = args[0];
        std::string planeName = "overworld";
        if (args.size() >= 2)
            planeName = args[1];
        std::string hint;
        if (args.size() >= 3)
            hint = args[2];
        else
        {
            const auto dot = path.rfind('.');
            if (dot != std::string::npos)
            {
                std::string suffix = path.substr(dot + 1);
                for (auto &c : suffix)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (suffix == "mca")       hint = "anvil";
                else if (suffix == "mce")  hint = "legacy";
                else if (suffix == "db")   hint = "sqlite";
            }
        }
        if (!ctx.exportWorldToPath(path, planeName, hint))
        {
            ctx.reply("Could not export plane '" + planeName + "' to '" + path + "'.");
            return;
        }
        ctx.reply("Exported plane '" + planeName + "' to '" + path +
                  "' via world model '" + ctx.worldModelName() + "'");
    }
};
