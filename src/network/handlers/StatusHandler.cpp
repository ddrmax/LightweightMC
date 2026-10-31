#include "network/handlers/StatusHandler.hpp"
#include "core/Logger.hpp"
#include <sstream>
#include <iomanip>

namespace StatusHandler
{

    void handle(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd,
                int32_t packetId, const ClientMap &clients, LightweightMC::Network::NetworkManager &net)
    {
        // The dispatcher has already consumed the packet length varint and the
        // packet id varint: `offset` now points at the first byte of the packet
        // body and `packetEnd` is the exclusive upper bound for this packet.
        // Do NOT re-read a length varint here or `offset` will be advanced into
        // the payload and every subsequent field read will be misaligned.
        (void)session; // no session mutation on status packets

        if (packetId == 0x00) // Status Request
        {
            int onlinePlayers = 0;
            for (const auto &[fd, s] : clients)
            {
                if (s.state == ClientState::PLAY)
                    onlinePlayers++;
            }

            std::string motd = SECTION "2" SECTION "n" SECTION "l" SECTION "o"
                                       "LightweightMC C++ Engine " SECTION "c" SECTION "n" SECTION "l" SECTION "o"
                                       "V" LWMC_VER SECTION "r";

            double currentTps = 20.0;
            double currentRam = LightweightMC::Core::getProcessRAM();

            std::ostringstream tpsStream, ramStream;
            tpsStream << std::fixed << std::setprecision(1) << currentTps;
            ramStream << std::fixed << std::setprecision(1) << currentRam;

            std::string sampleJson = R"([{"name": ")" SECTION R"(aLightweightMC C++ Engine",
                     "id" : "00000000-0000-0000-0000-000000000000"
      },
      {"name" : ")" SECTION "eTPS: " SECTION "f" +
                                     tpsStream.str() + R"( )" SECTION "7| " SECTION "eRAM: " SECTION "f" + ramStream.str() + R"( MB",
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
            LightweightMC::Network::Packet::writeString(payload, statusJson);
            LightweightMC::Core::Logger::info("Sent Server version " + std::to_string(session.clientProtocol) + " to client.");
            net.sendPacket(clientFd, 0x00, payload);
        }
        else if (packetId == 0x01) // Ping Request
        {
            // Ping packet body is exactly 8 bytes (long latency). A short body
            // is a decode gap — WARN so it shows up in the log rather than
            // being silently swallowed.
            if (packetEnd >= offset + 8)
            {
                std::vector<uint8_t> payload(data + offset, data + offset + 8);
                net.sendPacket(clientFd, 0x01, payload);
            }
            else
            {
                LightweightMC::Core::Logger::warn("[NET DROP] STATUS ping (0x01) body too short fd=" +
                                   std::to_string(clientFd) + " haveBytes=" +
                                   std::to_string(packetEnd > offset ? packetEnd - offset : 0) +
                                   " need=8 — no reply sent");
            }
        }
        else
        {
            // No decoder registered for this packet id in STATUS state — the
            // frame is dropped. WARN so protocol/behaviour decoding gaps are
            // visible in the log immediately.
            char hexBuf[16];
            std::snprintf(hexBuf, sizeof(hexBuf), "0x%02X", packetId & 0xFF);
            LightweightMC::Core::Logger::warn(std::string("[NET DROP] STATUS packet ") + hexBuf +
                               " (" + std::to_string(packetId) + ") has no decoder — dropped fd=" +
                               std::to_string(clientFd) + " bodyBytes=" +
                               std::to_string(packetEnd > offset ? packetEnd - offset : 0));
        }
    }



} // namespace StatusHandler