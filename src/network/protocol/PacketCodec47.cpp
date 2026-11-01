#include "network/protocol/PacketCodec47.hpp"

#include "core/Logger.hpp"
#include "network/Packet.hpp"

#include <cstring>
#include <sstream>
#include <unordered_map>

namespace LightweightMC::Network::Protocol
{

    // -----------------------------------------------------------------------
    // 0x39 Update Block Entity — Play state.
    // -----------------------------------------------------------------------
    std::vector<uint8_t> encodeUpdateBlockEntity(int32_t blockId, int32_t x, int32_t y,
                                                 int32_t z, uint8_t actionType,
                                                 const std::vector<uint8_t> &nbtData)
    {
        std::vector<uint8_t> payload;

        // int x
        payload.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
        payload.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
        payload.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>(x & 0xFF));

        // short y
        int16_t y16 = static_cast<int16_t>(y);
        payload.push_back(static_cast<uint8_t>((y16 >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>(y16 & 0xFF));

        // int z
        payload.push_back(static_cast<uint8_t>((z >> 24) & 0xFF));
        payload.push_back(static_cast<uint8_t>((z >> 16) & 0xFF));
        payload.push_back(static_cast<uint8_t>((z >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>(z & 0xFF));

        // byte action type
        payload.push_back(actionType);

        // byte[] nbtData (length-prefixed int32)
        uint32_t nbtLen = static_cast<uint32_t>(nbtData.size());
        payload.push_back(static_cast<uint8_t>((nbtLen >> 24) & 0xFF));
        payload.push_back(static_cast<uint8_t>((nbtLen >> 16) & 0xFF));
        payload.push_back(static_cast<uint8_t>((nbtLen >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>(nbtLen & 0xFF));
        payload.insert(payload.end(), nbtData.begin(), nbtData.end());

        (void)blockId; // blockId is informational for now; NBT drives behaviour.
        return payload;
    }

} // namespace LightweightMC::Network::Protocol
