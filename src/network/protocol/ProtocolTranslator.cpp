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
        int16_t c = internalItemId;
        switch (internalItemId)
        {
            case -1: c = -1; break;    // NONE -> client empty marker
            case 0:  c = 334; break;   // LEATHER -> leather (id 0 was the vanilla no-item sentinel that desynced the JVM 1.8 InventoryWindow: the login crash)
            case 1:  c = 363; break;   // MEAT_RAW -> beef
            case 2:  c = 364; break;   // MEAT_COOKED -> steak
            case 8:  c = 367; break;   // BONE -> rotten_flesh (no 1.8 bone)
            case 9:  c = 334; break;   // HIDE -> leather
            case 13: c = 338; break;   // REED -> sugar canes
            case 14: c = 337; break;   // CLAY -> clay_ball
            case 20: c = 5; break;    // OAK_PLANK -> wooden planks
            case 50: c = 297; break;   // BREAD -> bread
            case 51: c = 364; break;   // COOKED_MEAT -> steak
            case 52: c = 350; break;   // COOKED_FISH -> cooked fish
            case 53: c = 282; break;   // STEW -> mushroom_stew
            default: c = (internalItemId > 0) ? 367 : -1;   // LWMC-only ids -> benign valid nonzero 1.8 code, decoder stays aligned
        }
        return TranslatedItem{c, count, damage};
    }

    TranslatedItem ProtocolTranslator::translateItemFromClient(int clientProtocolVersion, int16_t clientItemId, uint8_t count, int16_t damage)
    {
        (void)clientProtocolVersion;
        return TranslatedItem{clientItemId, count, damage};
    }
}

