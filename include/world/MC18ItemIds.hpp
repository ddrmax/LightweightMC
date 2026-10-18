#pragma once

// LWMC item ids (0.0.15 phase 9).
//
// LWMC-authored numeric item ids, NOT the MC 1.8 vanilla item-ID table: the
// vanilla pre-1.13 item ids were never embedded in this codebase, and the
// existing item space is already LWMC's own -- phase 6 mob loot uses IDs 0..10
// (see EntityManager.cpp::kItemTemplates: leather 0 .. rabbit_foot 10), which
// is the space persisted in players.inventory_blob / block_entities data_blob
// (see the container codec in PlayerSession.cpp and Database.hpp's
// saveContainer comment). New items therefore continue that same space in an
// explicitly documented band so old saves keep decoding, and CraftingRules /
// ProcessingTick / ItemDurability all name these constants rather than magic
// numbers.
//
// Layout:
//   0..10   mob loot (phase 6, unchanged)
//   11..49  LWMC materials + crafted objects
//   50..79  LWMC food / consumables (raw + cooked + brewed)
//   80..99  LWMC tools & weapons (the durability-bearing class)

#include <cstdint>

namespace LightweightMC
{
    namespace World
    {
        namespace Items18
        {
            // --- mob loot (phase 6, EntityManager::kItemTemplates) ---
            constexpr std::int32_t LEATHER = 0;
            constexpr std::int32_t MEAT_RAW = 1;
            constexpr std::int32_t MEAT_COOKED = 2;
            constexpr std::int32_t WOOL = 3;
            constexpr std::int32_t FEATHER = 4;
            constexpr std::int32_t WOLF_PELT = 5;
            constexpr std::int32_t PELT = 6;
            constexpr std::int32_t BRAIN = 7;
            constexpr std::int32_t BONE = 8;
            constexpr std::int32_t HIDE = 9;
            constexpr std::int32_t RABBIT_FOOT = 10;

            // --- materials / crafted objects ---
            constexpr std::int32_t TWIG = 11;
            constexpr std::int32_t FLINT = 12;         // fire starter (burnable frontier source)
            constexpr std::int32_t REED = 13;
            constexpr std::int32_t CLAY = 14;
            constexpr std::int32_t CHARCOAL = 15;      // furnace fuel
            constexpr std::int32_t ASH = 16;           // burned-out residue
            constexpr std::int32_t WATTLE = 17;        // woven wattle (hopper/dispenser body)
            constexpr std::int32_t HOLLOW_REED = 18;   // brewing vessel
            constexpr std::int32_t IRON_FITTING = 19;  // dispenser hardware
            constexpr std::int32_t OAK_PLANK = 20;
            constexpr std::int32_t GRAPE = 21;         // brewing input
            constexpr std::int32_t BARLEY = 22;        // brewing input
            constexpr std::int32_t CRAFTING_TABLE_KIT = 23;
            constexpr std::int32_t CHEST_KIT = 24;
            constexpr std::int32_t FURNACE_KIT = 25;
            constexpr std::int32_t BREWING_STAND_KIT = 26;
            constexpr std::int32_t HOPPER_KIT = 27;
            constexpr std::int32_t DISPENSER_KIT = 28;
            constexpr std::int32_t TORCH_KIT = 29;

            // --- food / consumables ---
            constexpr std::int32_t BREAD = 50;
            constexpr std::int32_t COOKED_MEAT = 51;   // furnace output for MEAT_RAW
            constexpr std::int32_t COOKED_FISH = 52;
            constexpr std::int32_t STEW = 53;
            constexpr std::int32_t SALAD = 54;
            constexpr std::int32_t JUICE = 55;
            constexpr std::int32_t BROTH = 56;
            constexpr std::int32_t GROG = 57;          // brewing-stand output (potable grog)
            // LWMC potions carry an encoded brew grade in ItemDurability's
            // damage field rather than a distinct id (see brewGrade/damageFor).
            constexpr std::int32_t MEDICINE = 58;
            constexpr std::int32_t FERTILIZER = 59;    // crop booster (processing input)

            // --- tools & weapons (durability-bearing) ---
            constexpr std::int32_t HATCHET = 80;       // 1.9 axe slot
            constexpr std::int32_t MACHETE = 81;       // 1.21 weapon slot
            constexpr std::int32_t SPIKE = 82;         // 1.22 sword slot
            constexpr std::int32_t ADZE = 83;
            constexpr std::int32_t HOE = 84;
            constexpr std::int32_t MATTOCK = 85;       // 1.21 pickaxe slot
            constexpr std::int32_t CHISEL = 86;
            constexpr std::int32_t HAMMER = 87;        // anvil enchanting tool
            constexpr std::int32_t SCYTHE = 88;

            // Air/no-item sentinel matching the container codec's empty slot
            // encoding (PlayerSession.cpp uses id == -1).
            constexpr std::int32_t NONE = -1;

            // Max stack size for the LWMC space: containers store counts in a
            // single byte (see serializeContainerItems), so 127 is the largest
            // value that survives a round trip through block_entities.
            constexpr std::int32_t kMaxStack = 127;

        } // namespace Items18
    }     // namespace World
} // namespace LightweightMC
