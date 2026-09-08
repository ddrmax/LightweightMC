#include "network/NetworkManager.hpp"
#include "storage/WorldStorage.hpp"
#include "core/Logger.hpp"
#include "network/Packet.hpp"
#include <netinet/tcp.h>
#include <arpa/inet.h>
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
#define SECTION "\xC2\xA7"
#ifndef SERVER_VERSION
#define SERVER_VERSION "0.0.3-alpha"
#endif

#ifndef MC_PROTOCOL_VERSION_NAME
#define MC_PROTOCOL_VERSION_NAME "1.8"
#endif
namespace LightweightMC::Network
{

    void NetworkManager::setNonBlocking(int fd)
    {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    void NetworkManager::sendPacket(int fd, int32_t packetId, const std::vector<uint8_t> &payload)
    {
        auto it = m_clients.find(fd);
        if (it == m_clients.end())
        {
            return; // Session non trouv�e / d�j� ferm�e
        }

        ClientSession &session = it->second;

        std::vector<uint8_t> body;
        Packet::writeVarInt(body, packetId);
        body.insert(body.end(), payload.begin(), payload.end());

        std::vector<uint8_t> frame;
        Packet::writeVarInt(frame, static_cast<int32_t>(body.size()));
        frame.insert(frame.end(), body.begin(), body.end());
        session.sendBuffer.insert(session.sendBuffer.end(), frame.begin(), frame.end());
        flushSendBuffer(session);

        /* std::string logMsg = "Sent packet ID " + std::to_string(packetId) + " (size: " + std::to_string(frame.size()) + " bytes)";
         LightweightMC::Core::Logger::info(logMsg);*/
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
        std::string title = SECTION "e" SECTION "lLIGHTWEIGHT MC";

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
            SECTION "fPlayers: " SECTION "a" + std::to_string(onlinePlayers),
            SECTION "fLoaded Chunks: " SECTION "e" + std::to_string(totalGlobalChunks),
            SECTION "fRAM: " SECTION "b" + ramStream.str() + " Mo",
            SECTION "fCPU Load: " SECTION "c" + cpuStream.str()};

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
        std::string headerJson = R"({"text": ")" SECTION "e" SECTION R"(lLightweightMC Server\n)" SECTION R"(7Real Time Status"})";

        double ram = getProcessRAM();
        std::stringstream ramStream;
        ramStream << std::fixed << std::setprecision(1) << ram;

        std::string footerJson = R"({"text": ")" SECTION "fRAM Proc: " SECTION "b" + ramStream.str() + " Mo " SECTION "7| " SECTION "fPlayers: " SECTION "a" + std::to_string(m_clients.size()) + R"("})";

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

        auto &session = it->second;
        uint8_t buf[4096];
        ssize_t bytes = read(clientFd, buf, sizeof(buf));

        if (bytes <= 0)
        {
            Core::Logger::info("Client " + std::to_string(clientFd) + " disconnected.");
            if (session.state == ClientState::PLAY)
            {
                std::vector<uint8_t> destroy;
                Packet::writeVarInt(destroy, 1);        // 1 entity
                Packet::writeVarInt(destroy, clientFd); // Entity ID = Client FD

                broadcastPacket(0x13, destroy, clientFd);
            }
            epoll_ctl(m_epollFd, EPOLL_CTL_DEL, clientFd, nullptr);
            close(clientFd);
            m_clients.erase(it);
            return; // Mandatory stop here!
        }
        if (m_clients.find(clientFd) == m_clients.end())
        {
            return;
        }
        session.rxBuffer.insert(session.rxBuffer.end(), buf, buf + bytes);

        size_t offset = 0;
        while (offset < session.rxBuffer.size())
        {
            size_t tempOffset = offset;
            int32_t packetLen = Packet::readVarInt(session.rxBuffer.data(), tempOffset);
            size_t headerSize = tempOffset - offset;
            if (packetLen <= 0 || (offset + headerSize + packetLen) > session.rxBuffer.size())
            {
                break;
            }

            offset = tempOffset;
            size_t dataStart = offset;
            const uint8_t *data = session.rxBuffer.data();

            int32_t packetId = Packet::readVarInt(data, offset);

            if (session.state == ClientState::HANDSHAKE)
            {
                if (packetId == 0x00)
                {
                    int32_t protoVersion = Packet::readVarInt(data, offset);
                    (void)protoVersion;
                    std::string host = Packet::readString(data, offset);
                    uint16_t port = (data[offset] << 8) | data[offset + 1];
                    (void)port;
                    offset += 2;
                    int32_t nextState = Packet::readVarInt(data, offset);

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
                    int onlinePlayers = 0;
                    for (const auto &[fd, s] : m_clients)
                    {
                        if (s.state == ClientState::PLAY)
                            onlinePlayers++;
                    }

                    std::string statusJson = "{\"version\":{\"name\":\"LightweightMC 1.8\",\"protocol\":47},"
                                             "\"players\":{\"max\":100,\"online\":" +
                                             std::to_string(onlinePlayers) + "},"
                                                                             "\"description\":{\"text\":\"LightweightMC C++ Engine V0.0.3\"}}";

                    std::vector<uint8_t> payload;
                    Packet::writeString(payload, statusJson);
                    sendPacket(clientFd, 0x00, payload);
                }
                else if (packetId == 0x01)
                {
                    if (dataStart + packetLen >= offset + 8)
                    {
                        std::vector<uint8_t> payload(data + offset, data + offset + 8);
                        sendPacket(clientFd, 0x01, payload);
                    }
                }
            }
            else if (session.state == ClientState::LOGIN)
            {
                if (packetId == 0x00)
                {
                    session.username = Packet::readString(data, offset);
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
                    size_t payloadSize = packetLen - (offset - dataStart);
                    std::vector<uint8_t> payload(data + offset, data + offset + payloadSize);
                    sendPacket(clientFd, 0x00, payload);
                }
                else if (packetId == 0x01) // Chat Message (Client -> Server)
                {
                    std::string message = Packet::readString(data, offset);

                    // 1. GESTION DES COMMANDES (commencent par '/')
                    if (!message.empty() && message[0] == '/')
                    {
                        std::istringstream iss(message);
                        std::string command;
                        iss >> command;

                        // Extraction des arguments
                        std::vector<std::string> args;
                        std::string arg;
                        while (iss >> arg)
                        {
                            args.push_back(arg);
                        }

                        // --- Commande : /stats ou /hud ---
                        if (command == "/stats" || command == "/hud")
                        {
                            sendScoreboard(clientFd);
                            sendTablistHeaderFooter(clientFd);

                            std::vector<uint8_t> chatMsg;
                            Packet::writeString(chatMsg, R"({"text":")" SECTION R"(a[Server] HUD and Scoreboard Activated !"})");
                            chatMsg.push_back(1); // Position 1 = System message
                            sendPacket(clientFd, 0x02, chatMsg);
                        }
                        // --- Commande : /version ou /ver ou /about ---
                        else if (command == "/version" || command == "/ver" || command == "/about")
                        {
                            std::string verInfo = SECTION "aThis server is running " SECTION "lLightweightMC " SECTION "av" SERVER_VERSION "\n" SECTION "7Target Protocol: " SECTION "fMinecraft " MC_PROTOCOL_VERSION_NAME;

                            std::vector<uint8_t> chatMsg;
                            Packet::writeString(chatMsg, R"({"text":")" + verInfo + R"("})");
                            chatMsg.push_back(1); // Position 1 = System message
                            sendPacket(clientFd, 0x02, chatMsg);
                        }
                        // --- Commande : /ping ---
                        else if (command == "/ping")
                        {
                            std::vector<uint8_t> chatMsg;
                            Packet::writeString(chatMsg, R"({"text":")" SECTION R"(aPong !"})");
                            chatMsg.push_back(1);
                            sendPacket(clientFd, 0x02, chatMsg);
                        }
                        // --- Commande : /list ---
                        else if (command == "/list")
                        {
                            std::string listStr = SECTION "aPlayers online (" + std::to_string(m_clients.size()) + "): " SECTION "f";
                            bool first = true;
                            for (const auto &[fd, s] : m_clients)
                            {
                                if (s.state == ClientState::PLAY)
                                {
                                    if (!first)
                                        listStr += ", ";
                                    listStr += s.username;
                                    first = false;
                                }
                            }

                            std::vector<uint8_t> chatMsg;
                            Packet::writeString(chatMsg, R"({"text":")" + listStr + R"("})");
                            chatMsg.push_back(1);
                            sendPacket(clientFd, 0x02, chatMsg);
                        }
                        // --- Commande : /tell, /w, /msg (Chuchotement) ---
                        else if (command == "/tell" || command == "/w" || command == "/msg")
                        {
                            if (args.size() < 2)
                            {
                                std::string usage = R"({"text":")" SECTION R"(cUsage: )" + command + R"( <player> <message>"})";
                                std::vector<uint8_t> chatMsg;
                                Packet::writeString(chatMsg, usage);
                                chatMsg.push_back(1); // Type: System Message / Chat
                                sendPacket(clientFd, 0x02, chatMsg);
                            }
                            else
                            {
                                std::string targetName = args[0];

                                // Reconstitution du message complet à partir de args[1]
                                std::string message;
                                for (size_t i = 1; i < args.size(); ++i)
                                {
                                    if (i > 1)
                                        message += " ";
                                    message += args[i];
                                }

                                // Recherche du destinataire par son pseudo
                                int targetFd = -1;
                                for (const auto &[fd, session] : m_clients)
                                {
                                    if (session.username == targetName)
                                    {
                                        targetFd = fd;
                                        break;
                                    }
                                }

                                if (targetFd != -1)
                                {
                                    std::string senderName = m_clients[clientFd].username;

                                    // Message pour le destinataire
                                    std::string targetPayload = R"({"text":")" SECTION R"(d)" + senderName + R"( whispers to you : )" + message + R"("})";
                                    std::vector<uint8_t> targetPacket;
                                    Packet::writeString(targetPacket, targetPayload);
                                    targetPacket.push_back(1);
                                    sendPacket(targetFd, 0x02, targetPacket);

                                    // Confirmation pour l'expéditeur
                                    std::string senderPayload = R"({"text":")" SECTION R"(dÀ )" + targetName + R"( : )" + message + R"("})";
                                    std::vector<uint8_t> senderPacket;
                                    Packet::writeString(senderPacket, senderPayload);
                                    senderPacket.push_back(1);
                                    sendPacket(clientFd, 0x02, senderPacket);
                                }
                                else
                                {
                                    // Joueur introuvable
                                    std::string errPayload = R"({"text":")" SECTION R"(cPlayer Not Found."})";
                                    std::vector<uint8_t> errPacket;
                                    Packet::writeString(errPacket, errPayload);
                                    errPacket.push_back(1);
                                    sendPacket(clientFd, 0x02, errPacket);
                                }
                            }
                        }
                        // --- Commande : /tp ou /teleport ---
                        else if (command == "/tp" || command == "/teleport")
                        {
                            // Cas 1 : /tp <x> <y> <z>
                            if (args.size() == 3)
                            {
                                try
                                {
                                    double x = std::stod(args[0]);
                                    double y = std::stod(args[1]);
                                    double z = std::stod(args[2]);

                                    teleportPlayer(clientFd, x, y, z);

                                    std::vector<uint8_t> chatMsg;
                                    Packet::writeString(chatMsg, R"({"text":")" SECTION R"(aTeleported to )" + args[0] + " " + args[1] + " " + args[2] + R"("})");
                                    chatMsg.push_back(1);
                                    sendPacket(clientFd, 0x02, chatMsg);
                                }
                                catch (...)
                                {
                                    std::vector<uint8_t> chatMsg;
                                    Packet::writeString(chatMsg, R"({"text":")" SECTION R"(cInvalid coordinates. Usage: /tp <x> <y> <z>"})");
                                    chatMsg.push_back(1);
                                    sendPacket(clientFd, 0x02, chatMsg);
                                }
                            }
                            // Cas 2 : /tp <joueur> (Téléportation vers un autre joueur)
                            else if (args.size() == 1)
                            {
                                std::string targetName = args[0];
                                int targetFd = -1;
                                double tx = 0, ty = 0, tz = 0;

                                for (const auto &[fd, s] : m_clients)
                                {
                                    if (s.state == ClientState::PLAY && s.username == targetName)
                                    {
                                        targetFd = fd;
                                        tx = s.x;
                                        ty = s.y;
                                        tz = s.z;
                                        break;
                                    }
                                }

                                if (targetFd != -1)
                                {
                                    teleportPlayer(clientFd, tx, ty, tz);

                                    std::vector<uint8_t> chatMsg;
                                    Packet::writeString(chatMsg, R"({"text":")" SECTION R"(aTeleported to )" + targetName + R"("})");
                                    chatMsg.push_back(1);
                                    sendPacket(clientFd, 0x02, chatMsg);
                                }
                                else
                                {
                                    std::vector<uint8_t> chatMsg;
                                    Packet::writeString(chatMsg, R"({"text":")" SECTION R"(cPlayer not found."})");
                                    chatMsg.push_back(1);
                                    sendPacket(clientFd, 0x02, chatMsg);
                                }
                            }
                            // Cas 3 : /tp <joueur1> <joueur2> (Téléporter joueur1 sur joueur2)
                            else if (args.size() == 2)
                            {
                                int p1Fd = -1, p2Fd = -1;
                                double p2x = 0, p2y = 0, p2z = 0;

                                for (const auto &[fd, s] : m_clients)
                                {
                                    if (s.state == ClientState::PLAY)
                                    {
                                        if (s.username == args[0])
                                            p1Fd = fd;
                                        if (s.username == args[1])
                                        {
                                            p2Fd = fd;
                                            p2x = s.x;
                                            p2y = s.y;
                                            p2z = s.z;
                                        }
                                    }
                                }

                                if (p1Fd != -1 && p2Fd != -1)
                                {
                                    teleportPlayer(p1Fd, p2x, p2y, p2z);

                                    std::vector<uint8_t> chatMsg;
                                    Packet::writeString(chatMsg, R"({"text":")" SECTION R"(aTeleported )" + args[0] + R"( to )" + args[1] + R"("})");
                                    chatMsg.push_back(1);
                                    sendPacket(clientFd, 0x02, chatMsg);
                                }
                                else
                                {
                                    std::vector<uint8_t> chatMsg;
                                    Packet::writeString(chatMsg, R"({"text":")" SECTION R"(cOne or both players not found."})");
                                    chatMsg.push_back(1);
                                    sendPacket(clientFd, 0x02, chatMsg);
                                }
                            }
                            else
                            {
                                std::vector<uint8_t> chatMsg;
                                Packet::writeString(chatMsg, R"({"text":")" SECTION R"(cUsage: /tp <x> <y> <z> OR /tp <player> OR /tp <player1> <player2>"})");
                                chatMsg.push_back(1);
                                sendPacket(clientFd, 0x02, chatMsg);
                            }
                        }
                        // --- Commande : /tpall (Téléporte tous les joueurs sur soi) ---
                        else if (command == "/tpall")
                        {
                            double myX = session.x, myY = session.y, myZ = session.z;
                            int count = 0;

                            for (const auto &[fd, s] : m_clients)
                            {
                                if (s.state == ClientState::PLAY && fd != clientFd)
                                {
                                    teleportPlayer(fd, myX, myY, myZ);

                                    std::vector<uint8_t> chatTarget;
                                    Packet::writeString(chatTarget, R"({"text":")" SECTION R"(aTeleported to )" + session.username + R"("})");
                                    chatTarget.push_back(1);
                                    sendPacket(fd, 0x02, chatTarget);

                                    count++;
                                }
                            }

                            std::vector<uint8_t> chatMsg;
                            Packet::writeString(chatMsg, R"({"text":")" SECTION R"(aTeleported )" + std::to_string(count) + R"( players to you."})");
                            chatMsg.push_back(1);
                            sendPacket(clientFd, 0x02, chatMsg);
                        }
                        // --- Commande : /help ---
                        else if (command == "/help" || command == "/?")
                        {
                            std::string helpText = SECTION "e--- LightweightMC Help ---\n" SECTION "f/help " SECTION "7- Displays this help menu\n" SECTION "f/list " SECTION "7- Lists all connected players\n" SECTION "f/ping " SECTION "7- Tests server latency\n" SECTION "f/hud " SECTION "7or " SECTION "f/stats " SECTION "7- Shows HUD & Scoreboard\n" SECTION "f/tell <player> <msg> " SECTION "7- Sends a private message";

                            std::vector<uint8_t> chatMsg;
                            Packet::writeString(chatMsg, R"({"text":")" + helpText + R"("})");
                            chatMsg.push_back(1); // Position 1 = System message
                            sendPacket(clientFd, 0x02, chatMsg);
                        }
                        // --- Commande inconnue ---
                        else
                        {
                            std::vector<uint8_t> chatMsg;
                            Packet::writeString(chatMsg, R"({"text":")" SECTION R"(cUnknown command. Type /help for a list of commands."})");
                            chatMsg.push_back(1);
                            sendPacket(clientFd, 0x02, chatMsg);
                        }
                    }
                    // 2. CHAT STANDARD (Broadcast à tous les joueurs connectés en état PLAY)
                    else
                    {
                        std::string formattedChat = "<" + session.username + "> " + message;

                        std::vector<uint8_t> chatMsg;
                        Packet::writeString(chatMsg, R"({"text":")" + formattedChat + R"("})");
                        chatMsg.push_back(0); // Position 0 = Chat box standard

                        for (const auto &[fd, s] : m_clients)
                        {
                            if (s.state == ClientState::PLAY)
                            {
                                sendPacket(fd, 0x02, chatMsg);
                            }
                        }

                        LightweightMC::Core::Logger::info("[CHAT] " + formattedChat);
                    }
                }
                else if (packetId == 0x04)
                { // Player Position
                    // --------
                    /*
                    std::string hexDump = "";
                    char buf[4];

                    // Utilisation de la taille r�elle du rxBuffer pour ne pas lire au-del�
                    for (size_t i = offset; i < session.rxBuffer.size(); ++i) {
                        std::snprintf(buf, sizeof(buf), "%02X ", data[i]);
                        hexDump += buf;
                    }
                    Core::Logger::info("Packet 0x0" + std::to_string(packetId) + " Brut (HEX): [ " + hexDump + "]");
                    // ------------------------------------------
                    */
                    session.x = Packet::readDouble(data, offset);
                    session.y = Packet::readDouble(data, offset);
                    session.z = Packet::readDouble(data, offset);
                    std::vector<uint8_t> teleport;
                    Packet::writeVarInt(teleport, clientFd);
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
                    /*std::vector<uint8_t> keepAlivePayload;
                    Packet::writeVarInt(keepAlivePayload, 12345);
                    sendPacket(clientFd, 0x00, keepAlivePayload);*/

                    broadcastPacket(0x18, teleport, clientFd);
                    // Log avec position pour le paquet 0x04
                    /* Core::Logger::info("Client " + std::to_string(clientFd) + " moved (P4) to X: " +
                                        std::to_string(session.x) + " Y: " + std::to_string(session.y) + " Z: " + std::to_string(session.z));*/
                }
                else if (packetId == 0x06)
                { // Player Position And Look
                    // ----  ----
                    /*
                    std::string hexDump = "";
                    char buf[4];

                    // Utilisation de la taille r�elle du rxBuffer pour ne pas lire au-del�
                    for (size_t i = offset; i < session.rxBuffer.size(); ++i) {
                        std::snprintf(buf, sizeof(buf), "%02X ", data[i]);
                        hexDump += buf;
                    }
                    Core::Logger::info("Packet 0x0" + std::to_string(packetId) + " Brut (HEX): [ " + hexDump + "]");
                    // ------------------------------------------
                    */
                    session.x = Packet::readDouble(data, offset);
                    session.y = Packet::readDouble(data, offset);
                    session.z = Packet::readDouble(data, offset);
                    std::vector<uint8_t> teleport;
                    Packet::writeVarInt(teleport, clientFd);
                    Packet::writeInt(teleport, static_cast<int32_t>(session.x * 32.0));
                    Packet::writeInt(teleport, static_cast<int32_t>(session.y * 32.0));
                    Packet::writeInt(teleport, static_cast<int32_t>(session.z * 32.0));
                    teleport.push_back(0);
                    teleport.push_back(0);
                    teleport.push_back(1);

                    int pChunkX = static_cast<int>(std::floor(session.x / 16.0));
                    int pChunkZ = static_cast<int>(std::floor(session.z / 16.0));

                    if (pChunkX != session.currentChunkX || pChunkZ != session.currentChunkZ)
                    {
                        updateChunksAroundPlayer(clientFd, 5);
                    }

                    /*std::vector<uint8_t> keepAlivePayload;
                    Packet::writeVarInt(keepAlivePayload, 12345);
                    sendPacket(clientFd, 0x00, keepAlivePayload);*/
                    broadcastPacket(0x18, teleport, clientFd);
                    // Log avec position pour le paquet 0x06
                    /* Core::Logger::info("Client " + std::to_string(clientFd) + " moved (P6) to X: " +
                                        std::to_string(session.x) + " Y: " + std::to_string(session.y) + " Z: " + std::to_string(session.z));  */
                }
                else if (packetId == 0x07)
                { // Player Digging
                    int32_t status = Packet::readVarInt(data, offset);

                    uint64_t val = Packet::readUInt64(data, offset);
                    int32_t x = static_cast<int32_t>(val >> 38);
                    int32_t y = static_cast<int32_t>((val >> 26) & 0xFFF);
                    int32_t z = static_cast<int32_t>(val << 38 >> 38);

                    if (x >= (1 << 25))
                        x -= (1 << 26);
                    if (y >= (1 << 11))
                        y -= (1 << 12);
                    if (z >= (1 << 25))
                        z -= (1 << 26);

                    uint8_t face = data[offset++];
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
                        Packet::writeVarInt(blockChange, 0); // Air

                        broadcastPacket(0x23, blockChange);
                    }
                }
                else if (packetId == 0x08)
                { // Player Block Placement
                    uint64_t val = Packet::readUInt64(data, offset);
                    int32_t x = static_cast<int32_t>(val >> 38);
                    int32_t y = static_cast<int32_t>((val >> 26) & 0xFFF);
                    int32_t z = static_cast<int32_t>(val << 38 >> 38);

                    if (x >= (1 << 25))
                        x -= (1 << 26);
                    if (y >= (1 << 11))
                        y -= (1 << 12);
                    if (z >= (1 << 25))
                        z -= (1 << 26);

                    uint8_t face = data[offset++];
                    int16_t itemSlot = Packet::readShort(data, offset);

                    if (itemSlot > 0 && itemSlot < 256)
                    {
                        uint8_t itemCount = data[offset++];
                        (void)itemCount;
                        uint16_t itemDamage = Packet::readShort(data, offset);

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
                    int16_t slot = Packet::readShort(data, offset);

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
                    int16_t slot = Packet::readShort(data, offset);
                    int16_t itemId = Packet::readShort(data, offset);

                    if (itemId != -1)
                    {
                        uint8_t count = data[offset++];
                        int16_t damage = Packet::readShort(data, offset);
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

            // Alignement strict sur la fin th�orique du paquet
            offset = dataStart + packetLen;
        }

        // Purge des donn�es trait�es du buffer
        if (offset > 0)
        {
            session.rxBuffer.erase(session.rxBuffer.begin(), session.rxBuffer.begin() + offset);
        }
    }
    void NetworkManager::pollEvents(int timeoutMs)
    {
        int nfds = epoll_wait(m_epollFd, m_events, 64, timeoutMs);

        // 1. Network event processing
        for (int i = 0; i < nfds; ++i)
        {
            int clientFd = m_events[i].data.fd;
            uint32_t events = m_events[i].events;

            if (clientFd == m_serverFd)
            {
                sockaddr_in clientAddr{};
                socklen_t clientLen = sizeof(clientAddr);
                int newFd = accept(m_serverFd, (struct sockaddr *)&clientAddr, &clientLen);
                if (newFd >= 0)
                {
                    setNonBlocking(newFd);

                    // --- OPTIONS SYSTEME POUR LE VPS ---
                    int flag = 1;
                    setsockopt(newFd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));

                    int pmTu = IP_PMTUDISC_DONT;
                    setsockopt(newFd, IPPROTO_IP, IP_MTU_DISCOVER, &pmTu, sizeof(pmTu));
                    // ------------------------------------

                    epoll_event ev{};
                    ev.events = EPOLLIN | EPOLLRDHUP;
                    ev.data.fd = newFd;
                    epoll_ctl(m_epollFd, EPOLL_CTL_ADD, newFd, &ev);

                    m_clients[newFd] = ClientSession{newFd, ClientState::HANDSHAKE, ""};
                }
            }
            else
            {
                auto it = m_clients.find(clientFd);
                if (it == m_clients.end())
                {
                    continue; // La session a d�j� �t� supprim�e
                }

                // 1.1 D�connexion ou erreur socket
                if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
                {
                    handleDisconnect(clientFd);
                    continue;
                }

                // 1.2 Socket pr�t pour l'�criture (vidage du buffer pendatif)
                if (events & EPOLLOUT)
                {
                    flushSendBuffer(it->second);
                }

                // V�rifier si la session existe toujours (flushSendBuffer a pu la d�connecter en cas d'erreur)
                if (m_clients.find(clientFd) == m_clients.end())
                {
                    continue;
                }

                // 1.3 Socket pr�t pour la lecture
                if (events & EPOLLIN)
                {
                    handleClientData(clientFd);
                }
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
    void NetworkManager::handleDisconnect(int fd)
    {
        auto it = m_clients.find(fd);
        if (it != m_clients.end())
        {
            sockaddr_in addr{};
            socklen_t addrLen = sizeof(addr);
            std::string ipStr = "Unknown IP";
            int port = 0;

            if (getpeername(fd, (struct sockaddr *)&addr, &addrLen) == 0)
            {
                char ipBuf[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, &addr.sin_addr, ipBuf, sizeof(ipBuf)))
                {
                    ipStr = ipBuf;
                }
                port = ntohs(addr.sin_port);
            }
            std::string username = it->second.username.empty() ? "Unauthenticated Client" : it->second.username;
            LightweightMC::Core::Logger::info("Client " + std::to_string(fd) + " (" + username + ") disconnected. [IP: " + ipStr + ":" + std::to_string(port) + "]");
            epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, nullptr);
            close(fd);

            m_clients.erase(it);
        }
    }

    void NetworkManager::flushSendBuffer(ClientSession &session)
    {
        while (!session.sendBuffer.empty())
        {
            ssize_t sent = send(session.fd, session.sendBuffer.data(), session.sendBuffer.size(), MSG_NOSIGNAL);

            if (sent > 0)
            {
                session.sendBuffer.erase(session.sendBuffer.begin(), session.sendBuffer.begin() + sent);
            }
            else if (sent < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    epoll_event ev{};
                    ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
                    ev.data.fd = session.fd;
                    epoll_ctl(m_epollFd, EPOLL_CTL_MOD, session.fd, &ev);
                    return;
                }

                handleDisconnect(session.fd);
                return;
            }
        }
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLRDHUP;
        ev.data.fd = session.fd;
        epoll_ctl(m_epollFd, EPOLL_CTL_MOD, session.fd, &ev);
    }
    void NetworkManager::teleportPlayer(int fd, double x, double y, double z, float yaw, float pitch)
    {
        auto it = m_clients.find(fd);
        if (it == m_clients.end())
            return;

        ClientSession &session = it->second;

        // Mettre à jour les coordonnées dans la session serveur
        session.x = x;
        session.y = y;
        session.z = z;

        // Construction du paquet 0x08 (Player Position And Look)
        std::vector<uint8_t> packet;
        Packet::writeDouble(packet, x);
        Packet::writeDouble(packet, y);
        Packet::writeDouble(packet, z);
        Packet::writeFloat(packet, yaw);
        Packet::writeFloat(packet, pitch);
        packet.push_back(0x00); // Flags: 0x00 = Coordonnées absolues

        sendPacket(fd, 0x08, packet);
    }
} // namespace LightweightMC::Network