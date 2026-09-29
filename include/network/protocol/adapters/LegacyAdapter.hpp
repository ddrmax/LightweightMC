#pragma once
#include "network/protocol/EraAdapter.hpp"

namespace LightweightMC::Network::Protocol
{
    // Adapter for the LEGACY era (1.7.10–1.12.2).
    // Block format: 16-bit combined (id << 4 | meta). Items: short id + byte count + short damage.
    class LegacyAdapter final : public EraAdapter
    {
    public:
        Era era() const override { return Era::LEGACY; }

        uint32_t encodeBlock(uint16_t internalId, uint8_t meta) const override
        {
            return (static_cast<uint32_t>(internalId) << 4) | (meta & 0x0F);
        }

        void decodeBlock(uint32_t encoded, uint16_t &internalId, uint8_t &meta) const override
        {
            internalId = static_cast<uint16_t>(encoded >> 4);
            meta = static_cast<uint8_t>(encoded & 0x0F);
        }

        ClientItem encodeItem(int16_t internalId, uint8_t count, int16_t damage) const override
        {
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
            // Legacy: big-endian 16-bit block data per section (4096 entries).
            std::vector<uint8_t> out(static_cast<size_t>(count) * 2);
            for (int i = 0; i < count; ++i)
            {
                uint16_t v = static_cast<uint16_t>(blocks[i]);
                out[static_cast<size_t>(i) * 2 + 0] = static_cast<uint8_t>((v >> 8) & 0xFF);
                out[static_cast<size_t>(i) * 2 + 1] = static_cast<uint8_t>(v & 0xFF);
            }
            return out;
        }

        int32_t advertisedProtocol() const override { return 47; } // 1.8 baseline

        static LegacyAdapter &instance()
        {
            static LegacyAdapter inst;
            return inst;
        }
    };
}
