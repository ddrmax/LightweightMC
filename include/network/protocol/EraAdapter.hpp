#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "network/protocol/PacketKind.hpp"
#include "network/protocol/ProtocolVersion.hpp"

namespace LightweightMC::Network::Protocol
{
    // A versioned adapter for a single protocol era. All era-specific encoding
    // (block format, chunk packet layout, item NBT vs. short, etc.) lives behind
    // this interface so that core network logic stays era-agnostic.
    //
    // Implementations: LegacyAdapter (1.7.10–1.12.2), ModernAdapter (1.13–1.20.x),
    // LatestAdapter (1.21+). A factory (EraAdapter::create) returns the right one
    // for a given protocol number via ProtocolVersion::eraFor().
    class EraAdapter
    {
    public:
        virtual ~EraAdapter() = default;

        // The era this adapter handles.
        virtual Era era() const = 0;

        // --- Block translation (internal id/meta <-> client format) ---
        // Legacy: 16-bit combined (id << 4 | meta). Modern+: block state id.
        virtual uint32_t encodeBlock(uint16_t internalId, uint8_t meta) const = 0;
        virtual void decodeBlock(uint32_t encoded, uint16_t &internalId, uint8_t &meta) const = 0;

        // --- Item translation (internal <-> client format) ---
        struct ClientItem
        {
            int32_t id{-1};
            uint8_t count{0};
            int16_t damage{0};
        };
        virtual ClientItem encodeItem(int16_t internalId, uint8_t count, int16_t damage) const = 0;
        virtual void decodeItem(int32_t clientId, uint8_t count, int16_t damage,
                                int16_t &internalId, uint8_t &outCount, int16_t &outDamage) const = 0;

        // --- Chunk encoding (the biggest era difference) ---
        // Legacy: 16-bit block data per section. Modern+: palette-based bit-packed.
        virtual std::vector<uint8_t> encodeChunkSection(const uint32_t *blocks, int count) const = 0;

        // --- Status / handshake helpers ---
        // The protocol number to advertise in the status response for this era's client.
        virtual int32_t advertisedProtocol() const = 0;

        // Factory: pick the right adapter for a protocol number.
        static EraAdapter *create(int32_t protocol);
    };
}
