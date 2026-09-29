#pragma once
#include <cstdint>
#include <string>

// Generated data tables (registry). Do not edit by hand -- regenerate with:
//   python3 tools/gen_protocol_data.py
#include "protocol/data/protocol_versions.h"
#include "protocol/data/packet_ids.h"

namespace LightweightMC::Network::Protocol
{
    // Facade over the generated protocol registry. All version knowledge lives in
    // the generated data tables; core logic should call these rather than hardcoding
    // protocol numbers or packet IDs.
    class ProtocolVersion
    {
    public:
        // Returns true if the given protocol number is a supported (registered) version.
        static bool isSupported(int32_t protocol)
        {
            return resolve(protocol) != nullptr;
        }

        // Exact-match lookup of a registered protocol number.
        static const ProtocolVersionInfo *resolve(int32_t protocol)
        {
            return ::LightweightMC::Network::Protocol::resolve(protocol);
        }

        // Closest registered version (for kick messages / fallback). Never nullptr if the table is non-empty.
        static const ProtocolVersionInfo *nearestSupported(int32_t protocol)
        {
            return ::LightweightMC::Network::Protocol::nearestSupported(protocol);
        }

        // Maps a protocol number to its era (exact match first, else nearest supported).
        static Era eraFor(int32_t protocol)
        {
            return ::LightweightMC::Network::Protocol::eraFor(protocol);
        }

        // Human-readable version string for a protocol number (e.g. "1.8").
        static std::string versionName(int32_t protocol)
        {
            const ProtocolVersionInfo *info = resolve(protocol);
            if (!info)
                info = nearestSupported(protocol);
            return info ? std::string(info->version) : std::string("unknown");
        }

        // The highest registered protocol number (used for the status handshake response).
        static int32_t maxProtocol()
        {
            if (kProtocolVersionCount == 0)
                return -1;
            int32_t max = kProtocolVersions[0].protocol;
            for (std::size_t i = 1; i < kProtocolVersionCount; ++i)
                if (kProtocolVersions[i].protocol > max)
                    max = kProtocolVersions[i].protocol;
            return max;
        }

        // S2C packet ID for a given era + kind. Returns -1 if unsupported in that era.
        static int32_t s2cPacketId(Era era, PacketKind kind)
        {
            return ::LightweightMC::Network::Protocol::s2cPacketId(era, kind);
        }

        // Maps a 1.8 wire ID (the literal IDs used throughout the codebase) to the equivalent
        // wire ID in the given era, via the generated reverse tables. Returns -1 if the ID is
        // not mapped for that era; callers should treat -1 as "pass the original ID through".
        static int32_t s2cWireIdFromLegacy(Era era, int32_t legacyWireId)
        {
            return ::LightweightMC::Network::Protocol::s2cWireIdFromLegacy(era, legacyWireId);
        }

        // Reverse C2S lookup: raw client packet id -> PacketKind (UNKNOWN if not recognised).
        static PacketKind c2sPacketKind(Era era, int32_t id)
        {
            return ::LightweightMC::Network::Protocol::c2sPacketKind(era, id);
        }
    };
}
