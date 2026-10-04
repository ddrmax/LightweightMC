#pragma once
#include "network/protocol/BaseProtocol.hpp"

namespace LightweightMC::Network::Protocol
{
    // ---------------------------------------------------------------------------
    // ModernProtocol — 1.13 through 1.20.x (protocol 393 - 769).
    //
    // Placeholder for the version walk-up: identity and packet ID mapping use the
    // generated MODERN era tables. Field-level overrides (packed positions from
    // 1.9+, JSON chat from 1.13+, NBT items, login handshake fields) are added
    // here one version at a time as each protocol number is validated with
    // /testpackets. Until then the BaseProtocol defaults keep behavior identical
    // to the legacy path.
    class ModernProtocol : public BaseProtocol
    {
    public:
        static ModernProtocol &instance()
        {
            static ModernProtocol inst;
            return inst;
        }

        int32_t protocolVersion() const override { return 491; } // representative (1.14)
        Era era() const override { return Era::MODERN; }
        std::string versionName() const override { return "1.13-1.20.x"; }

        int32_t s2cPacketId(PacketKind kind) const override
        {
            return ProtocolVersion::s2cPacketId(Era::MODERN, kind);
        }

        PacketKind c2sPacketKind(int32_t wireId) const override
        {
            return ProtocolVersion::c2sPacketKind(Era::MODERN, wireId);
        }
    };
} // namespace LightweightMC::Network::Protocol