#pragma once
#include <cstdint>
#include <sys/epoll.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

#include "storage/WorldStorage.hpp"

namespace LightweightMC::Network
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
    // Inventory  Slot Structure
    struct ItemStack
    {
        int16_t id = -1;
        uint8_t count = 0;
        int16_t damage = 0;
    };

    struct ClientSession
    {
        int fd{-1};
        ClientState state{ClientState::HANDSHAKE};
        std::string username{""};

        double x{0.0}, y{7.0}, z{0.0};
        int currentChunkX{0};
        int currentChunkZ{0};
        std::unordered_set<ChunkPos, ChunkPosHash> loadedChunks{};
        int16_t selectedSlot = 0;
        std::unordered_map<int, ItemStack> inventory;
        std::vector<uint8_t> rxBuffer; // TCP Buffer
        std::vector<uint8_t> sendBuffer;
    };

    class NetworkManager
    {
    private:
        int m_serverFd{-1};
        int m_epollFd{-1};
        epoll_event m_events[64];
        std::unordered_map<int, ClientSession> m_clients;
        std::unordered_map<int, std::vector<std::string>> m_clientScoreboardCache;
        Storage::WorldStorage m_worldStorage;
        void sendScoreboard(int clientFd);
        void sendTablistHeaderFooter(int clientFd);

        void setNonBlocking(int fd);
        void handleClientData(int clientFd);
        void handleDisconnect(int fd);
        void sendPacket(int fd, int32_t packetId, const std::vector<uint8_t> &payload);
        void flushSendBuffer(ClientSession &client);

        void teleportPlayer(int fd, double x, double y, double z, float yaw = 0.0f, float pitch = 0.0f);

        void sendPlayPackets(int clientFd, const std::string &username);
        void sendFlatChunk(int clientFd, int chunkX, int chunkZ);
        void updateChunksAroundPlayer(int clientFd, int renderDistance = 6);
        void broadcastEquipment(int entityId, int16_t itemSlot, int16_t itemId);

        void broadcastPacket(int32_t packetId, const std::vector<uint8_t> &payload, int ignoreFd = -1);

    public:
        NetworkManager() = default;
        ~NetworkManager() = default;

        bool start(uint16_t port);
        void pollEvents(int timeoutMs);
        void stop();
    };

} // namespace LightweightMC::Network