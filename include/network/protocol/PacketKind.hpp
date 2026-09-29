#pragma once
#include <cstdint>

namespace LightweightMC::Network::Protocol
{
    // Canonical packet kinds shared by S2C and C2S. The integer value is the index
    // into the generated per-era ID arrays (see src/protocol/data/packet_ids.h).
    // This list MUST stay in sync with PACKET_KINDS in tools/gen_protocol_data.py.
    enum class PacketKind : std::int32_t
    {
        UNKNOWN = -1,

        // --- S2C (server -> client) ---
        DISCONNECT,                // 0x00 handshake / 0x00 login
        KEEP_ALIVE,                // S2C keep-alive ping
        JOIN_GAME,                 // 0x01
        CHAT,                      // 0x02
        ENTITY_EQUIPMENT,          // 0x04
        SPAWN_POSITION,            // 0x05
        UPDATE_PLAYER_INFO,        // 0x38 (tab list add/remove)
        PLAYER_POS_LOOK,           // 0x08 (server -> client position/look)
        BLOCK_CHANGE,              // 0x21 / 0x23
        ANIMATION,                 // 0x0B
        ADD_PLAYER,                // 0x0C
        OPEN_WINDOW,               // 0x2D
        WINDOW_ITEMS,              // 0x30
        SET_SLOT,                  // 0x31
        CONFIRM_TRANSACTION,       // 0x32
        SCOREBOARD_OBJECTIVE,      // 0x3B
        UPDATE_SCORE,              // 0x3C
        DISPLAY_SCOREBOARD,        // 0x3D
        PLAYER_LIST_HEADER_FOOTER, // 0x47

        // --- C2S (client -> server) ---
        KEEP_ALIVE_RESPONSE,  // (alias for keep-alive echo)
        PLAYER_DIGGING,       // 0x07
        PLACE_BLOCK,          // 0x08
        HELD_ITEM,            // 0x09
        CLICK_WINDOW,         // 0x0E
        CREATIVE_SLOT_ACTION, // 0x10

        COUNT
    };
}
