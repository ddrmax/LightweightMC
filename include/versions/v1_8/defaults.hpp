#pragma once
// Curated Minecraft 1.8 (protocol 47) defaults.
// Generated from tools/protocol_seed/packet_ids_1.8.json + versions.json.
namespace LightweightMC::Versions::V1_8 {
    constexpr int PROTOCOL = 47;
    constexpr int DEFAULT_DIFFICULTY = 1; // easy
    constexpr int MAX_PLAYERS_DEFAULT = 20;
    constexpr int VIEW_DISTANCE = 10;
    constexpr int SPAWN_PROTECTION = 16;
    constexpr const char* LEVEL_TYPE = "default";
}
