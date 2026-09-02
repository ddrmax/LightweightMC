#include "network/NetworkManager.hpp"
#include "storage/WorldStorage.hpp"
#include "core/Logger.hpp"
#include "network/Packet.hpp"
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cmath>
#include <csignal>
#include <sys/sysinfo.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
namespace LightweightMC::Network
{

    void NetworkManager::setNonBlocking(int fd)
    {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    void NetworkManager::sendPacket(int fd, int32_t packetId, const std::vector<uint8_t> &payload)
    {
        std::vector<uint8_t> body;
        Packet::writeVarInt(body, packetId);
        body.insert(body.end(), payload.begin(), payload.end());

        std::vector<uint8_t> frame;
        Packet::writeVarInt(frame, static_cast<int32_t>(body.size()));
        frame.insert(frame.end(), body.begin(), body.end());

        // MSG_NOSIGNAL also prevents SIGPIPE from being raised
        send(fd, frame.data(), frame.size(), MSG_NOSIGNAL);
    }

    // Helper to retrieve RAM consumed by the process (in MB)
    static double getProcessRAM()
    {
        std::ifstream statm("/proc/self/statm");
        long pages = 0;
        if (statm >> pages)
        {
            return (pages * sysconf(_SC_PAGESIZE)) / (1024.0 * 1024.0);
        }
        return 0.0;
    }

    // Helper to retrieve the system CPU load
    static double getCPUUsage()
    {
        double load[1];
        if (getloadavg(load, 1) != -1)
        {
            return load[0];
        }
        return 0.0;
    }

    bool NetworkManager::start(uint16_t port)
    {
        // Ignore SIGPIPE to prevent unexpected server termination if a client disconnects
        std::signal(SIGPIPE, SIG_IGN);
        // Initialize SQLite database
        if (!m_worldStorage.init("world.db"))
        {
            Core::Logger::error("Failed to initialize SQLite database.");
            return false;
        }
        m_serverFd = socket(AF_INET, SOCK_STREAM, 0);
        int opt = 1;
        setsockopt(m_serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(m_serverFd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
            Core::Logger::error("Failed to bind socket to port.");
            return false;
        }

        listen(m_serverFd, SOMAXCONN);
        setNonBlocking(m_serverFd);

        m_epollFd = epoll_create1(0);
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = m_serverFd;
        epoll_ctl(m_epollFd, EPOLL_CTL_ADD, m_serverFd, &ev);

        return true;
    }
    // Stores previous text for each line per client (e.g., std::map<int, std::vector<std::string>> m_lastScoreboardLines;)

    void NetworkManager::sendScoreboard(int clientFd)
    {
        std::string objectiveName = "server_stats";
        std::string title = "§e§lLIGHTWEIGHT MC";

        // 1. Create the objective (Packet 0x3B - Scoreboard Objective)
        std::vector<uint8_t> p1;
        Packet::writeString(p1, objectiveName);
        p1.push_back(0); // 0 = Create
        Packet::writeString(p1, title);
        Packet::writeString(p1, "integer");
        sendPacket(clientFd, 0x3B, p1);

        // 2. Display in Sidebar (Packet 0x3D - Display Scoreboard)
        std::vector<uint8_t> p2;
        p2.push_back(1); // Position 1 = Sidebar
        Packet::writeString(p2, objectiveName);
        sendPacket(clientFd, 0x3D, p2);

        // -------------------------------------------------------------
        // Helpers for Score Packets Management (Packet 0x3C)
        // -------------------------------------------------------------

        // Helper: Remove a scoreboard line (Action = 1)
        auto removeScoreLine = [&](const std::string &scoreName)
        {
            std::vector<uint8_t> p3;
            Packet::writeVarInt(p3, static_cast<int32_t>(scoreName.size()));
            p3.insert(p3.end(), scoreName.begin(), scoreName.end());

            p3.push_back(1); // Action 1 = Remove

            Packet::writeVarInt(p3, static_cast<int32_t>(objectiveName.size()));
            p3.insert(p3.end(), objectiveName.begin(), objectiveName.end());

            sendPacket(clientFd, 0x3C, p3);
        };

        // Helper: Create / Update a scoreboard line (Action = 0)
        auto sendScoreLine = [&](const std::string &scoreName, int scoreValue)
        {
            std::vector<uint8_t> p3;
            Packet::writeVarInt(p3, static_cast<int32_t>(scoreName.size()));
            p3.insert(p3.end(), scoreName.begin(), scoreName.end());

            p3.push_back(0); // Action 0 = Create / Update

            Packet::writeVarInt(p3, static_cast<int32_t>(objectiveName.size()));
            p3.insert(p3.end(), objectiveName.begin(), objectiveName.end());

            Packet::writeVarInt(p3, static_cast<int32_t>(scoreValue));

            sendPacket(clientFd, 0x3C, p3);
        };

        // -------------------------------------------------------------
        // Calculate Server Data
        // -------------------------------------------------------------
        double ram = getProcessRAM();
        double cpu = getCPUUsage();
        int onlinePlayers = static_cast<int>(m_clients.size());

        std::unordered_set<ChunkPos, ChunkPosHash> globalLoadedChunks;
        for (const auto &[fd, session] : m_clients)
        {
            globalLoadedChunks.insert(session.loadedChunks.begin(), session.loadedChunks.end());
        }
        int totalGlobalChunks = static_cast<int>(globalLoadedChunks.size());

        std::stringstream ramStream;
        ramStream << std::fixed << std::setprecision(1) << ram;

        std::stringstream cpuStream;
        cpuStream << std::fixed << std::setprecision(1) << cpu;

        // Build the new line list
        std::vector<std::string> newLines = {
            "§fPlayers: §a" + std::to_string(onlinePlayers),
            "§fLoaded Chunks: §e" + std::to_string(totalGlobalChunks),
            "§fRAM: §b" + ramStream.str() + " Mo",
            "§fCPU Load: §c" + cpuStream.str()};

        // 3. Purge OLD lines to avoid duplication
        auto &oldLines = m_clientScoreboardCache[clientFd];
        for (const auto &oldLine : oldLines)
        {
            removeScoreLine(oldLine);
        }

        // 4. Send NEW lines (descending scores to order the list)
        int score = static_cast<int>(newLines.size());
        for (const auto &line : newLines)
        {
            sendScoreLine(line, score--);
        }

        // Update local cache for the next refresh
        oldLines = newLines;
    }
    void NetworkManager::sendTablistHeaderFooter(int clientFd)
    {
        std::string headerJson = "{\"text\": \"§e§lLightweightMC Server\\n§7Real Time Status\"}";

        double ram = getProcessRAM();
        std::stringstream ramStream;
        ramStream << std::fixed << std::setprecision(1) << ram;

        std::string footerJson = "{\"text\": \"§fRAM Proc: §b" + ramStream.str() + " Mo §7| §fPlayers: §a" + std::to_string(m_clients.size()) + "\"}";

        // Packet 0x47 - Player List Header And Footer
        std::vector<uint8_t> payload;
        Packet::writeString(payload, headerJson);
        Packet::writeString(payload, footerJson);

        sendPacket(clientFd, 0x47, payload);
    }

    void NetworkManager::sendFlatChunk(int clientFd, int chunkX, int chunkZ)
    {
        auto blocks = m_worldStorage.getChunkBlocks(chunkX, chunkZ);

        std::vector<uint8_t> blockData;
        blockData.reserve(16 * 16 * 16 * 2); // 16 sections * 4096 blocks * 2 bytes

        // Reconstruct section 0 (Y = 0 to 15)
        for (int y = 0; y < 16; ++y)
        {
            for (int z = 0; z < 16; ++z)
            { // Minecraft Indexing: Y -> Z -> X
                for (int x = 0; x < 16; ++x)
                {
                    uint16_t blockType = 0;

                    Storage::BlockCoord coord{x, y, z};
                    auto it = blocks.find(coord);
                    if (it != blocks.end())
                    {
                        blockType = it->second;
                    }

                    blockData.push_back(static_cast<uint8_t>(blockType & 0xFF));
                    blockData.push_back(static_cast<uint8_t>((blockType >> 8) & 0xFF));
                }
            }
        }

        // Light & Biomes
        for (int i = 0; i < 2048; ++i)
            blockData.push_back(0x00); // Block Light
        for (int i = 0; i < 2048; ++i)
            blockData.push_back(0xFF); // Sky Light
        for (int i = 0; i < 256; ++i)
            blockData.push_back(1); // Biome

        std::vector<uint8_t> payload;
        Packet::writeInt(payload, chunkX);
        Packet::writeInt(payload, chunkZ);
        payload.push_back(1);                // Ground-Up continuous
        Packet::writeShort(payload, 0x0001); // Bitmask section 0
        Packet::writeVarInt(payload, static_cast<int32_t>(blockData.size()));
        payload.insert(payload.end(), blockData.begin(), blockData.end());

        sendPacket(clientFd, 0x21, payload);
        m_clients[clientFd].loadedChunks.insert({chunkX, chunkZ});
    }
    void NetworkManager::updateChunksAroundPlayer(int clientFd, int renderDistance)
    {
        auto &session = m_clients[clientFd];

        int playerChunkX = static_cast<int>(std::floor(session.x / 16.0));
        int playerChunkZ = static_cast<int>(std::floor(session.z / 16.0));

        // Generate and send all chunks within view distance that haven't been transmitted yet
        for (int cx = playerChunkX - renderDistance; cx <= playerChunkX + renderDistance; ++cx)
        {
            for (int cz = playerChunkZ - renderDistance; cz <= playerChunkZ + renderDistance; ++cz)
            {
                ChunkPos pos{cx, cz};
                if (session.loadedChunks.find(pos) == session.loadedChunks.end())
                {
                    sendFlatChunk(clientFd, cx, cz);
                }
            }
        }

        session.currentChunkX = playerChunkX;
        session.currentChunkZ = playerChunkZ;
    }

    void NetworkManager::sendPlayPackets(int clientFd, const std::string &username)
    {
        auto &currentSession = m_clients[clientFd];
        currentSession.username = username;
        currentSession.state = ClientState::PLAY;

        // -------------------------------------------------------------
        // A. Send required initialization packets (1.8)
        // -------------------------------------------------------------

        // 1. Join Game (Packet 0x01)
        std::vector<uint8_t> joinGame;
        Packet::writeInt(joinGame, clientFd);  // Entity ID
        joinGame.push_back(1);                 // Gamemode (1 = Creative)
        joinGame.push_back(0);                 // Dimension (0 = Overworld)
        joinGame.push_back(1);                 // Difficulty (1 = Easy)
        joinGame.push_back(100);               // Max Players
        Packet::writeString(joinGame, "flat"); // Level Type
        joinGame.push_back(0);                 // Reduced Debug Info
        sendPacket(clientFd, 0x01, joinGame);

        // 2. Spawn Position (Packet 0x05)
        std::vector<uint8_t> spawnPos;
        // Position x=0, y=6, z=0 encoded in BlockPosition format
        uint64_t location = ((0ULL & 0x3FFFFFF) << 38) | ((6ULL & 0xFFF) << 26) | (0ULL & 0x3FFFFFF);
        for (int i = 7; i >= 0; --i)
            spawnPos.push_back((location >> (i * 8)) & 0xFF);
        sendPacket(clientFd, 0x05, spawnPos);

        // 3. Load chunks around the player (mandatory before Position & Look)
        currentSession.x = 0.5;
        currentSession.y = 7.0;
        currentSession.z = 0.5;
        updateChunksAroundPlayer(clientFd, 3);

        // 4. Player Position And Look (Packet 0x08)
        std::vector<uint8_t> posLook;
        Packet::writeDouble(posLook, currentSession.x);
        Packet::writeDouble(posLook, currentSession.y);
        Packet::writeDouble(posLook, currentSession.z);
        Packet::writeFloat(posLook, 0.0f); // Yaw
        Packet::writeFloat(posLook, 0.0f); // Pitch
        posLook.push_back(0x00);           // Flags (0 = Absolute coordinates)
        sendPacket(clientFd, 0x08, posLook);

        // -------------------------------------------------------------
        // B. Add the new player to EVERYONE's Tablist (0x38)
        // -------------------------------------------------------------
        std::vector<uint8_t> addTab;
        Packet::writeVarInt(addTab, 0); // Action 0 = Add Player
        Packet::writeVarInt(addTab, 1); // 1 player

        // Dummy UUID based on clientFd
        for (int i = 0; i < 12; ++i)
            addTab.push_back(0);
        addTab.push_back((clientFd >> 24) & 0xFF);
        addTab.push_back((clientFd >> 16) & 0xFF);
        addTab.push_back((clientFd >> 8) & 0xFF);
        addTab.push_back(clientFd & 0xFF);

        // Username
        Packet::writeString(addTab, username);

        Packet::writeVarInt(addTab, 0); // 0 Properties
        Packet::writeVarInt(addTab, 1); // Gamemode
        Packet::writeVarInt(addTab, 0); // Ping
        addTab.push_back(0);            // No display name

        broadcastPacket(0x38, addTab);

        // -------------------------------------------------------------
        // C. Spawn the NEW player for EXISTING players (0x0C)
        // -------------------------------------------------------------
        std::vector<uint8_t> spawnNewPlayer;
        Packet::writeVarInt(spawnNewPlayer, clientFd); // Entity ID

        for (int i = 0; i < 12; ++i)
            spawnNewPlayer.push_back(0);
        spawnNewPlayer.push_back((clientFd >> 24) & 0xFF);
        spawnNewPlayer.push_back((clientFd >> 16) & 0xFF);
        spawnNewPlayer.push_back((clientFd >> 8) & 0xFF);
        spawnNewPlayer.push_back(clientFd & 0xFF);

        Packet::writeInt(spawnNewPlayer, static_cast<int32_t>(currentSession.x * 32.0));
        Packet::writeInt(spawnNewPlayer, static_cast<int32_t>(currentSession.y * 32.0));
        Packet::writeInt(spawnNewPlayer, static_cast<int32_t>(currentSession.z * 32.0));
        spawnNewPlayer.push_back(0);           // Yaw
        spawnNewPlayer.push_back(0);           // Pitch
        Packet::writeShort(spawnNewPlayer, 0); // Current Item
        spawnNewPlayer.push_back(0x7F);        // Metadata Terminator

        broadcastPacket(0x0C, spawnNewPlayer, clientFd);

        // -------------------------------------------------------------
        // D. Spawn EXISTING players on the NEW player's screen
        // -------------------------------------------------------------
        for (const auto &[otherFd, otherSession] : m_clients)
        {
            if (otherFd != clientFd && otherSession.state == ClientState::PLAY)
            {

                // 1. Add existing player to the new player's Tablist
                std::vector<uint8_t> addExistingTab;
                Packet::writeVarInt(addExistingTab, 0);
                Packet::writeVarInt(addExistingTab, 1);
                for (int i = 0; i < 12; ++i)
                    addExistingTab.push_back(0);
                addExistingTab.push_back((otherFd >> 24) & 0xFF);
                addExistingTab.push_back((otherFd >> 16) & 0xFF);
                addExistingTab.push_back((otherFd >> 8) & 0xFF);
                addExistingTab.push_back(otherFd & 0xFF);

                Packet::writeString(addExistingTab, otherSession.username);
                Packet::writeVarInt(addExistingTab, 0);
                Packet::writeVarInt(addExistingTab, 1);
                Packet::writeVarInt(addExistingTab, 0);
                addExistingTab.push_back(0);
                sendPacket(clientFd, 0x38, addExistingTab);

                // 2. Spawn existing player entity for the new player
                std::vector<uint8_t> spawnExisting;
                Packet::writeVarInt(spawnExisting, otherFd);
                for (int i = 0; i < 12; ++i)
                    spawnExisting.push_back(0);
                spawnExisting.push_back((otherFd >> 24) & 0xFF);
                spawnExisting.push_back((otherFd >> 16) & 0xFF);
                spawnExisting.push_back((otherFd >> 8) & 0xFF);
                spawnExisting.push_back(otherFd & 0xFF);

                Packet::writeInt(spawnExisting, static_cast<int32_t>(otherSession.x * 32.0));
                Packet::writeInt(spawnExisting, static_cast<int32_t>(otherSession.y * 32.0));
                Packet::writeInt(spawnExisting, static_cast<int32_t>(otherSession.z * 32.0));
                spawnExisting.push_back(0);
                spawnExisting.push_back(0);
                Packet::writeShort(spawnExisting, 0);
                spawnExisting.push_back(0x7F);

                sendPacket(clientFd, 0x0C, spawnExisting);
            }
        }

        Core::Logger::info("Player " + username + " connected and spawned in the world !");
    }

    void NetworkManager::broadcastPacket(int32_t packetId, const std::vector<uint8_t> &payload, int ignoreFd)
    {
        for (const auto &[fd, session] : m_clients)
        {
            if (session.state == ClientState::PLAY && fd != ignoreFd)
            {
                sendPacket(fd, packetId, payload);
            }
        }
    }
    // Sends the Entity Equipment packet (0x04) to other players
    void NetworkManager::broadcastEquipment(int entityId, int16_t itemSlot, int16_t itemId)
    {
        std::vector<uint8_t> packet;
        Packet::writeVarInt(packet, entityId); // Entity ID holding the item
        Packet::writeShort(packet, itemSlot);  // 0 = Main hand (Held item)

        // Item Slot structure in 1.8:
        Packet::writeShort(packet, itemId); // e.g., 276 for Diamond Sword, -1 for empty hand
        if (itemId != -1)
        {
            packet.push_back(1);           // Quantity
            Packet::writeShort(packet, 0); // Damage / Metadata
            packet.push_back(0);           // NBT Tag (0 = no NBT)
        }

        // Broadcast to everyone except the player holding the item
        broadcastPacket(0x04, packet, entityId);
    }

    void NetworkManager::handleClientData(int clientFd)
    {
        auto it = m_clients.find(clientFd);
        if (it == m_clients.end())
            return;

        auto &session = m_clients[clientFd];
        uint8_t buf[2048];
        ssize_t bytes = read(clientFd, buf, sizeof(buf));

        if (bytes <= 0)
        {
            Core::Logger::info("Client " + std::to_string(clientFd) + " disconnected.");
            if (m_clients[clientFd].state == ClientState::PLAY)
            {
                std::vector<uint8_t> destroy;
                Packet::writeVarInt(destroy, 1);        // 1 entity
                Packet::writeVarInt(destroy, clientFd); // Entity ID = Client FD

                broadcastPacket(0x13, destroy, clientFd);
            }
            epoll_ctl(m_epollFd, EPOLL_CTL_DEL, clientFd, nullptr);
            close(clientFd);
            m_clients.erase(clientFd);
            return; // Mandatory stop here!
        }

        size_t offset = 0;
        while (offset < static_cast<size_t>(bytes))
        {
            size_t packetStart = offset;
            (void)packetStart;
            int32_t packetLen = Packet::readVarInt(buf, offset);
            if (packetLen <= 0 || (offset + packetLen) > static_cast<size_t>(bytes))
                break;

            size_t dataStart = offset;
            int32_t packetId = Packet::readVarInt(buf, offset);

            if (session.state == ClientState::HANDSHAKE)
            {
                if (packetId == 0x00)
                {
                    int32_t protoVersion = Packet::readVarInt(buf, offset);
                    (void)protoVersion;
                    std::string host = Packet::readString(buf, offset);
                    uint16_t port = (buf[offset] << 8) | buf[offset + 1];
                    (void)port;
                    offset += 2;
                    int32_t nextState = Packet::readVarInt(buf, offset);

                    if (nextState == 1)
                        session.state = ClientState::STATUS;
                    else if (nextState == 2)
                        session.state = ClientState::LOGIN;
                }
            }
            else if (session.state == ClientState::STATUS)
            {
                if (packetId == 0x00)
                {
                    // Calcul du nombre de joueurs connectés en phase PLAY
                    int onlinePlayers = 0;
                    for (const auto &[fd, s] : m_clients)
                    {
                        if (s.state == ClientState::PLAY)
                            onlinePlayers++;
                    }

                    std::string statusJson = "{\"version\":{\"name\":\"LightweightMC 1.8\",\"protocol\":47},"
                                             "\"players\":{\"max\":100,\"online\":" +
                                             std::to_string(onlinePlayers) + "},"
                                                                             "\"description\":{\"text\":\"LightweightMC C++ Engine\"}}";

                    std::vector<uint8_t> payload;
                    Packet::writeString(payload, statusJson);
                    sendPacket(clientFd, 0x00, payload);
                }
                else if (packetId == 0x01)
                {
                    // Vérification de sécurité : s'assurer que les 8 octets de payload sont bien reçus
                    if (dataStart + packetLen >= offset + 8)
                    {
                        std::vector<uint8_t> payload(buf + offset, buf + offset + 8);
                        sendPacket(clientFd, 0x01, payload);
                    }
                }
            }
            else if (session.state == ClientState::LOGIN)
            {
                if (packetId == 0x00)
                {
                    session.username = Packet::readString(buf, offset);
                    Core::Logger::info("Login Start received for: " + session.username);

                    std::vector<uint8_t> loginSuccess;
                    Packet::writeString(loginSuccess, "00000000-0000-0000-0000-000000000000");
                    Packet::writeString(loginSuccess, session.username);
                    sendPacket(clientFd, 0x02, loginSuccess);

                    session.state = ClientState::PLAY;
                    sendPlayPackets(clientFd, session.username);
                }
            }
            else if (session.state == ClientState::PLAY)
            {
                if (packetId == 0x00)
                { // Keep Alive
                    std::vector<uint8_t> payload(buf + offset, buf + offset + packetLen - (offset - dataStart));
                    sendPacket(clientFd, 0x00, payload);
                }
                else if (packetId == 0x01)
                { // Chat Message (0x01)
                    std::string message = Packet::readString(buf, offset);

                    if (message == "/stats" || message == "/hud")
                    {
                        sendScoreboard(clientFd);
                        sendTablistHeaderFooter(clientFd);

                        std::vector<uint8_t> chatMsg;
                        Packet::writeString(chatMsg, "{\"text\":\"§a[Server] HUD and Scoreboard Activated !\"}");
                        chatMsg.push_back(1); // Position 1 = System message
                        sendPacket(clientFd, 0x02, chatMsg);
                    }
                }
                else if (packetId == 0x04)
                { // Player Position
                    session.x = Packet::readDouble(buf, offset);
                    session.y = Packet::readDouble(buf, offset);
                    session.z = Packet::readDouble(buf, offset);
                    std::vector<uint8_t> teleport;
                    Packet::writeVarInt(teleport, clientFd); // Entity ID
                    Packet::writeInt(teleport, static_cast<int32_t>(session.x * 32.0));
                    Packet::writeInt(teleport, static_cast<int32_t>(session.y * 32.0));
                    Packet::writeInt(teleport, static_cast<int32_t>(session.z * 32.0));
                    teleport.push_back(0); // Yaw
                    teleport.push_back(0); // Pitch
                    teleport.push_back(1); // On Ground

                    int pChunkX = static_cast<int>(std::floor(session.x / 16.0));
                    int pChunkZ = static_cast<int>(std::floor(session.z / 16.0));

                    if (pChunkX != session.currentChunkX || pChunkZ != session.currentChunkZ)
                    {
                        updateChunksAroundPlayer(clientFd, 5);
                        broadcastPacket(0x18, teleport, clientFd);
                    }
                }
                else if (packetId == 0x06)
                { // Player Position And Look
                    session.x = Packet::readDouble(buf, offset);
                    session.y = Packet::readDouble(buf, offset);
                    session.z = Packet::readDouble(buf, offset);
                    std::vector<uint8_t> teleport;
                    Packet::writeVarInt(teleport, clientFd); // Entity ID
                    Packet::writeInt(teleport, static_cast<int32_t>(session.x * 32.0));
                    Packet::writeInt(teleport, static_cast<int32_t>(session.y * 32.0));
                    Packet::writeInt(teleport, static_cast<int32_t>(session.z * 32.0));
                    teleport.push_back(0); // Yaw
                    teleport.push_back(0); // Pitch
                    teleport.push_back(1); // On Ground

                    int pChunkX = static_cast<int>(std::floor(session.x / 16.0));
                    int pChunkZ = static_cast<int>(std::floor(session.z / 16.0));

                    if (pChunkX != session.currentChunkX || pChunkZ != session.currentChunkZ)
                    {
                        updateChunksAroundPlayer(clientFd, 5);
                    }

                    std::vector<uint8_t> keepAlivePayload;
                    Packet::writeVarInt(keepAlivePayload, 12345);
                    sendPacket(clientFd, 0x00, keepAlivePayload);
                    broadcastPacket(0x18, teleport, clientFd);
                }
                else if (packetId == 0x07)
                { // Player Digging (0x07)
                    int32_t status = Packet::readVarInt(buf, offset);

                    uint64_t val = Packet::readUInt64(buf, offset);
                    int32_t x = static_cast<int32_t>(val >> 38);
                    int32_t y = static_cast<int32_t>((val >> 26) & 0xFFF);
                    int32_t z = static_cast<int32_t>(val << 38 >> 38);

                    if (x >= (1 << 25))
                        x -= (1 << 26);
                    if (y >= (1 << 11))
                        y -= (1 << 12);
                    if (z >= (1 << 25))
                        z -= (1 << 26);

                    uint8_t face = buf[offset++];
                    (void)face;

                    if (status == 0 || status == 2)
                    {
                        int chunkX = Packet::floorDiv(x, 16.0);
                        int chunkZ = Packet::floorDiv(z, 16.0);
                        int relX = Packet::floorMod(x, 16.0);
                        int relZ = Packet::floorMod(z, 16.0);

                        m_worldStorage.saveBlockChange(chunkX, chunkZ, relX, y, relZ, 0);

                        std::vector<uint8_t> blockChange;

                        uint64_t posEnc = ((static_cast<uint64_t>(x) & 0x3FFFFFF) << 38) |
                                          ((static_cast<uint64_t>(y) & 0xFFF) << 26) |
                                          (static_cast<uint64_t>(z) & 0x3FFFFFF);

                        for (int i = 7; i >= 0; --i)
                            blockChange.push_back((posEnc >> (i * 8)) & 0xFF);
                        Packet::writeVarInt(blockChange, 0); // ID 0 = Air

                        broadcastPacket(0x23, blockChange);
                    }
                }
                else if (packetId == 0x08)
                { // Player Block Placement (0x08)
                    uint64_t val = Packet::readUInt64(buf, offset);
                    int32_t x = static_cast<int32_t>(val >> 38);
                    int32_t y = static_cast<int32_t>((val >> 26) & 0xFFF);
                    int32_t z = static_cast<int32_t>(val << 38 >> 38);

                    if (x >= (1 << 25))
                        x -= (1 << 26);
                    if (y >= (1 << 11))
                        y -= (1 << 12);
                    if (z >= (1 << 25))
                        z -= (1 << 26);

                    uint8_t face = buf[offset++];

                    int16_t itemSlot = Packet::readShort(buf, offset);

                    if (itemSlot > 0 && itemSlot < 256)
                    {
                        uint8_t itemCount = buf[offset++];
                        (void)itemCount;
                        uint16_t itemDamage = Packet::readShort(buf, offset);

                        if (face == 0)
                            y--;
                        else if (face == 1)
                            y++;
                        else if (face == 2)
                            z--;
                        else if (face == 3)
                            z++;
                        else if (face == 4)
                            x--;
                        else if (face == 5)
                            x++;

                        int chunkX = Packet::floorDiv(x, 16);
                        int chunkZ = Packet::floorDiv(z, 16);
                        int relX = Packet::floorMod(x, 16);
                        int relZ = Packet::floorMod(z, 16);

                        uint16_t blockData = (static_cast<uint16_t>(itemSlot) << 4) | (itemDamage & 0x0F);

                        m_worldStorage.saveBlockChange(chunkX, chunkZ, relX, y, relZ, blockData);

                        std::vector<uint8_t> blockChange;
                        uint64_t posEnc = ((static_cast<uint64_t>(x) & 0x3FFFFFF) << 38) |
                                          ((static_cast<uint64_t>(y) & 0xFFF) << 26) |
                                          (static_cast<uint64_t>(z) & 0x3FFFFFF);

                        for (int i = 7; i >= 0; --i)
                            blockChange.push_back((posEnc >> (i * 8)) & 0xFF);
                        Packet::writeVarInt(blockChange, blockData);

                        broadcastPacket(0x23, blockChange);
                    }
                }
                else if (packetId == 0x09)
                {
                    int16_t slot = Packet::readShort(buf, offset);

                    session.selectedSlot = slot;

                    std::vector<uint8_t> equipPacket;
                    Packet::writeVarInt(equipPacket, clientFd);
                    Packet::writeShort(equipPacket, 0);

                    int16_t itemId = session.inventory[36 + slot].id;

                    Packet::writeShort(equipPacket, itemId);
                    if (itemId != -1)
                    {
                        equipPacket.push_back(session.inventory[36 + slot].count);
                        Packet::writeShort(equipPacket, session.inventory[36 + slot].damage);
                        equipPacket.push_back(0);
                    }

                    broadcastPacket(0x04, equipPacket, clientFd);
                }
                else if (packetId == 0x10)
                {
                    int16_t slot = Packet::readShort(buf, offset);
                    int16_t itemId = Packet::readShort(buf, offset);

                    if (itemId != -1)
                    {
                        uint8_t count = buf[offset++];
                        int16_t damage = Packet::readShort(buf, offset);
                        if (buf[offset] != 0)
                        {
                        }

                        session.inventory[slot] = {itemId, count, damage};
                    }
                    else
                    {
                        session.inventory[slot] = {-1, 0, 0};
                    }

                    if (slot == (36 + session.selectedSlot))
                    {
                        std::vector<uint8_t> equipPacket;
                        Packet::writeVarInt(equipPacket, clientFd);
                        Packet::writeShort(equipPacket, 0);

                        Packet::writeShort(equipPacket, itemId);
                        if (itemId != -1)
                        {
                            equipPacket.push_back(session.inventory[slot].count);
                            Packet::writeShort(equipPacket, session.inventory[slot].damage);
                            equipPacket.push_back(0);
                        }

                        broadcastPacket(0x04, equipPacket, clientFd);
                    }
                }
                else if (packetId == 0x0A)
                {
                    std::vector<uint8_t> animPacket;
                    Packet::writeVarInt(animPacket, clientFd);
                    animPacket.push_back(0);

                    broadcastPacket(0x0B, animPacket, clientFd);
                }
            }

            // Réalignement strict de la boucle sur la fin théorique du paquet
            offset = dataStart + packetLen;
        }
    }
    void NetworkManager::pollEvents(int timeoutMs)
    {
        int nfds = epoll_wait(m_epollFd, m_events, 64, timeoutMs);

        // 1. Network event processing
        for (int i = 0; i < nfds; ++i)
        {
            int clientFd = m_events[i].data.fd;

            if (clientFd == m_serverFd)
            {
                sockaddr_in clientAddr;
                socklen_t clientLen = sizeof(clientAddr);
                int newFd = accept(m_serverFd, (struct sockaddr *)&clientAddr, &clientLen);
                if (newFd >= 0)
                {
                    setNonBlocking(newFd);

                    // --- OPTIONS SYSTEME POUR LE VPS ---
                    // Envoie immédiatement les paquets sans attendre l'algorithme de Nagle
                    int flag = 1;
                    setsockopt(newFd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));

                    // Évite les pertes de paquets fragmentés sur les interfaces réseau virtuelles
                    int pmTu = IP_PMTUDISC_DONT;
                    setsockopt(newFd, IPPROTO_IP, IP_MTU_DISCOVER, &pmTu, sizeof(pmTu));
                    // ------------------------------------

                    epoll_event ev{};
                    // Passage en Level-Triggered (suppression de EPOLLET)
                    ev.events = EPOLLIN | EPOLLRDHUP;
                    ev.data.fd = newFd;
                    epoll_ctl(m_epollFd, EPOLL_CTL_ADD, newFd, &ev);

                    m_clients[newFd] = ClientSession{newFd, ClientState::HANDSHAKE, ""};
                }
            }
            else
            {
                handleClientData(clientFd);
            }
        }

        // 2. Update stats every 1000 ms (OUTSIDE the for loop)
        static auto lastUpdate = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate).count() >= 1000)
        {
            for (const auto &[fd, session] : m_clients)
            {
                if (session.state == ClientState::PLAY)
                {
                    sendScoreboard(fd);
                    sendTablistHeaderFooter(fd);
                }
            }
            lastUpdate = now;
        }
    }

    void NetworkManager::stop()
    {
        if (m_serverFd != -1)
            close(m_serverFd);
        if (m_epollFd != -1)
            close(m_epollFd);
    }

} // namespace LightweightMC::Network