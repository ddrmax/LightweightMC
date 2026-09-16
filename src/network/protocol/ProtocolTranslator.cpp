#include "network/protocol/ProtocolTranslator.hpp"

namespace LightweightMC::Network::Protocol
{
    TranslatedBlock ProtocolTranslator::translateBlockToClient(int targetProtocolVersion, uint16_t internalBlockId, uint8_t internalData)
    {
        (void)targetProtocolVersion; // Used later for the ViaVersion/ViaBackwards mapping table
        // For now, direct switch to 1.8 (protocol 47)
        return TranslatedBlock{internalBlockId, internalData};
    }

    TranslatedBlock ProtocolTranslator::translateBlockFromClient(int clientProtocolVersion, uint16_t clientBlockId, uint8_t clientData)
    {
        (void)clientProtocolVersion;
        return TranslatedBlock{clientBlockId, clientData};
    }

    TranslatedItem ProtocolTranslator::translateItemToClient(int targetProtocolVersion, int16_t internalItemId, uint8_t count, int16_t damage)
    {
        (void)targetProtocolVersion;
        return TranslatedItem{internalItemId, count, damage};
    }

    TranslatedItem ProtocolTranslator::translateItemFromClient(int clientProtocolVersion, int16_t clientItemId, uint8_t count, int16_t damage)
    {
        (void)clientProtocolVersion;
        return TranslatedItem{clientItemId, count, damage};
    }
}

