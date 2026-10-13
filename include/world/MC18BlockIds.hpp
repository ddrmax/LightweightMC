#pragma once

// Minecraft 1.8-era (Java Edition, pre-1.13 numeric IDs) canonical block IDs.
//
// Curated from the Minecraft Wiki article "Java Edition data values /
// Pre-flattening / Block IDs" -- the numeric-ID table that was in use up
// through 1.12.2 (Protocol 47 / 1.7 / 1.8 era, before the 1.13 dictionary
// format and before 1.14's fully-string IDs). LWMC stores chunks internally
// with these numeric IDs; the network Packet/ProtocolTranslator layer
// converts them to the era-specific wire IDs at send time.
//
// Every block from ID 0 up through 197 (the highest block ID introduced in 1.8)
// is present.
//
// Aliases: legacy LWMC names that don't match the wiki's canonical spellings
// are kept in an explicit legacy block at the bottom so existing call-sites
// in src/storage/WorldGen.cpp and src/storage/WorldStorage.cpp still resolve.

#include <cstdint>

namespace LightweightMC
{
    namespace World
    {
        namespace Blocks18
        {
            // ---------------------------------------------------------------------
            // Canonical MC 1.8 (Java, pre-1.13) numeric block IDs, in ID order.
            // Values are the raw block ID; combine with meta via combined18().
            // ---------------------------------------------------------------------
            constexpr std::uint16_t AIR = 0;         // Air / (no block)
            constexpr std::uint16_t STONE = 1;       // Stone, Granite, Diorite, Andesite (via meta)
            constexpr std::uint16_t GRASS_BLOCK = 2; // Grass
            constexpr std::uint16_t DIRT = 3;        // Dirt, Coarse Dirt, Podzol (via meta)
            constexpr std::uint16_t COBBLESTONE = 4;
            constexpr std::uint16_t PLANKS = 5; // Wood Planks
            constexpr std::uint16_t SAPLING = 6;
            constexpr std::uint16_t BEDROCK = 7; // Rock / Bedrock
            constexpr std::uint16_t WATER_FLOWING = 8;
            constexpr std::uint16_t WATER_STATIONARY = 9;
            constexpr std::uint16_t LAVA_FLOWING = 10;
            constexpr std::uint16_t LAVA_STATIONARY = 11;
            constexpr std::uint16_t SAND = 12; // Sand, Red Sand (via meta)
            constexpr std::uint16_t GRAVEL = 13;
            constexpr std::uint16_t GOLD_ORE = 14;
            constexpr std::uint16_t IRON_ORE = 15;
            constexpr std::uint16_t COAL_ORE = 16;
            constexpr std::uint16_t LOG = 17;    // Wood / Tree Trunk (Oak, Spruce, Birch, Jungle)
            constexpr std::uint16_t LEAVES = 18; // Oak, Spruce, Birch, Jungle leaves
            constexpr std::uint16_t SPONGE = 19; // Sponge, Wet Sponge (via meta in 1.8)
            constexpr std::uint16_t GLASS = 20;
            constexpr std::uint16_t LAPIS_ORE = 21;
            constexpr std::uint16_t LAPIS_BLOCK = 22;
            constexpr std::uint16_t DISPENSER = 23;
            constexpr std::uint16_t SANDSTONE = 24;
            constexpr std::uint16_t NOTEBLOCK = 25;
            constexpr std::uint16_t BED = 26;
            constexpr std::uint16_t GOLDEN_RAIL = 27; // Powered Rail
            constexpr std::uint16_t DETECTOR_RAIL = 28;
            constexpr std::uint16_t STICKY_PISTON = 29;
            constexpr std::uint16_t COBWEB = 30;    // Spider Web
            constexpr std::uint16_t TALLGRASS = 31; // Grass Tuft / Bush / Fern
            constexpr std::uint16_t DEAD_BUSH = 32;
            constexpr std::uint16_t PISTON = 33;
            constexpr std::uint16_t PISTON_HEAD = 34;
            constexpr std::uint16_t WOOL = 35;             // Colored via meta nibble
            constexpr std::uint16_t PISTON_EXTENSION = 36; // Piston rod
            constexpr std::uint16_t FLOWER_YELLOW = 37;    // Dandelion
            constexpr std::uint16_t FLOWER_RED = 38;       // Poppy, Orchid, Allium, etc.
            constexpr std::uint16_t MUSHROOM_BROWN = 39;
            constexpr std::uint16_t MUSHROOM_RED = 40;
            constexpr std::uint16_t GOLD_BLOCK = 41;
            constexpr std::uint16_t IRON_BLOCK = 42;
            constexpr std::uint16_t DOUBLE_STONE_SLAB = 43;
            constexpr std::uint16_t STONE_SLAB = 44;
            constexpr std::uint16_t BRICK_BLOCK = 45;
            constexpr std::uint16_t TNT = 46;
            constexpr std::uint16_t BOOKSHELF = 47;
            constexpr std::uint16_t MOSSY_COBBLESTONE = 48;
            constexpr std::uint16_t OBSIDIAN = 49;
            constexpr std::uint16_t TORCH = 50;
            constexpr std::uint16_t FIRE = 51;
            constexpr std::uint16_t SPAWNER = 52; // Mob Spawner
            constexpr std::uint16_t OAK_STAIRS = 53;
            constexpr std::uint16_t CHEST = 54;
            constexpr std::uint16_t REDSTONE_WIRE = 55;
            constexpr std::uint16_t DIAMOND_ORE = 56;
            constexpr std::uint16_t DIAMOND_BLOCK = 57;
            constexpr std::uint16_t CRAFTING_TABLE = 58;
            constexpr std::uint16_t WHEAT = 59;    // Crops stage via meta
            constexpr std::uint16_t FARMLAND = 60; // Tilled soil
            constexpr std::uint16_t FURNACE = 61;
            constexpr std::uint16_t LIT_FURNACE = 62;
            constexpr std::uint16_t STANDING_SIGN = 63;
            constexpr std::uint16_t WOODEN_DOOR = 64; // Oak Door
            constexpr std::uint16_t LADDER = 65;
            constexpr std::uint16_t RAIL = 66;         // Train Track
            constexpr std::uint16_t STONE_STAIRS = 67; // Cobblestone Stairs
            constexpr std::uint16_t WALL_SIGN = 68;
            constexpr std::uint16_t LEVER = 69;
            constexpr std::uint16_t STONE_PRESSURE_PLATE = 70;
            constexpr std::uint16_t IRON_DOOR = 71;
            constexpr std::uint16_t WOODEN_PRESSURE_PLATE = 72;
            constexpr std::uint16_t REDSTONE_ORE = 73;
            constexpr std::uint16_t LIT_REDSTONE_ORE = 74;
            constexpr std::uint16_t UNLIT_REDSTONE_TORCH = 75;
            constexpr std::uint16_t REDSTONE_TORCH = 76;
            constexpr std::uint16_t STONE_BUTTON = 77;
            constexpr std::uint16_t SNOW_LAYER = 78;
            constexpr std::uint16_t ICE = 79;
            constexpr std::uint16_t SNOW = 80;
            constexpr std::uint16_t CACTUS = 81;
            constexpr std::uint16_t CLAY = 82;  // Unfired Clay
            constexpr std::uint16_t REEDS = 83; // Sugar Cane
            constexpr std::uint16_t JUKEBOX = 84;
            constexpr std::uint16_t FENCE = 85; // Oak Fence
            constexpr std::uint16_t PUMPKIN = 86;
            constexpr std::uint16_t NETHERRACK = 87;
            constexpr std::uint16_t SOUL_SAND = 88;
            constexpr std::uint16_t GLOWSTONE = 89;
            constexpr std::uint16_t PORTAL = 90;      // Nether Portal
            constexpr std::uint16_t LIT_PUMPKIN = 91; // Jack o'Lantern
            constexpr std::uint16_t CAKE = 92;
            constexpr std::uint16_t UNPOWERED_REPEATER = 93; // Redstone Repeater
            constexpr std::uint16_t POWERED_REPEATER = 94;   // Powered Repeater
            constexpr std::uint16_t STAINED_GLASS = 95;
            constexpr std::uint16_t TRAPDOOR = 96;
            constexpr std::uint16_t MONSTER_EGG = 97; // Infested Stone
            constexpr std::uint16_t STONEBRICK = 98;
            constexpr std::uint16_t BROWN_MUSHROOM_BLOCK = 99;
            constexpr std::uint16_t RED_MUSHROOM_BLOCK = 100;
            constexpr std::uint16_t IRON_BARS = 101;
            constexpr std::uint16_t GLASS_PANE = 102;
            constexpr std::uint16_t MELON_BLOCK = 103;
            constexpr std::uint16_t PUMPKIN_STEM = 104;
            constexpr std::uint16_t MELON_STEM = 105;
            constexpr std::uint16_t VINE = 106;
            constexpr std::uint16_t FENCE_GATE = 107; // Oak Fence Gate
            constexpr std::uint16_t BRICK_STAIRS = 108;
            constexpr std::uint16_t STONE_BRICK_STAIRS = 109;
            constexpr std::uint16_t MYCELIUM = 110;
            constexpr std::uint16_t WATERLILY = 111; // Lily Pad
            constexpr std::uint16_t NETHER_BRICK = 112;
            constexpr std::uint16_t NETHER_BRICK_FENCE = 113;
            constexpr std::uint16_t NETHER_BRICK_STAIRS = 114;
            constexpr std::uint16_t NETHER_WART = 115;
            constexpr std::uint16_t ENCHANTING_TABLE = 116;
            constexpr std::uint16_t BREWING_STAND = 117;
            constexpr std::uint16_t CAULDRON = 118;
            constexpr std::uint16_t END_PORTAL = 119;
            constexpr std::uint16_t END_PORTAL_FRAME = 120;
            constexpr std::uint16_t END_STONE = 121;
            constexpr std::uint16_t DRAGON_EGG = 122;
            constexpr std::uint16_t REDSTONE_LAMP = 123;
            constexpr std::uint16_t LIT_REDSTONE_LAMP = 124;
            constexpr std::uint16_t DOUBLE_WOODEN_SLAB = 125;
            constexpr std::uint16_t WOODEN_SLAB = 126;
            constexpr std::uint16_t COCOA = 127;
            constexpr std::uint16_t SANDSTONE_STAIRS = 128;
            constexpr std::uint16_t EMERALD_ORE = 129;
            constexpr std::uint16_t ENDER_CHEST = 130;
            constexpr std::uint16_t TRIPWIRE_HOOK = 131;
            constexpr std::uint16_t TRIPWIRE = 132;
            constexpr std::uint16_t EMERALD_BLOCK = 133;
            constexpr std::uint16_t SPRUCE_STAIRS = 134;
            constexpr std::uint16_t BIRCH_STAIRS = 135;
            constexpr std::uint16_t JUNGLE_STAIRS = 136;
            constexpr std::uint16_t COMMAND_BLOCK = 137;
            constexpr std::uint16_t BEACON = 138;
            constexpr std::uint16_t COBBLESTONE_WALL = 139;
            constexpr std::uint16_t FLOWER_POT = 140;
            constexpr std::uint16_t CARROTS = 141;
            constexpr std::uint16_t POTATOES = 142;
            constexpr std::uint16_t WOODEN_BUTTON = 143;
            constexpr std::uint16_t SKULL = 144;
            constexpr std::uint16_t ANVIL = 145;
            constexpr std::uint16_t TRAPPED_CHEST = 146;
            constexpr std::uint16_t LIGHT_WEIGHTED_PRESSURE_PLATE = 147; // Gold
            constexpr std::uint16_t HEAVY_WEIGHTED_PRESSURE_PLATE = 148; // Iron
            constexpr std::uint16_t UNPOWERED_COMPARATOR = 149;
            constexpr std::uint16_t POWERED_COMPARATOR = 150;
            constexpr std::uint16_t DAYLIGHT_DETECTOR = 151;
            constexpr std::uint16_t REDSTONE_BLOCK = 152;
            constexpr std::uint16_t QUARTZ_ORE = 153;
            constexpr std::uint16_t HOPPER = 154;
            constexpr std::uint16_t QUARTZ_BLOCK = 155;
            constexpr std::uint16_t QUARTZ_STAIRS = 156;
            constexpr std::uint16_t ACTIVATOR_RAIL = 157;
            constexpr std::uint16_t DROPPER = 158;
            constexpr std::uint16_t STAINED_HARDENED_CLAY = 159; // Stained Terracotta
            constexpr std::uint16_t STAINED_GLASS_PANE = 160;
            constexpr std::uint16_t LEAVES2 = 161; // Acacia, Dark Oak leaves
            constexpr std::uint16_t LOG2 = 162;    // Acacia, Dark Oak logs
            constexpr std::uint16_t ACACIA_STAIRS = 163;
            constexpr std::uint16_t DARK_OAK_STAIRS = 164;
            constexpr std::uint16_t SLIME_BLOCK = 165;   // (Ajout 1.8)
            constexpr std::uint16_t BARRIER = 166;       // (Ajout 1.8) Invisible Bedrock
            constexpr std::uint16_t IRON_TRAPDOOR = 167; // (Ajout 1.8)
            constexpr std::uint16_t PRISMARINE = 168;    // (Ajout 1.8) Prismarine, Bricks, Dark (via meta)
            constexpr std::uint16_t SEA_LANTERN = 169;   // (Ajout 1.8)
            constexpr std::uint16_t HAY_BLOCK = 170;
            constexpr std::uint16_t CARPET = 171;        // Wool Carpet
            constexpr std::uint16_t HARDENED_CLAY = 172; // Terracotta
            constexpr std::uint16_t COAL_BLOCK = 173;
            constexpr std::uint16_t PACKED_ICE = 174;
            constexpr std::uint16_t DOUBLE_PLANT = 175;               // Large Flowers/Grass
            constexpr std::uint16_t STANDING_BANNER = 176;            // (Ajout 1.8)
            constexpr std::uint16_t WALL_BANNER = 177;                // (Ajout 1.8)
            constexpr std::uint16_t DAYLIGHT_DETECTOR_INVERTED = 178; // (Ajout 1.8)
            constexpr std::uint16_t RED_SANDSTONE = 179;              // (Ajout 1.8)
            constexpr std::uint16_t RED_SANDSTONE_STAIRS = 180;       // (Ajout 1.8)
            constexpr std::uint16_t DOUBLE_STONE_SLAB2 = 181;         // (Ajout 1.8) Red Sandstone Slab Double
            constexpr std::uint16_t STONE_SLAB2 = 182;                // (Ajout 1.8) Red Sandstone Slab
            constexpr std::uint16_t SPRUCE_FENCE_GATE = 183;          // (Ajout 1.8)
            constexpr std::uint16_t BIRCH_FENCE_GATE = 184;           // (Ajout 1.8)
            constexpr std::uint16_t JUNGLE_FENCE_GATE = 185;          // (Ajout 1.8)
            constexpr std::uint16_t DARK_OAK_FENCE_GATE = 186;        // (Ajout 1.8)
            constexpr std::uint16_t ACACIA_FENCE_GATE = 187;          // (Ajout 1.8)
            constexpr std::uint16_t SPRUCE_FENCE = 188;               // (Ajout 1.8)
            constexpr std::uint16_t BIRCH_FENCE = 189;                // (Ajout 1.8)
            constexpr std::uint16_t JUNGLE_FENCE = 190;               // (Ajout 1.8)
            constexpr std::uint16_t DARK_OAK_FENCE = 191;             // (Ajout 1.8)
            constexpr std::uint16_t ACACIA_FENCE = 192;               // (Ajout 1.8)
            constexpr std::uint16_t SPRUCE_DOOR = 193;                // (Ajout 1.8)
            constexpr std::uint16_t BIRCH_DOOR = 194;                 // (Ajout 1.8)
            constexpr std::uint16_t JUNGLE_DOOR = 195;                // (Ajout 1.8)
            constexpr std::uint16_t ACACIA_DOOR = 196;                // (Ajout 1.8)
            constexpr std::uint16_t DARK_OAK_DOOR = 197;              // (Ajout 1.8)

