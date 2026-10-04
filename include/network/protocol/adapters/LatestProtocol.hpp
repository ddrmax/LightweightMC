#pragma once
#include "network/protocol/BaseProtocol.hpp"

namespace LightweightMC::Network::Protocol
{
    // ---------------------------------------------------------------------------
    // LatestProtocol — 1.20.5+ (protocol 764+).
    //
    // Placeholder for the newest era: identity and packet ID mapping use the
    // generated LATEST era tables. Version-specific overrides (chat signing,
    // entity metadata changes, ...) are added here during the walk-up as each
    // protocol number is validated with /testpackets.
    class LatestProtocol : public BaseProtocol
    {
    public:
        static LatestProtocol &instance()
        {
            static LatestProtocol inst;
            return inst;
        }

        int32_t protocolVersion() const override { return 769; } // representative (1.20.5+)
        Era era() const override { return Era::LATEST; }
        std::string versionName() const override { return "1.20.5+"; }

        int32_t s2cPacketId(PacketKind kind) const override
        {
            return ProtocolVersion::s2cPacketId(Era::LATEST, kind);
        }

        PacketKind c2sPacketKind(int32_t wireId) const override
        {
            return ProtocolVersion::c2sPacketKind(Era::LATEST, wireId);
        }
    };
} // namespace LightweightMC::Network::Protocol