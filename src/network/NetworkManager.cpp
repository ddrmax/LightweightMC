#include "network/NetworkManager.hpp"
#include "storage/WorldStorage.hpp"
#include "core/ConfigManager.hpp"
#include "network/AntiScan.hpp"
#include "core/utils.hpp"
#include "core/Logger.hpp"
#include "network/Packet.hpp"
#include "managers/ScoreboardManager.hpp"
#include "managers/PlayerManager.hpp"
#include "network/protocol/ProtocolTranslator.hpp"
#include "network/protocol/EraAdapter.hpp"
#include "network/protocol/ProtocolVersion.hpp"

#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <cstring>
#include <cmath>
#include <csignal>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <mutex>

#ifndef SERVER_VERSION
#define SERVER_VERSION "0.0.4-alpha"
#endif

#ifndef MC_PROTOCOL_VERSION_NAME
#define MC_PROTOCOL_VERSION_NAME "1.8"
#endif

std::mutex m_clientsMutex;

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
            return;
        }

        LightweightMC::Player::PlayerSession &session = it->second;

        // Route the packet ID through this session's era adapter so multi-version
        // clients each get the correct wire ID. The literal IDs used throughout the
        // codebase are 1.8 wire IDs, so we look them up by (era, 1.8 wire ID) via the
        // generated reverse tables. Unmapped IDs (-1), or pre-handshake sessions with no
        // adapter yet, pass through unchanged.
        int32_t wireId = packetId;
        if (session.adapter != nullptr)
        {
            const int32_t mapped = Protocol::ProtocolVersion::s2cWireIdFromLegacy(session.adapter->era(), packetId);
            if (mapped >= 0)
                wireId = mapped;
        }

        std::vector<uint8_t> body;
        Packet::writeVarInt(body, wireId);
        body.insert(body.end(), payload.begin(), payload.end());

        std::vector<uint8_t> frame;
        Packet::writeVarInt(frame, static_cast<int32_t>(body.size()));
        frame.insert(frame.end(), body.begin(), body.end());
        session.sendBuffer.insert(session.sendBuffer.end(), frame.begin(), frame.end());
        flushSendBuffer(session);
    }

    bool NetworkManager::start(uint16_t port)
    {
        antiscan = ConfigManager::getInstance().getBool("security", "enable-antiscan", true);

        std::string url = ConfigManager::getInstance().getString("security", "blocklist-url");
        AntiScan::init(url);

        std::signal(SIGPIPE, SIG_IGN);

        if (!m_worldStorage.init("world.db"))
        {
            Core::Logger::error("Failed to initialize SQLite world storage.");
            return false;
        }

        if (!m_database.init())
        {
            Core::Logger::error("Failed to initialize SQLite player database.");
            return false;
        }

        // Select the world generation preset from server.properties ([world] gen-style)
        const std::string genStyle = ConfigManager::getInstance().getString("world", "gen-style", "overworld");
        if (genStyle == "superflat")
        {
            m_worldStorage.setGenerationStyle(Storage::GenStyle::SUPERFLAT);
            Core::Logger::info("World generation style: superflat (" + ConfigManager::getInstance().getString("world", "superflat-preset", "minecraft:flat") + ")");
        }
        else if (genStyle == "overworld")
        {
            m_worldStorage.setGenerationStyle(Storage::GenStyle::OVERWORLD);
            Core::Logger::info("World generation style: overworld (1 bedrock / 59 stone / 3 dirt / 1 grass)");
        }
        else
        {
            Core::Logger::warn("[NetworkManager] Unknown gen-style '" + genStyle + "', falling back to 'overworld'");
            m_worldStorage.setGenerationStyle(Storage::GenStyle::OVERWORLD);
        }

        m_worldStorage.pregenerateWorld(20);
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
            handleDisconnect(clientFd);
            return;
        }

        if (m_clients.find(clientFd) == m_clients.end())
        {
            return;
        }
        session.rxBuffer.insert(session.rxBuffer.end(), buf, buf + bytes);

        auto packetSender = [this](int f, int32_t p, const std::vector<uint8_t> &b)
        {
            sendPacket(f, p, b);
        };

        auto broadcastSender = [this](int32_t p, const std::vector<uint8_t> &b, int i)
        {
            broadcastPacket(p, b, i);
        };

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

            // Bounds-checked read helpers live in Packet (see Packet::read*Bounded).
            // Every field of a client packet is validated against `packetEnd` so that
            // malformed or truncated data can never make Packet::read* walk past the
            // buffer (stack/heap corruption).
            const size_t packetEnd = dataStart + static_cast<size_t>(packetLen);

            int32_t packetId = Packet::readVarInt(data, offset);

            if (session.state == ClientState::HANDSHAKE)
            {
                if (packetId == 0x00)
                {
                    int32_t protoVersion = Packet::readVarIntBounded(data, offset, packetEnd, 1);
                    std::string host = Packet::readStringBounded(data, offset, packetEnd);
                    uint16_t port = (data[offset] << 8) | data[offset + 1];
                    if (offset + 2 <= packetEnd)
                        offset += 2;
                    (void)port;
                    int32_t nextState = Packet::readVarIntBounded(data, offset, packetEnd, 1);

                    // Multi-version support: record the client's protocol number and
                    // bind it to the era adapter for that version. The adapter is a
                    // process-lifetime singleton (see EraAdapter::create), so the
                    // pointer is never freed while the session lives. A malformed
                    // handshake (protoVersion == -1) keeps the default 47/1.8 era.
                    if (protoVersion >= 0)
                        session.clientProtocol = protoVersion;
                    session.adapter = Protocol::EraAdapter::create(session.clientProtocol);

                    std::string ipPort = Core::getIpAndPortFromFd(clientFd);
                    Core::Logger::info("[NET] Handshake from " + ipPort + " protocol=" + std::to_string(session.clientProtocol) +
                                       (session.adapter ? "" : " [UNSUPPORTED ERA - legacy handlers only]"));

                    const Protocol::ProtocolVersionInfo *versionInfo =
                        Protocol::ProtocolVersion::resolve(session.clientProtocol);
                    if (versionInfo)
                    {
                        Core::Logger::info("Handshake from " + ipPort +
                                           " protocol=" + std::to_string(protoVersion) +
                                           " (" + versionInfo->version + ", era=" +
                                           Protocol::eraName(versionInfo->era) + ")");
                    }
                    else
                    {
                        const Protocol::ProtocolVersionInfo *nearest =
                            Protocol::ProtocolVersion::nearestSupported(protoVersion);
                        Core::Logger::info("Handshake from " + ipPort +
                                           " protocol=" + std::to_string(protoVersion) +
                                           " (unsupported; nearest supported: " +
                                           (nearest ? std::string(nearest->version) : std::string("?")) + ")");
                    }

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

                    std::string motd = SECTION "2" SECTION "n" SECTION "l" SECTION "o"
                                               "LightweightMC C++ Engine " SECTION "c" SECTION "n" SECTION "l" SECTION "o"
                                               "V0.0.5" SECTION "r";

                    double currentTps = 20.0;
                    double currentRam = LightweightMC::Core::getProcessRAM();

                    std::ostringstream tpsStream, ramStream;
                    tpsStream << std::fixed << std::setprecision(1) << currentTps;
                    ramStream << std::fixed << std::setprecision(1) << currentRam;

                    std::string tpsStr = tpsStream.str();
                    std::string ramStr = ramStream.str();

                    std::string sampleJson = R"([{"name": ")" SECTION R"(aLightweightMC C++ Engine",
                                 "id" : "00000000-0000-0000-0000-000000000000"
                  },
                  {"name" : ")" SECTION "eTPS: " SECTION "f" +
                                             tpsStr + R"( )" SECTION "7| " SECTION "eRAM: " SECTION "f" + ramStr + R"( MB",
          "id": "00000000-0000-0000-0000-000000000001"
      },
      {
          "name": ")" SECTION "7Online players: " SECTION "b" +
                                             std::to_string(onlinePlayers) + R"(",
          "id": "00000000-0000-0000-0000-000000000002"
      }
  ])";

                    std::string statusJson = R"({
    "version": {
        "name": "LightweightMC 1.8",
        "protocol": )" + std::to_string(session.clientProtocol) +
                                             R"(
    },
    "players": {
        "max": 100,
        "online": )" + std::to_string(onlinePlayers) +
                                             R"(,
        "sample": )" + sampleJson + R"(
    },
    "description": {
        "text": ")" + motd + R"("
    }
})";
                    std::vector<uint8_t> payload;
                    Packet::writeString(payload, statusJson);
                    Core::Logger::info("Sent Server version " + std::to_string(session.clientProtocol) + " to client.");
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
                    session.username = Packet::readStringBounded(data, offset, packetEnd);
                    Core::Logger::info("Login Start received for: " + session.username);

                    std::string playerUuid = Player::PlayerManager::getPlayerUuid(session.username);

                    std::vector<uint8_t> loginSuccess;
                    Packet::writeString(loginSuccess, playerUuid);
                    Packet::writeString(loginSuccess, session.username);
                    sendPacket(clientFd, 0x02, loginSuccess);

                    Player::PlayerManager::getInstance().sendPlayPackets(clientFd, session, session.username, m_clients, m_worldManager, m_database, packetSender, broadcastSender);
                }
            }
            else if (session.state == ClientState::PLAY)
            {
                if (packetId == 0x00)
                { // Keep Alive
                    int32_t id = Packet::readVarIntBounded(data, offset, packetEnd, 1);
                    std::vector<uint8_t> payload;
                    Packet::writeVarInt(payload, id);
                    sendPacket(clientFd, 0x00, payload);
                }
                else if (packetId == 0x01) // Chat Message
                {
                    std::string message = Packet::readStringBounded(data, offset, packetEnd);

                    if (!message.empty() && message[0] == '/')
                    {
                        std::istringstream iss(message);
                        std::string command;
                        iss >> command;

                        std::vector<std::string> args;
                        std::string arg;
                        while (iss >> arg)
                        {
                            args.push_back(arg);
                        }

                        if (command == "/stats" || command == "/hud")
                        {
                            Managers::ScoreboardManager::getInstance().sendScoreboard(clientFd, m_clients, packetSender);
                            Managers::ScoreboardManager::getInstance().sendTablistHeaderFooter(clientFd, m_clients.size(), packetSender);

                            std::vector<uint8_t> chatMsg;
                            Packet::writeString(chatMsg, R"({"text":")" SECTION R"(a[Server] HUD and Scoreboard Activated !"})");
                            chatMsg.push_back(1);
                            sendPacket(clientFd, 0x02, chatMsg);
                        }
                        else if (command == "/antiscan" || command == "/hunter")
                        {
                            if (args.size() >= 2 && (args[0] == "block" || args[0] == "add"))
                            {
                                std::string ipToBlock = args[1];
                                if (AntiScan::addCustomIp(ipToBlock))
                                {
                                    std::string reply = SECTION "a[AntiScan] IP " + ipToBlock + " successfully added to custom_ips.txt!";
                                    std::vector<uint8_t> chatMsg;
                                    Packet::writeString(chatMsg, R"({"text":")" + reply + R"("})");
                                    chatMsg.push_back(1);
                                    sendPacket(clientFd, 0x02, chatMsg);
                                }
                            }
                            else if (!args.empty() && args[0] == "reload")
                            {
                                bool success = AntiScan::reload();
                                std::string reply = success
                                                        ? SECTION "a[AntiScan] List reloaded (" + std::to_string(AntiScan::getBlockedCount()) + " IPs)"
                                                        : SECTION "c[AntiScan] Failed to reload list!";
                                std::vector<uint8_t> chatMsg;
                                Packet::writeString(chatMsg, R"({"text":")" + reply + R"("})");
                                chatMsg.push_back(1);
                                sendPacket(clientFd, 0x02, chatMsg);
                            }
                        }
                        else if (command == "/version" || command == "/ver" || command == "/about")
                        {
                            std::string verInfo = SECTION "aThis server is running " SECTION "lLightweightMC " SECTION "av" SERVER_VERSION "\n" SECTION "7Target Protocol: " SECTION "fMinecraft " MC_PROTOCOL_VERSION_NAME;

                            std::vector<uint8_t> chatMsg;
                            Packet::writeString(chatMsg, R"({"text":")" + verInfo + R"("})");
                            chatMsg.push_back(1);
                            sendPacket(clientFd, 0x02, chatMsg);
                        }
                        else if (command == "/ping")
                        {
                            std::vector<uint8_t> chatMsg;
                            Packet::writeString(chatMsg, R"({"text":")" SECTION R"(aPong !"})");
                            chatMsg.push_back(1);
                            sendPacket(clientFd, 0x02, chatMsg);
                        }
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
                        else if (command == "/tell" || command == "/w" || command == "/msg")
                        {
                            if (args.size() < 2)
                            {
                                std::string usage = R"({"text":")" SECTION R"(cUsage: )" + command + R"( <player> <message>"})";
                                std::vector<uint8_t> chatMsg;
                                Packet::writeString(chatMsg, usage);
                                chatMsg.push_back(1);
                                sendPacket(clientFd, 0x02, chatMsg);
                            }
                            else
                            {
                                std::string targetName = args[0];
                                std::string msgText;
                                for (size_t i = 1; i < args.size(); ++i)
                                {
                                    if (i > 1)
                                        msgText += " ";
                                    msgText += args[i];
                                }

                                int targetFd = -1;
                                for (const auto &[fd, s] : m_clients)
                                {
                                    if (s.username == targetName)
                                    {
                                        targetFd = fd;
                                        break;
                                    }
                                }

                                if (targetFd != -1)
                                {
                                    std::string senderName = m_clients[clientFd].username;

                                    std::string targetPayload = R"({"text":")" SECTION R"(d)" + senderName + R"( whispers to you : )" + msgText + R"("})";
                                    std::vector<uint8_t> targetPacket;
                                    Packet::writeString(targetPacket, targetPayload);
                                    targetPacket.push_back(1);
                                    sendPacket(targetFd, 0x02, targetPacket);

                                    std::string senderPayload = R"({"text":")" SECTION R"(dTo )" + targetName + R"( : )" + msgText + R"("})";
                                    std::vector<uint8_t> senderPacket;
                                    Packet::writeString(senderPacket, senderPayload);
                                    senderPacket.push_back(1);
                                    sendPacket(clientFd, 0x02, senderPacket);
                                }
                                else
                                {
                                    std::string errPayload = R"({"text":")" SECTION R"(cPlayer Not Found."})";
                                    std::vector<uint8_t> errPacket;
                                    Packet::writeString(errPacket, errPayload);
                                    errPacket.push_back(1);
                                    sendPacket(clientFd, 0x02, errPacket);
                                }
                            }
                        }
                        else if (command == "/tp" || command == "/teleport")
                        {
                            if (args.size() == 3)
                            {
                                try
                                {
                                    double x = std::stod(args[0]);
                                    double y = std::stod(args[1]);
                                    double z = std::stod(args[2]);

                                    Player::PlayerManager::getInstance().teleportPlayer(clientFd, session, x, y, z, 0.0f, 0.0f, packetSender);

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
                                    Player::PlayerManager::getInstance().teleportPlayer(clientFd, session, tx, ty, tz, 0.0f, 0.0f, packetSender);

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
                                    auto &p1Session = m_clients[p1Fd];
                                    Player::PlayerManager::getInstance().teleportPlayer(p1Fd, p1Session, p2x, p2y, p2z, 0.0f, 0.0f, packetSender);

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
                        else if (command == "/tpall")
                        {
                            double myX = session.x, myY = session.y, myZ = session.z;
                            int count = 0;

                            for (auto &[fd, s] : m_clients)
                            {
                                if (s.state == ClientState::PLAY && fd != clientFd)
                                {
                                    Player::PlayerManager::getInstance().teleportPlayer(fd, s, myX, myY, myZ, 0.0f, 0.0f, packetSender);

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
                        else if (command == "/help" || command == "/?")
                        {
                            std::string helpText = SECTION "e--- LightweightMC Help ---\n" SECTION "f/help " SECTION "7- Displays this help menu\n" SECTION "f/list " SECTION "7- Lists all connected players\n" SECTION "f/ping " SECTION "7- Tests server latency\n" SECTION "f/hud " SECTION "7or " SECTION "f/stats " SECTION "7- Shows HUD & Scoreboard\n" SECTION "f/tell <player> <msg> " SECTION "7- Sends a private message";

                            std::vector<uint8_t> chatMsg;
                            Packet::writeString(chatMsg, R"({"text":")" + helpText + R"("})");
                            chatMsg.push_back(1);
                            sendPacket(clientFd, 0x02, chatMsg);
                        }
                        else
                        {
                            std::vector<uint8_t> chatMsg;
                            Packet::writeString(chatMsg, R"({"text":")" SECTION R"(cUnknown command. Type /help for a list of commands."})");
                            chatMsg.push_back(1);
                            sendPacket(clientFd, 0x02, chatMsg);
                        }
                    }
                    else
                    {
                        std::string formattedChat = "<" + session.username + "> " + message;

                        std::vector<uint8_t> chatMsg;
                        Packet::writeString(chatMsg, R"({"text":")" + formattedChat + R"("})");
                        chatMsg.push_back(0);

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
                else if (packetId == 0x04 || packetId == 0x06) // Player Position & Look
                {
                    session.x = Packet::readDoubleBounded(data, offset, packetEnd);
                    session.y = Packet::readDoubleBounded(data, offset, packetEnd);
                    session.z = Packet::readDoubleBounded(data, offset, packetEnd);

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
                        m_worldManager.updateChunksAroundPlayer(clientFd, session, 5, packetSender);
                    }

                    broadcastPacket(0x18, teleport, clientFd);
                }
                else if (packetId == 0x07) // Player Digging
                {
                    int32_t status = Packet::readVarIntBounded(data, offset, packetEnd, 1);

                    uint64_t val = Packet::readUInt64Bounded(data, offset, packetEnd);
                    int32_t x = static_cast<int32_t>(val >> 38);
                    // 1.7.10 sends y as a plain signed value in the 12-bit field; 1.8+ packs it so
                    // that values >= 2048 are negative (supports the full -64..319 world range).
                    int32_t y = (session.clientProtocol >= 47)
                                    ? Packet::decodeModernY(val)
                                    : static_cast<int32_t>((val >> 26) & 0xFFF);
                    int32_t z = static_cast<int32_t>(val << 38 >> 38);

                    if (x >= (1 << 25))
                        x -= (1 << 26);
                    if (session.clientProtocol < 47 && y >= (1 << 11))
                        y -= (1 << 12);
                    if (z >= (1 << 25))
                        z -= (1 << 26);

                    uint8_t face = static_cast<uint8_t>(Packet::readByteBounded(data, offset, packetEnd));
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
                else if (packetId == 0x08) // Block Placement
                {
                    Core::Logger::info("Block Placement Packet");
                    uint64_t val = Packet::readUInt64Bounded(data, offset, packetEnd);
                    int32_t x = static_cast<int32_t>(val >> 38);
                    // 1.7.10 sends y as a plain signed value in the 12-bit field; 1.8+ packs it so
                    // that values >= 2048 are negative (supports the full -64..319 world range).
                    int32_t y = (session.clientProtocol >= 47)
                                    ? Packet::decodeModernY(val)
                                    : static_cast<int32_t>((val >> 26) & 0xFFF);
                    int32_t z = static_cast<int32_t>(val << 38 >> 38);

                    if (x >= (1 << 25))
                        x -= (1 << 26);
                    if (session.clientProtocol < 47 && y >= (1 << 11))
                        y -= (1 << 12);
                    if (z >= (1 << 25))
                        z -= (1 << 26);

                    uint8_t face = static_cast<uint8_t>(Packet::readByteBounded(data, offset, packetEnd));
                    int16_t itemSlot = Packet::readShortBounded(data, offset, packetEnd);

                    if (itemSlot > 0 && itemSlot < 256)
                    {

                        uint8_t itemCount = static_cast<uint8_t>(Packet::readByteBounded(data, offset, packetEnd));
                        (void)itemCount;
                        int16_t itemDamage = Packet::readShortBounded(data, offset, packetEnd);

                        // Read cursor position on the clicked face (0-15 each byte)
                        uint8_t cursorX = 0, cursorY = 0, cursorZ = 0;
                        if (offset + 3 <= packetEnd)
                        {
                            cursorX = data[offset++];
                            cursorY = data[offset++];
                            cursorZ = data[offset++];
                        }
                        (void)cursorX; // consumed to keep the read offset aligned
                        (void)cursorZ; // consumed to keep the read offset aligned

                        // Adjust target position based on face clicked
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

                        // ------------------------------------
                        // Compute orientation metadata from player yaw/pitch
                        // MC yaw: 0=south, 90=west, 180=north, 270=east (clockwise from south)
                        // ------------------------------------
                        uint8_t orientMeta = 0;
                        uint16_t blockId = static_cast<uint16_t>(itemSlot);

                        // Normalise yaw to [0, 360)
                        float yaw = session.yaw;
                        while (yaw < 0.0f)
                            yaw += 360.0f;
                        while (yaw >= 360.0f)
                            yaw -= 360.0f;

                        // 4-way horizontal direction from yaw:
                        //  0 = south (yaw 315-360 or 0-44)  → meta 3 for stairs
                        //  1 = west  (yaw 45-134)            → meta 1
                        //  2 = north (yaw 135-224)           → meta 2
                        //  3 = east  (yaw 225-314)           → meta 0
                        // Standard "facing" meta: 2=N,3=S,4=W,5=E
                        uint8_t yawDir4 = 0; // 0=south,1=west,2=north,3=east
                        if (yaw < 45.0f || yaw >= 315.0f)
                            yawDir4 = 0; // south
                        else if (yaw < 135.0f)
                            yawDir4 = 1; // west
                        else if (yaw < 225.0f)
                            yawDir4 = 2; // north
                        else
                            yawDir4 = 3; // east

                        // facing5: 2=north, 3=south, 4=west, 5=east (for dispensers/furnaces/pistons)
                        uint8_t facing5 = 3; // default south
                        switch (yawDir4)
                        {
                        case 0:
                            facing5 = 3;
                            break; // south
                        case 1:
                            facing5 = 4;
                            break; // west
                        case 2:
                            facing5 = 2;
                            break; // north
                        case 3:
                            facing5 = 5;
                            break; // east
                        }
                        // Override with face for vertical blocks (top/bottom)
                        if (face == 0)
                            facing5 = 0; // down
                        if (face == 1)
                            facing5 = 1; // up

                        // --- Determine block-specific orientation metadata ---
                        bool isStairs = (blockId == 53 ||  // oak stairs
                                         blockId == 67 ||  // cobblestone stairs
                                         blockId == 108 || // brick stairs
                                         blockId == 109 || // stone brick stairs
                                         blockId == 114 || // nether brick stairs
                                         blockId == 128 || // sandstone stairs
                                         blockId == 134 || // spruce stairs
                                         blockId == 135 || // birch stairs
                                         blockId == 136 || // jungle stairs
                                         blockId == 156 || // quartz stairs
                                         blockId == 163 || // acacia stairs
                                         blockId == 164);  // dark oak stairs

                        bool isSlab = (blockId == 44 || blockId == 126);
                        bool isLever = (blockId == 69);
                        bool isButton = (blockId == 77 || blockId == 143);
                        bool isLog = (blockId == 17 || blockId == 162);

                        if (isStairs)
                        {
                            // Stairs meta: 0=east,1=west,2=south,3=north; bit3=upside-down
                            // Updated mapping for standard orientation:
                            switch (yawDir4)
                            {
                            case 0:
                                orientMeta = 2;
                                break; // South
                            case 1:
                                orientMeta = 1;
                                break; // West
                            case 2:
                                orientMeta = 3;
                                break; // North
                            case 3:
                                orientMeta = 0;
                                break; // East
                            }
                            // Upside-down if clicked on top half of bottom face or placed on ceiling
                            bool upsideDown = (face == 0) || (face == 1 && cursorY > 8);
                            if (upsideDown)
                                orientMeta |= 0x04;
                        }
                        else if (isSlab)
                        {
                            // Lower half (meta & ~8) = bottom; +8 = top half
                            uint8_t slabType = static_cast<uint8_t>(itemDamage & 0x07);
                            orientMeta = slabType;
                            bool topHalf = (face == 0) || (face == 1 && cursorY > 8);
                            if (topHalf)
                                orientMeta |= 0x08;
                        }
                        else if (isLever)
                        {
                            // face==0 (bottom) → ceiling lever: 0=east-west, 7=north-south
                            // face==1 (top)    → floor lever:   5=east-west, 6=north-south
                            // face 2-5 (wall)  → wall lever: 1=east,2=west,3=south,4=north
                            if (face == 0)
                                orientMeta = (yawDir4 == 0 || yawDir4 == 2) ? 7 : 0;
                            else if (face == 1)
                                orientMeta = (yawDir4 == 0 || yawDir4 == 2) ? 6 : 5;
                            else
                                orientMeta = facing5 - 1; // 1=east wall,2=west wall,3=south wall,4=north wall
                        }
                        else if (isButton)
                        {
                            // face 0=ceiling(5), 1=floor(0), 2=north(4), 3=south(3), 4=west(2), 5=east(1)
                            switch (face)
                            {
                            case 0:
                                orientMeta = 5;
                                break;
                            case 1:
                                orientMeta = 0;
                                break;
                            case 2:
                                orientMeta = 4;
                                break;
                            case 3:
                                orientMeta = 3;
                                break;
                            case 4:
                                orientMeta = 2;
                                break;
                            case 5:
                                orientMeta = 1;
                                break;
                            }
                        }
                        else if (isLog)
                        {
                            // Log axis: 0=vertical(y), 4=east-west(x), 8=north-south(z)
                            uint8_t logType = static_cast<uint8_t>(itemDamage & 0x03);
                            if (face == 0 || face == 1)
                                orientMeta = logType | 0x00; // y-axis
                            else if (face == 4 || face == 5)
                                orientMeta = logType | 0x04; // x-axis
                            else
                                orientMeta = logType | 0x08; // z-axis
                        }
                        else
                        {
                            // Default: use facing5 for directional blocks (furnace, dispenser, etc.)
                            // or fall back to raw itemDamage metadata for non-directional blocks
                            bool isDirectional = (blockId == 23 ||  // dispenser
                                                  blockId == 26 ||  // bed
                                                  blockId == 29 ||  // sticky piston
                                                  blockId == 33 ||  // piston
                                                  blockId == 54 ||  // chest
                                                  blockId == 58 ||  // crafting table (no facing)
                                                  blockId == 61 ||  // furnace (off)
                                                  blockId == 62 ||  // furnace (on)
                                                  blockId == 130 || // ender chest
                                                  blockId == 146 || // trapped chest
                                                  blockId == 154 || // hopper
                                                  blockId == 158);  // dropper
                            if (isDirectional && face > 1)
                                orientMeta = facing5;
                            else
                                orientMeta = static_cast<uint8_t>(itemDamage & 0x0F);
                        }

                        int chunkX = Packet::floorDiv(x, 16);
                        int chunkZ = Packet::floorDiv(z, 16);
                        int relX = Packet::floorMod(x, 16);
                        int relZ = Packet::floorMod(z, 16);
                        uint16_t existingBlockData = m_worldStorage.getBlockAt(chunkX, chunkZ, relX, y, relZ);
                        uint16_t existingBlockId = existingBlockData >> 4;

                        // List of interactive block IDs opening a GUI window
                        bool isInteractive = (existingBlockId == 54 ||  // Chest
                                              existingBlockId == 61 ||  // Furnace (off)
                                              existingBlockId == 62 ||  // Furnace (on)
                                              existingBlockId == 58 ||  // Crafting Table
                                              existingBlockId == 23 ||  // Dispenser
                                              existingBlockId == 145 || // Anvil
                                              existingBlockId == 146 || // Trapped Chest
                                              existingBlockId == 154 || // Hopper
                                              existingBlockId == 158 || // Dropper
                                              existingBlockId == 130);  // Ender Chest
                        Core::Logger::info("Existing Bloc " + std::to_string(existingBlockId));
                        // Player interacted with an inventory block (without sneaking)

                        if (!isInteractive /*&& face != 255*/)
                        {
                            Core::Logger::info("Placement Bloc");
                            auto translatedBlock = Protocol::ProtocolTranslator::translateBlockFromClient(
                                session.clientProtocol, static_cast<uint16_t>(itemSlot), orientMeta);
                            uint16_t blockData = translatedBlock.toCombinedData();

                            m_worldStorage.saveBlockChange(chunkX, chunkZ, relX, y, relZ, blockData);

                            // Broadcast Block Change (0x23) to all players
                            std::vector<uint8_t> blockChange;
                            uint64_t posEnc = ((static_cast<uint64_t>(x) & 0x3FFFFFF) << 38) |
                                              ((static_cast<uint64_t>(y) & 0xFFF) << 26) |
                                              (static_cast<uint64_t>(z) & 0x3FFFFFF);

                            for (int i = 7; i >= 0; --i)
                                blockChange.push_back((posEnc >> (i * 8)) & 0xFF);
                            Packet::writeVarInt(blockChange, blockData);

                            broadcastPacket(0x23, blockChange);
                        }
                        else
                        {
                            Core::Logger::info("Interraction Bloc");
                            // ------------------------------------
                            // Open container GUI if block is a known container type
                            // ------------------------------------
                            uint8_t pureBlockId = static_cast<uint8_t>(blockId);
                            struct ContainerDef
                            {
                                const char *type;
                                const char *title;
                                uint8_t slots;
                            };

                            // Check if this block is a container and get its definition
                            ContainerDef container{"", "", 0};
                            bool isContainer = false;

                            if (pureBlockId == 54 || pureBlockId == 146) // chest / trapped chest
                            {
                                container = {"minecraft:chest", "Chest", 27};
                                isContainer = true;
                            }
                            else if (pureBlockId == 61 || pureBlockId == 62) // furnace
                            {
                                container = {"minecraft:furnace", "Furnace", 3};
                                isContainer = true;
                            }
                            else if (pureBlockId == 23) // dispenser
                            {
                                container = {"minecraft:dispenser", "Dispenser", 9};
                                isContainer = true;
                            }
                            else if (pureBlockId == 158) // dropper
                            {
                                container = {"minecraft:dropper", "Dropper", 9};
                                isContainer = true;
                            }
                            else if (pureBlockId == 154) // hopper
                            {
                                container = {"minecraft:hopper", "Hopper", 5};
                                isContainer = true;
                            }
                            else if (pureBlockId == 117) // brewing stand
                            {
                                container = {"minecraft:brewing_stand", "Brewing Stand", 4};
                                isContainer = true;
                            }
                            else if (pureBlockId == 116) // enchanting table
                            {
                                container = {"minecraft:enchanting_table", "Enchantment Table", 2};
                                isContainer = true;
                            }
                            else if (pureBlockId == 145) // anvil
                            {
                                container = {"minecraft:anvil", "Repair & Name", 3};
                                isContainer = true;
                            }

                            if (isContainer)
                            {
                                // Assign a window ID (1-100, wrap around)
                                session.openWindowId = (session.openWindowId % 100) + 1;
                                uint8_t winId = session.openWindowId;

                                // Build title JSON
                                std::string titleJson = "{\"text\":\"" + std::string(container.title) + "\"}";

                                // Send Open Window packet (0x2D)
                                std::vector<uint8_t> openWin;
                                openWin.push_back(winId);
                                // Inventory type string (VarInt length-prefixed)
                                std::string invType = container.type;
                                Packet::writeVarInt(openWin, static_cast<int32_t>(invType.size()));
                                openWin.insert(openWin.end(), invType.begin(), invType.end());
                                // Title JSON (VarInt length-prefixed)
                                Packet::writeVarInt(openWin, static_cast<int32_t>(titleJson.size()));
                                openWin.insert(openWin.end(), titleJson.begin(), titleJson.end());
                                // Number of slots
                                openWin.push_back(container.slots);
                                // Entity ID (only for horses; 0 otherwise)
                                Packet::writeInt(openWin, 0);
                                Core::Logger::info("[DEBUG] Sending 0x2D: WinID=" + std::to_string(winId) + ", Title=" + container.title);
                                // sendPacket(clientFd, 0x2D, openWin);

                                // Send Window Items (0x30) with empty slots for the container
                                // (container inventory not yet persistent; filled with empty stacks)
                                std::vector<uint8_t> winItems;
                                winItems.push_back(winId);
                                Packet::writeShort(winItems, static_cast<int16_t>(container.slots));
                                for (int s = 0; s < container.slots; ++s)
                                    Packet::writeShort(winItems, -1); // empty slot
                                Core::Logger::info("[DEBUG] Sending 0x30: WinID=" + std::to_string(winId) + ", Slots=" + std::to_string(container.slots));
                                // sendPacket(clientFd, 0x30, winItems);

                                Core::Logger::info("[Container] Opened " + std::string(container.title) +
                                                   " for player " + session.username +
                                                   " (windowId=" + std::to_string(winId) + ")");
                            }
                        }
                    }
                }

                else if (packetId == 0x09) // Held Item Change
                {
                    int16_t slot = Packet::readShortBounded(data, offset, packetEnd);
                    session.selectedSlot = slot;

                    std::vector<uint8_t> equipPacket;
                    Packet::writeVarInt(equipPacket, clientFd);
                    Packet::writeShort(equipPacket, 0);

                    int16_t itemId = (slot >= 0 && slot < 36) ? session.inventory[36 + slot].id : -1;
                    auto translatedItem = Protocol::ProtocolTranslator::translateItemToClient(session.clientProtocol, itemId);

                    Packet::writeShort(equipPacket, translatedItem.id);
                    if (translatedItem.id != -1)
                    {
                        equipPacket.push_back(session.inventory[36 + slot].count);
                        Packet::writeShort(equipPacket, session.inventory[36 + slot].damage);
                        equipPacket.push_back(0);
                    }

                    broadcastPacket(0x04, equipPacket, clientFd);
                }
                else if (packetId == 0x0E) // Click Window (Inventory Clicks)
                {
                    uint8_t windowId = static_cast<uint8_t>(Packet::readByteBounded(data, offset, packetEnd));
                    int16_t slot = Packet::readShortBounded(data, offset, packetEnd);
                    uint8_t button = static_cast<uint8_t>(Packet::readByteBounded(data, offset, packetEnd));
                    (void)button;
                    int16_t actionNumber = Packet::readShortBounded(data, offset, packetEnd);
                    uint8_t mode = static_cast<uint8_t>(Packet::readByteBounded(data, offset, packetEnd));
                    (void)mode;
                    int16_t itemId = Packet::readShortBounded(data, offset, packetEnd);

                    if (itemId != -1 && offset + 3 <= packetEnd)
                    {
                        uint8_t count = data[offset++];
                        int16_t damage = Packet::readShortBounded(data, offset, packetEnd);
                        if (offset < packetEnd && data[offset] == 0)
                        {
                            offset++; // NBT tag = 0
                        }

                        auto translatedItem = Protocol::ProtocolTranslator::translateItemFromClient(session.clientProtocol, itemId, count, damage);
                        if (slot >= 0 && slot < 45)
                        {
                            session.inventory[slot] = {translatedItem.id, translatedItem.count, translatedItem.damage};
                        }
                    }
                    else
                    {
                        if (slot >= 0 && slot < 45)
                        {
                            session.inventory[slot] = {-1, 0, 0};
                        }
                    }

                    // Confirm transaction (Packet 0x32)
                    std::vector<uint8_t> confirm;
                    confirm.push_back(windowId);
                    Packet::writeShort(confirm, actionNumber);
                    confirm.push_back(1); // Accepted = true
                    sendPacket(clientFd, 0x32, confirm);
                }
                else if (packetId == 0x10) // Creative Inventory Action / Slot Click
                {
                    int16_t slot = Packet::readShortBounded(data, offset, packetEnd);
                    int16_t itemId = Packet::readShortBounded(data, offset, packetEnd);

                    if (itemId != -1 && offset + 3 <= packetEnd)
                    {
                        uint8_t count = data[offset++];
                        int16_t damage = Packet::readShortBounded(data, offset, packetEnd);

                        auto translatedItem = Protocol::ProtocolTranslator::translateItemFromClient(session.clientProtocol, itemId, count, damage);
                        if (slot >= 0 && slot < 45)
                        {
                            session.inventory[slot] = {translatedItem.id, translatedItem.count, translatedItem.damage};
                        }
                    }
                    else
                    {
                        if (slot >= 0 && slot < 45)
                        {
                            session.inventory[slot] = {-1, 0, 0};
                        }
                    }

                    if (slot == (36 + session.selectedSlot))
                    {
                        std::vector<uint8_t> equipPacket;
                        Packet::writeVarInt(equipPacket, clientFd);
                        Packet::writeShort(equipPacket, 0);

                        int16_t currentItem = session.inventory[slot].id;
                        auto translatedItem = Protocol::ProtocolTranslator::translateItemToClient(session.clientProtocol, currentItem);

                        Packet::writeShort(equipPacket, translatedItem.id);
                        if (translatedItem.id != -1)
                        {
                            equipPacket.push_back(session.inventory[slot].count);
                            Packet::writeShort(equipPacket, session.inventory[slot].damage);
                            equipPacket.push_back(0);
                        }

                        broadcastPacket(0x04, equipPacket, clientFd);
                    }
                }
                else if (packetId == 0x0A) // Animation (Swing Arm)
                {
                    std::vector<uint8_t> animPacket;
                    Packet::writeVarInt(animPacket, clientFd);
                    animPacket.push_back(0);

                    broadcastPacket(0x0B, animPacket, clientFd);
                }
            }

            offset = dataStart + packetLen;
        }

        if (offset > 0)
        {
            session.rxBuffer.erase(session.rxBuffer.begin(), session.rxBuffer.begin() + offset);
        }
    }

    void NetworkManager::pollEvents(int timeoutMs)
    {
        int nfds = epoll_wait(m_epollFd, m_events, 64, timeoutMs);

        auto packetSender = [this](int f, int32_t p, const std::vector<uint8_t> &b)
        {
            sendPacket(f, p, b);
        };

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
                    char ipStr[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(clientAddr.sin_addr), ipStr, INET_ADDRSTRLEN);
                    std::string clientIp(ipStr);
                    uint16_t clientPort = ntohs(clientAddr.sin_port);

                    if (antiscan)
                    {
                        if (AntiScan::isBlocked(clientIp, clientPort))
                        {
                            AntiScan::handleBlockedClient(newFd, clientIp, clientPort);
                            continue;
                        }
                    }
                    setNonBlocking(newFd);

                    int flag = 1;
                    setsockopt(newFd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));

                    int pmTu = IP_PMTUDISC_DONT;
                    setsockopt(newFd, IPPROTO_IP, IP_MTU_DISCOVER, &pmTu, sizeof(pmTu));

                    epoll_event ev{};
                    ev.events = EPOLLIN | EPOLLRDHUP;
                    ev.data.fd = newFd;
                    epoll_ctl(m_epollFd, EPOLL_CTL_ADD, newFd, &ev);

                    m_clients[newFd] = LightweightMC::Player::PlayerSession{newFd, ClientState::HANDSHAKE, ""};
                }
            }
            else
            {
                auto it = m_clients.find(clientFd);
                if (it == m_clients.end())
                {
                    continue;
                }

                if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
                {
                    handleDisconnect(clientFd);
                    continue;
                }

                if (events & EPOLLOUT)
                {
                    flushSendBuffer(it->second);
                }

                if (m_clients.find(clientFd) == m_clients.end())
                {
                    continue;
                }

                if (events & EPOLLIN)
                {
                    handleClientData(clientFd);
                }
            }
        }

        // Periodic Scoreboard & Tablist refresh (every 1 sec)
        static auto lastUpdate = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate).count() >= 1000)
        {
            for (const auto &[fd, session] : m_clients)
            {
                if (session.state == ClientState::PLAY)
                {
                    Managers::ScoreboardManager::getInstance().sendScoreboard(fd, m_clients, packetSender);
                    Managers::ScoreboardManager::getInstance().sendTablistHeaderFooter(fd, m_clients.size(), packetSender);
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
        std::string username = "Unauthenticated Client";
        std::string ipPort = Core::getIpAndPortFromFd(fd);

        {
            std::lock_guard<std::mutex> lock(m_clientsMutex);

            auto it = m_clients.find(fd);
            if (it == m_clients.end())
            {
                return;
            }

            if (!it->second.username.empty())
            {
                username = it->second.username;
                Player::PlayerManager::getInstance().savePlayerState(it->second, m_database);
            }

            m_clients.erase(it);
        }

        Managers::ScoreboardManager::getInstance().removeClientCache(fd);
        epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);

        Core::Logger::info("Client " + std::to_string(fd) + " (" + username +
                           ") disconnected. [IP: " + ipPort + "]");
    }

    void NetworkManager::flushSendBuffer(LightweightMC::Player::PlayerSession &session)
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

} // namespace LightweightMC::Network