#include "network/handlers/HandshakeHandler.hpp"
#include "core/Logger.hpp"
#include <cstdio>
#include <string>

namespace HandshakeHandler
{

    void handle(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd, int32_t packetId)
    {
        if (packetId != 0x00)
        {
            // HANDSHAKE only decodes the Connect (0x00) packet. Any other id
            // reaching this handler is a protocol-decoding gap — WARN so it is
            // visible in the log instead of being silently swallowed.
            LightweightMC::Core::Logger::warn("[NET DROP] HANDSHAKE packet 0x" +
                               [&]{ char b8[8]; std::snprintf(b8, sizeof(b8), "%02X", packetId & 0xFF); return std::string(b8); }() +
                               " (" + std::to_string(packetId) + ") has no decoder — dropped fd=" +
                               std::to_string(clientFd) + " bodyBytes=" +
                               std::to_string(packetEnd > offset ? packetEnd - offset : 0));
            return;
        }

        int32_t protoVersion = LightweightMC::Network::Packet::readVarIntBounded(data, offset, packetEnd, 1);
        std::string host = LightweightMC::Network::Packet::readStringBounded(data, offset, packetEnd);
        // Bounds check before reading the two port bytes -- previously
        // `data[offset]` and `data[offset + 1]` were dereferenced before
        // checking that `offset + 2 <= packetEnd`, so a truncated
        // handshake (peer only sent protocol+host) read past the frame
        // into the following rxBuffer bytes.
        uint16_t port = 0;
        if (offset + 2 <= packetEnd)
        {
            port = static_cast<uint16_t>((static_cast<uint16_t>(data[offset]) << 8) |
                                          static_cast<uint16_t>(data[offset + 1]));
            offset += 2;
        }
        (void)port;
        int32_t nextState = LightweightMC::Network::Packet::readVarIntBounded(data, offset, packetEnd, 1);

        if (protoVersion >= 0)
            session.clientProtocol = protoVersion;

        session.adapter = LightweightMC::Network::Protocol::EraAdapter::create(session.clientProtocol);
        session.protocol = LightweightMC::Network::Protocol::BaseProtocol::create(session.clientProtocol);

        std::string ipPort = LightweightMC::Core::getIpAndPortFromFd(clientFd);
        LightweightMC::Core::Logger::info("[NET] Handshake from " + ipPort + " protocol=" + std::to_string(session.clientProtocol) +
                                          (session.adapter ? "" : " [UNSUPPORTED ERA - legacy handlers only]"));

        const LightweightMC::Network::Protocol::ProtocolVersionInfo *versionInfo = LightweightMC::Network::Protocol::ProtocolVersion::resolve(session.clientProtocol);
        if (versionInfo)
        {
            LightweightMC::Core::Logger::info("Handshake from " + ipPort + " protocol=" + std::to_string(protoVersion) +
                                              " (" + versionInfo->version + ", era=" + LightweightMC::Network::Protocol::eraName(versionInfo->era) + ")");
        }
        else
        {
            const LightweightMC::Network::Protocol::ProtocolVersionInfo *nearest = LightweightMC::Network::Protocol::ProtocolVersion::nearestSupported(protoVersion);
            LightweightMC::Core::Logger::info("Handshake from " + ipPort + " protocol=" + std::to_string(protoVersion) +
                                              " (unsupported; nearest supported: " + (nearest ? std::string(nearest->version) : "?") + ")");
        }

        if (nextState == 1)
            session.state = ClientState::STATUS;
        else if (nextState == 2)
            session.state = ClientState::LOGIN;
    }

} // namespace HandshakeHandler