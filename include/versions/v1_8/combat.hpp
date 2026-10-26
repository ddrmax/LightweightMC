#pragma once
// Curated Minecraft 1.8 (protocol 47) attack/damage constants (simplified).
namespace LightweightMC::Versions::V1_8 {
    constexpr float BASE_ATTACK_DAMAGE = 1.0f;
    constexpr float ATTACK_COOLDOWN_TICKS = 10.0f; // 0.5s
    constexpr float FALL_DAMAGE_MULTIPLIER = 1.0f;
    constexpr int ARMOR_TOUGHNESS = 0; // 1.8 has no toughness
}
