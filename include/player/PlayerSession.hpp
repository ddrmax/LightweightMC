#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>

namespace LightweightMC::Network::Protocol
{
    class EraAdapter; // Forward declaration (defined in network/protocol/EraAdapter.hpp)
}

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
        uint8_t openWindowId{0}; // next window ID to assign when opening a container
        std::unordered_map<int, ItemStack> inventory{};

        std::vector<uint8_t> rxBuffer{};
        std::vector<uint8_t> sendBuffer{};

        // Multi-version protocol support: the era adapter chosen during handshake.
        // Non-null once the client has sent a Handshake packet. Callers do not own
        // the pointer (adapters are process-lifetime singletons from EraAdapter::create).
        Network::Protocol::EraAdapter *adapter{nullptr};
        int32_t clientProtocol{47}; // Protocol number reported by the client in the handshake.
    };
}
using LightweightMC::Player::ChunkPos;
using LightweightMC::Player::ChunkPosHash;
using LightweightMC::Player::ClientState;