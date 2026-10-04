#include "network/protocol/BaseProtocol.hpp"
#include "network/Packet.hpp"
#include "network/protocol/adapters/LegacyProtocol.hpp"
#include "network/protocol/adapters/ModernProtocol.hpp"
#include "network/protocol/adapters/LatestProtocol.hpp"

#include <cstring>

namespace LightweightMC::Network::Protocol
{
    // ---------------------------------------------------------------------------
    // Default framing: plain varint length prefix (no compression).
    // ---------------------------------------------------------------------------
    std::vector<uint8_t> BaseProtocol::encodeFrame(const std::vector<uint8_t> &body) const
    {
        std::vector<uint8_t> frame;
        Network::Packet::writeVarInt(frame, static_cast<int>(body.size()));
        frame.insert(frame.end(), body.begin(), body.end());
        return frame;
    }

    bool BaseProtocol::decodeFrame(const uint8_t *buf, size_t len, std::vector<uint8_t> &outBody) const
    {
        // Peek at the varint length first (without consuming).
        size_t off = 0;
        int packetLen = Network::Packet::readVarInt(buf, off);
        if (packetLen < 0 || off == len)
            return false;
        if (len < off + static_cast<size_t>(packetLen))
            return false; // Incomplete body — wait for more data.

        outBody.assign(buf + off, buf + off + packetLen);
        return true;
    }

    // ---------------------------------------------------------------------------
    // Default position encoding: 1.8 layout (double x/y/z, float yaw/pitch).
    // ---------------------------------------------------------------------------
    void BaseProtocol::encodePosition(std::vector<uint8_t> &out, double x, double y, double z, float yaw, float pitch) const
    {
        Network::Packet::writeDouble(out, x);
        Network::Packet::writeDouble(out, y);
        Network::Packet::writeDouble(out, z);
        Network::Packet::writeFloat(out, yaw);
        Network::Packet::writeFloat(out, pitch);
    }

    void BaseProtocol::decodePosition(const uint8_t *d, size_t &off, size_t end, double &x, double &y, double &z, float &yaw, float &pitch) const
    {
        x = Network::Packet::readDouble(d, off);
        y = Network::Packet::readDouble(d, off);
        z = Network::Packet::readDouble(d, off);
        std::memcpy(&yaw, d + off, 4);
        off += 4;
        std::memcpy(&pitch, d + off, 4);
        off += 4;
        (void)end;
    }

    // ---------------------------------------------------------------------------
    // Default Login Success: 1.8 layout (UUID without dashes + player name).
    // ---------------------------------------------------------------------------
    void BaseProtocol::buildLoginSuccess(std::vector<uint8_t> &out, const std::string &uuid, const std::string &name) const
    {
        Network::Packet::writeString(out, uuid);
        Network::Packet::writeString(out, name);
    }

    // ---------------------------------------------------------------------------
    // Factory: pick the protocol class matching the negotiated protocol number.
    // Singletons are process-lifetime (same pattern as EraAdapter::create).
    // ---------------------------------------------------------------------------
    BaseProtocol *BaseProtocol::create(int32_t protocolVersion)
    {
        if (protocolVersion >= 769)
            return &LatestProtocol::instance();
        if (protocolVersion >= 315)
            return &ModernProtocol::instance();
        return &LegacyProtocol::instance(); // 1.7.x - 1.12.x (and unknown low numbers)
    }
} // namespace LightweightMC::Network::Protocol