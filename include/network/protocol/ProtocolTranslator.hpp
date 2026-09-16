#pragma once
#include <cstdint>

namespace LightweightMC::Network::Protocol
{
    struct TranslatedBlock
    {
        uint16_t id{0};
        uint8_t data{0};

        // Standard 16-bit encoding for Minecraft chunk format (BlockID << 4 | Metadata)
        uint16_t toCombinedData() const
        {
            return (id << 4) | (data & 0x0F);
        }
    };

    struct TranslatedItem
    {
        int16_t id{-1};
        uint8_t count{0};
        int16_t damage{0};
    };

    class ProtocolTranslator
    {
    public:
        // Converts a block from internal representation to client format (depending on protocol version)
        static TranslatedBlock translateBlockToClient(int targetProtocolVersion, uint16_t internalBlockId, uint8_t internalData = 0);
        
        // Converts a block from client format to internal representation
        static TranslatedBlock translateBlockFromClient(int clientProtocolVersion, uint16_t clientBlockId, uint8_t clientData = 0);

        // Converts an item from internal representation to client format
        static TranslatedItem translateItemToClient(int targetProtocolVersion, int16_t internalItemId, uint8_t count = 1, int16_t damage = 0);

        // Converts an item from client format to internal representation
        static TranslatedItem translateItemFromClient(int clientProtocolVersion, int16_t clientItemId, uint8_t count = 1, int16_t damage = 0);
    };
}

