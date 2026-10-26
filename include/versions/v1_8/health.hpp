#pragma once
// Curated Minecraft 1.8 (protocol 47) player health/food constants.
namespace LightweightMC::Versions::V1_8 {
    constexpr float MAX_HEALTH = 20.0f;
    constexpr int MAX_FOOD = 20;
    constexpr float MAX_SATURATION = 20.0f;
    constexpr float HEALTH_REGEN_PER_TICK = 1.0f; // half-heart? simplified
    constexpr int REGEN_DELAY_TICKS = 80; // 4s at 20TPS
}
