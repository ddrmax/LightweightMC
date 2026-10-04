#pragma once
#include "network/protocol/BaseProtocol.hpp"

namespace LightweightMC::Network::Protocol
{
    // ---------------------------------------------------------------------------
    // LegacyProtocol — 1.7.x through 1.12.x (protocol 4 - 340).
    //
    // This era matches the wire format the codebase currently speaks, so every
    // field encoding falls back to the BaseProtocol defaults (double/float
    // positions, §-code chat, UUID+name login success). Only the identity and
    // packet ID mapping are provided here; IDs come from the generated LEGACY
    // era tables.
    class LegacyProtocol : public BaseProtocol
    {
    public:
        static LegacyProtocol &instance()
        {
            static LegacyProtocol inst;
            return inst;
        }

        int32_t protocolVersion() const override { return 47; } // representative (1.8)
        Era era() const override { return Era::LEGACY; }
        std::string versionName() const override { return "1.7.x-1.12.x"; }

        int32_t s2cPacketId(PacketKind kind) const override
        {
            return ProtocolVersion::s2cPacketId(Era::LEGACY, kind);
        }

        PacketKind c2sPacketKind(int32_t wireId) const override
        {
            return ProtocolVersion::c2sPacketKind(Era::LEGACY, wireId);
        }
    };
} // namespace LightweightMC::Network::Protocol