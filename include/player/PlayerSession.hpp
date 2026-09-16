#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>

namespace LightweightMC::Player
{
    enum class ClientState
    {
        HANDSHAKE,
        STATUS,
        LOGIN,
        PLAY
    };

    struct ChunkPos
    {
        int x;
        int z;
        bool operator==(const ChunkPos &other) const { return x == other.x && z == other.z; }
    };

    struct ChunkPosHash
    {
        std::size_t operator()(const ChunkPos &pos) const
        {
            return std::hash<int>()(pos.x) ^ (std::hash<int>()(pos.z) << 1);
        }
    };

    struct ItemStack
    {
        int16_t id = -1;
        uint8_t count = 0;
        int16_t damage = 0;
    };

    struct PlayerSession
    {
        int fd{-1};
        ClientState state{ClientState::HANDSHAKE};
        std::string username{""};

        double x{0.0}, y{7.0}, z{0.0};
        float yaw{0.0f}, pitch{0.0f};

        int currentChunkX{0};
        int currentChunkZ{0};
        std::unordered_set<ChunkPos, ChunkPosHash> loadedChunks{};

        int16_t selectedSlot{0};
        std::unordered_map<int, ItemStack> inventory{};

        std::vector<uint8_t> rxBuffer{};
        std::vector<uint8_t> sendBuffer{};
    };
}
using LightweightMC::Player::ChunkPos;
using LightweightMC::Player::ChunkPosHash;
using LightweightMC::Player::ClientState;