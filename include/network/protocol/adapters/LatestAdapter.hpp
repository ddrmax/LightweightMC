#pragma once
#include "network/protocol/EraAdapter.hpp"

namespace LightweightMC::Network::Protocol
{
    // Adapter for the LATEST era (1.21+).
    // Inherits modern block/item/chunk formats; 1.20.5+ added a few field deltas
    // (e.g. new entity metadata, chat command signing) that are handled at the
    // packet level rather than in block/item encoding.
    class LatestAdapter final : public EraAdapter
    {
    public:
        Era era() const override { return Era::LATEST; }

        uint32_t encodeBlock(uint16_t internalId, [[maybe_unused]] uint8_t meta) const override
        {
            return static_cast<uint32_t>(internalId); // block state id (same as modern)
        }

        void decodeBlock(uint32_t encoded, uint16_t &internalId, uint8_t &meta) const override
        {
            internalId = static_cast<uint16_t>(encoded);
            meta = 0;
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
            // Same palette-based format as modern (1.20+ chunk format unchanged through 1.21).
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

        int32_t advertisedProtocol() const override { return 769; } // 1.20.5+ baseline

        static LatestAdapter &instance()
        {
            static LatestAdapter inst;
            return inst;
        }
    };
}
