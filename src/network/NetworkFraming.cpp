// Wire-level packet framing: how a semantic packet body becomes bytes on the
// socket, and how a raw wire ID gets translated into the era-correct ID for
// the target client. This is the single place where multi-version framing
// lives; the rest of NetworkManager and every handler is era-agnostic.
//
// See src/network/NetworkManager.cpp for the class-level invariant notes.

#include "network/NetworkManager.hpp"
#include "network/Packet.hpp"
#include "network/protocol/ProtocolVersion.hpp"
#include "network/protocol/EraAdapter.hpp"
#include "network/protocol/BaseProtocol.hpp"
#include "core/Logger.hpp"
#include <vector>
#include <cstdint>
#include <sstream>
#include <iomanip>

namespace LightweightMC::Network
{

    // Small helper so the wire-layer debug lines can show the first N bytes of
    // any buffer without pulling in a full hexdump utility. Truncated to
    // 32 bytes so the log stays readable while still letting us eyeball the
    // varint wire-ID prefix and the top of the payload.
    static inline std::string hexPreview(const uint8_t *bytes, size_t count)
    {
        std::ostringstream oss;
        const size_t n = count < 32 ? count : 32;
        for (size_t i = 0; i < n; ++i)
        {
            oss << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<unsigned>(bytes[i]);
            if (i + 1 < n)
                oss << ' ';
        }
        if (count > n)
            oss << " …(+" << (count - n) << "B)";
        return oss.str();
    }

    // Small helper for hex-rendering a packet id (0xNN) in debug lines.
    static inline std::string hexId(int32_t v)
    {
        std::ostringstream o;
        o << "0x" << std::hex << v;
        return o.str();
    }

    // Send a semantic packet (identified by its 1.8 wire ID -- the literal
    // IDs used throughout the codebase) to a specific client. Route through
    // the session's BaseProtocol::encodeFrame so era-specific framing
    // (deflate, XOR header for 1.13+, etc.) plugs in without any further
    // changes on the caller side.
    void NetworkManager::sendPacket(int fd, int32_t packetId, const std::vector<uint8_t> &payload)
    {
        Core::Logger::debug("[NET SEND] enter fd=" + std::to_string(fd) +
                            " semanticId=" + hexId(packetId) +
                            " payloadBytes=" + std::to_string(payload.size()));

        auto it = m_clients.find(fd);
        if (it == m_clients.end())
        {
            Core::Logger::debug("[NET SEND] abort fd=" + std::to_string(fd) +
                                " — client not in m_clients (packet dropped)");
            return;
        }

        auto &session = it->second;

        // Translate the 1.8 wire ID to this client's era. Callers use the
        // literal 1.8 IDs everywhere so we look them up by (era, 1.8 wire
        // ID) via the generated reverse tables. Unmapped IDs (-1) or
        // pre-handshake sessions (no adapter yet) pass through unchanged.
        int32_t wireId = packetId;
        if (session.adapter != nullptr)
        {
            const int32_t mapped = Protocol::ProtocolVersion::s2cWireIdFromLegacy(
                session.adapter->era(), packetId);
            if (mapped >= 0)
                wireId = mapped;
            Core::Logger::debug("[NET SEND] eraMap fd=" + std::to_string(fd) +
                                " semantic=" + hexId(packetId) +
                                " wire=" + hexId(wireId));
        }
        else
        {
            Core::Logger::debug("[NET SEND] noAdapter fd=" + std::to_string(fd) +
                                " wireId=" + hexId(wireId) + " (passthrough)");
        }

        // Build the packet body: varint wire ID + caller-supplied payload.
        std::vector<uint8_t> body;
        Packet::writeVarInt(body, wireId);
        body.insert(body.end(), payload.begin(), payload.end());

        // 1.8 allows up to 21 bits of length; anything bigger corrupts the
        // client stream because the caller forgot to split. Log and drop
        // rather than silently write a truncated length.
        static constexpr int32_t kMaxBodyBytes = 2097151;
        if (body.size() > static_cast<size_t>(kMaxBodyBytes))
        {
            Core::Logger::warn("sendPacket: client " + std::to_string(fd) +
                                " packet id=" + std::to_string(packetId) +
                                " body=" + std::to_string(body.size()) +
                                " bytes exceeds 21-bit wire length limit, dropping.");
            return;
        }

        Core::Logger::debug("[NET SEND] body fd=" + std::to_string(fd) +
                            " size=" + std::to_string(body.size()) +
                            "B hex=" + hexPreview(body.data(), body.size()));

        // Route the framing through the negotiated BaseProtocol so
        // multi-version wire formats (deflate for 1.13+, XOR header for
        // 1.21+, custom thresholds for modern) plug in here. The default
        // BaseProtocol::encodeFrame is byte-identical to the 1.8 framing
        // that used to be inlined, so this is a no-op today but a single
        // point of truth for era-specific framing.
        std::vector<uint8_t> frame;
        if (session.protocol != nullptr)
            frame = session.protocol->encodeFrame(body);
        else
        {
            // Pre-handshake session (client has not sent Handshake yet, e.g.
            // Server List Ping path): fall back to plain varint framing.
            Packet::writeVarInt(frame, static_cast<int32_t>(body.size()));
            frame.insert(frame.end(), body.begin(), body.end());
        }

        Core::Logger::debug("[NET SEND] frame fd=" + std::to_string(fd) +
                            " size=" + std::to_string(frame.size()) +
                            "B hex=" + hexPreview(frame.data(), frame.size()));

        session.sendBuffer.insert(session.sendBuffer.end(), frame.begin(), frame.end());
        Core::Logger::debug("[NET SEND] queued fd=" + std::to_string(fd) +
                            " pendingSendBytes=" + std::to_string(session.sendBuffer.size()));
        flushSendBuffer(session);
    }

    // Broadcast a semantic packet to every PLAY-state client. `ignoreFd`
    // lets the caller skip the source client (used by e.g. /tell so the
    // target's own message does not double-send).
    void NetworkManager::broadcastPacket(int32_t packetId, const std::vector<uint8_t> &payload, int ignoreFd)
    {
        Core::Logger::debug("[NET BCAST] enter id=" + hexId(packetId) +
                            " payload=" + std::to_string(payload.size()) + "B" +
                            " ignoreFd=" + std::to_string(ignoreFd));
        int dispatched = 0;
        int skipped = 0;
        for (const auto &[fd, ses] : m_clients)
        {
            if (ses.state == ClientState::PLAY && fd != ignoreFd)
            {
                sendPacket(fd, packetId, payload);
                ++dispatched;
            }
            else
            {
                ++skipped;
            }
        }
        Core::Logger::debug("[NET BCAST] done id=" + hexId(packetId) +
                            " dispatched=" + std::to_string(dispatched) +
                            " skipped=" + std::to_string(skipped));
    }

    // Number of sessions currently in ClientState::PLAY. Used by the
    // server-browser status datagram so master-server listings report
    // the same count the in-game tablist shows.
    int NetworkManager::countOnline() const
    {
        int n = 0;
        for (const auto &[fd, ses] : m_clients)
            if (ses.state == ClientState::PLAY)
                ++n;
        return n;
    }

} // namespace LightweightMC::Network