            // ---------------------------------------------------------------------
            // Legacy LWMC aliases -- kept for internal compatibility.
            // ---------------------------------------------------------------------
            constexpr std::uint16_t WOOD = LOG;
            constexpr std::uint16_t TREE_TRUNK = LOG;
            constexpr std::uint16_t WOOD_PLANK = PLANKS;
            constexpr std::uint16_t PINE_NEEDLES = LEAVES;
            constexpr std::uint16_t MOSS = MOSSY_COBBLESTONE;
            constexpr std::uint16_t METAL = IRON_BLOCK;
            constexpr std::uint16_t BOULDER = COBBLESTONE;
            constexpr std::uint16_t FUNGUS = MUSHROOM_BROWN;
            constexpr std::uint16_t LIGHTSTONE = GLOWSTONE;
            constexpr std::uint16_t SOIL = FARMLAND;
            constexpr std::uint16_t CROPS = WHEAT;
            constexpr std::uint16_t WATER = WATER_FLOWING;
            constexpr std::uint16_t WATER_DEEP = WATER_STATIONARY;
            constexpr std::uint16_t LAVA = LAVA_FLOWING;
            constexpr std::uint16_t WEB = COBWEB;
            constexpr std::uint16_t GRASS_DECOR = TALLGRASS;
            constexpr std::uint16_t RAILS = RAIL;
            constexpr std::uint16_t MOB_SPAWNER = SPAWNER;
            constexpr std::uint16_t STORM_TRIGGER = AIR;
            constexpr std::uint16_t VENT = AIR;
            constexpr std::uint16_t FORCE_FIELD = SLIME_BLOCK; // ID 165 en 1.8

            // combined(id, meta) -- pre-1.13 storage form used by WorldStorage chunks.
            constexpr std::uint16_t combined18(std::uint16_t id, std::uint8_t meta = 0)
            {
                return static_cast<std::uint16_t>((id << 4) | (meta & 0x0F));
            }

            // Overworld sea level -- matches MC 1.8 vanilla (y=62).
            constexpr int SEA_LEVEL = 62;

            // Compile-time sanity: canonical IDs 0..197 must fit in the 12-bit
            // pre-1.13 block ID field (u16 storage with 4-bit meta nibble).
            static_assert(DARK_OAK_DOOR < (1u << 12),
                          "MC18 block IDs must fit in the 12-bit ID field");

        } // namespace Blocks18
    } // namespace World
} // namespace LightweightMC