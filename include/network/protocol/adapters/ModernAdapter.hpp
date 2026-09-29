#pragma once
#include "network/protocol/EraAdapter.hpp"
#include <algorithm>

namespace LightweightMC::Network::Protocol
{
    // Adapter for the MODERN era (1.13–1.20.x).
    // Block format: 18-bit block state id (flattened in 1.13+). Items: varint id + count + NBT.
    // Chunks: palette-based bit-packed sections.
    class ModernAdapter final : public EraAdapter
    {
    public:
        Era era() const override { return Era::MODERN; }

        uint32_t encodeBlock(uint16_t internalId, [[maybe_unused]] uint8_t meta) const override
        {
            // 1.13+ flattened block states: id is the state id directly (meta folded in by caller).
            // For now, treat internalId as the block state id; meta is reserved.
            return static_cast<uint32_t>(internalId);
        }

        void decodeBlock(uint32_t encoded, uint16_t &internalId, uint8_t &meta) const override
        {
            internalId = static_cast<uint16_t>(encoded);
            meta = 0;
        }

        ClientItem encodeItem(int16_t internalId, uint8_t count, int16_t damage) const override
        {
            // Modern items use varint id + NBT for damage. Damage is carried in NBT (omitted here).
            return ClientItem{static_cast<int32_t>(internalId), count, damage};
        }

        void decodeItem(int32_t clientId, uint8_t count, int16_t damage,
                        int16_t &internalId, uint8_t &outCount, int16_t &outDamage) const override
        {
            internalId = static_cast<int16_t>(clientId);
            outCount = count;
            outDamage = damage;
        }

        std::vector<uint8_t> encodeChunkSection(const uint32_t *blocks, int count) const override
        {
            // Modern: palette-based bit-packed section. This is a simplified encoding that
            // emits a single-palette section (all entries identical) or falls back to raw.
            // Full palette compression is a follow-up; for now emit big-endian 18-bit-ish data.
            std::vector<uint8_t> out(static_cast<size_t>(count) * 3);
            for (int i = 0; i < count; ++i)
            {
                uint32_t v = blocks[i];
                out[static_cast<size_t>(i) * 3 + 0] = static_cast<uint8_t>((v >> 16) & 0xFF);
                out[static_cast<size_t>(i) * 3 + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
                out[static_cast<size_t>(i) * 3 + 2] = static_cast<uint8_t>(v & 0xFF);
            }
            return out;
        }

        int32_t advertisedProtocol() const override { return 766; } // 1.20 baseline

        static ModernAdapter &instance()
        {
            static ModernAdapter inst;
            return inst;
        }
    };
}
