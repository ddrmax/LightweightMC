#pragma once
#include <cstdint>

namespace LightweightMC::Network::Protocol
{
    // Canonical packet kinds shared across all supported Minecraft versions (1.7.2 -> 26.3 / proto 4 -> 777).
    // The integer value serves as the index into the generated per-era / per-protocol ID mapping arrays.
    // MUST stay in sync with tools/gen_protocol_data.py.
    enum class PacketKind : std::int32_t
    {
        UNKNOWN = -1,

        // =====================================================================
        // S2C (Server -> Client) Packets
        // =====================================================================
        // --- Core & Common Handshake/Login/Play ---
        DISCONNECT = 0,            // 0x00 Handshake / Login / Play
        KEEP_ALIVE,                // S2C keep-alive ping
        JOIN_GAME,                 // Join game / Login Success
        CHAT,                      // System / Player chat message
        ENTITY_EQUIPMENT,          // Equipment updates
        SPAWN_POSITION,            // World spawn location
        UPDATE_PLAYER_INFO,        // Player list / Tab list update
        PLAYER_POS_LOOK,           // Server-enforced position & look
        BLOCK_CHANGE,              // Single block update
        ANIMATION,                 // Entity animation
        ADD_PLAYER,                // Spawn player entity
        OPEN_WINDOW,               // Open inventory / container
        WINDOW_ITEMS,              // Bulk inventory slot contents
        SET_SLOT,                  // Single slot update
        CONFIRM_TRANSACTION,       // Window transaction confirmation
        SCOREBOARD_OBJECTIVE,      // Scoreboard objective management
        UPDATE_SCORE,              // Scoreboard score update
        DISPLAY_SCOREBOARD,        // Scoreboard display placement
        PLAYER_LIST_HEADER_FOOTER, // Tab list custom header/footer

        // --- Protocol-47 (1.8.x) & Legacy Era (1.7 - 1.8.9) ---
        SET_HEALTH,              // Health / Food update
        UPDATE_HUD,              // Experience bar & HUD status
        UPDATE_HEALTH_FOOD,      // Separate health/food packet (proto 47)
        EXPLOSION,               // Explosion particle & knockback
        ENTITY_ACTION,           // Entity action broadcast
        START_OBSCURED_BY_BLOCK, // Underwater / suffocation overlay on
        END_OBSCURED_BY_BLOCK,   // Underwater / suffocation overlay off
        ENTITY_LOOK,             // Entity head/body look rotation
        ENTITY_TELEPORT,         // Absolute entity position/rotation
        ENTITY_RELATIVE_MOVE,    // Entity relative delta move & look
        SPAWN_ENTITY,            // Spawn object / vehicle / mob
        SPAWN_PAINTING,          // Legacy spawn painting (merged in 1.8.6)
        DAMAGE_ENTITY_EFFECT,    // Damage effect / knockback flash
        HEART_PARTICLE,          // Legacy heart particle
        BLOCK_CHANGE_1_7,        // Legacy 1.7 block change variant
        MAP_CHUNK_BULK,          // Multi-chunk compressed column data
        GAME_INFO,               // Difficulty / game status change
        SET_COOL_DOWN,           // Item cooldown trigger
        SUCCEEDING_BURST,        // Legacy world border effect
        SET_CENTER_CHUNK,        // Center chunk location
        SIGN_EDIT,               // Open sign editor
        SET_TITLE,               // Title & subtitle display
        PLAYER_ABILITIES,        // Server-side abilities update
        TAB_LIST_REMOVE,         // Player remove from tab list
        SET_DEFAULT_SPAWN,       // Default spawn position update
        SET_CARRIED_MINECART,    // Legacy minecart carrying state
        PLAYER_TIME,             // Time of day sync
        UPDATE_BLOCK_ENTITY,     // Tile entity NBT update
        SET_PARTICLE,            // World particle display

        SPAWN_EXPERIENCE_ORB,  // Spawn XP orb
        ENTITY_VELOCITY,       // Entity motion vector
        DESTROY_ENTITIES,      // Bulk destroy entities
        ENTITY_MOVEMENT,       // Base entity movement packet
        ENTITY_HEAD_LOOK,      // Entity head yaw rotation
        ENTITY_STATUS,         // Entity status byte
        ATTACH_ENTITY,         // Vehicle/leash attachment
        ENTITY_METADATA,       // Entity metadata index map
        ENTITY_EFFECT,         // Apply potion effect
        REMOVE_ENTITY_EFFECT,  // Remove potion effect
        SET_EXPERIENCE,        // XP level/progress bar update
        ENTITY_PROPERTIES,     // Entity attributes & modifiers
        CHUNK_DATA,            // Single chunk column data
        MULTI_BLOCK_CHANGE,    // Batched block updates in chunk
        BLOCK_ACTION,          // Block state action (chest/piston)
        BLOCK_BREAK_ANIMATION, // Block damage progress frame
        EFFECT,                // Sound & visual effect
        PARTICLE,              // Modern particle packet
        CHANGE_GAME_STATE,     // Gamemode, rain, demo state
        SPAWN_GLOBAL_ENTITY,   // Lightning bolt entity
        CLOSE_WINDOW,          // Server-forced window close
        UPDATE_SIGN,           // Sign line text sync
        MAP,                   // Map item canvas data
        WORLD_BORDER,          // World border center/radius/warning
        COMBAT_EVENT,          // Combat start/end/death event
        CAMERA,                // Spectator camera lock
        RESOURCE_PACK,         // Resource pack push

        // --- Modern Era (1.9 -> 1.20.4 / Proto 107 -> 765) ---
        BOSS_BAR,                   // Boss bar add/remove/update (1.9+)
        COOLDOWN,                   // Dedicated item cooldown (1.9+)
        NAMED_SOUND_EFFECT,         // Sound effect by resource location (1.9+)
        UNLOAD_CHUNK,               // Explicit chunk unload command (1.9+)
        SET_PASSENGERS,             // Multi-passenger entity mounting (1.9+)
        SOUND_EFFECT,               // Registry-based sound effect (1.9+)
        TAB_COMPLETE_S2C,           // Auto-complete suggestion payload (1.9+)
        DECLARE_COMMANDS,           // Command node tree graph (1.13+)
        DECLARE_TAGS,               // Registry tags sync (1.13+)
        STOP_SOUND,                 // Stop sound instance (1.13+)
        UPDATE_LIGHT,               // Dynamic lighting section data (1.14+)
        TRADE_LIST,                 // Villager GUI trade options (1.14+)
        OPEN_BOOK,                  // Force open written book (1.14+)
        ACKNOWLEDGE_PLAYER_DIGGING, // Client dig verification ack (1.14.2+)
        PING_S2C,                   // Play-state network ping measurement (1.17+)
        SIMULATION_DISTANCE,        // Simulation distance sync (1.18+)
        SET_DISPLAY_CHAT_PREVIEW,   // Chat preview flag (1.19+)
        SERVER_DATA,                // MOTD & icon packet (1.19+)
        CHAT_SUGGESTIONS,           // Chat completion suggestions (1.19.1+)
        DISGUISED_CHAT,             // Filtered / masked chat message (1.19.2+)
        BUNDLE_DELIMITER,           // Atomic packet bundle delimiter (1.19.4+)
        DAMAGE_EVENT,               // Direct damage event notification (1.19.4+)
        HURT_ANIMATION,             // Damage hurt animation direction (1.19.4+)
        CHUNK_BIOMES,               // Paletted biome chunk data batch (1.20+)

        // --- Configuration State & Latest Era (1.20.5 -> 26.3 / Proto 766 -> 777) ---
        COOKIE_REQUEST,          // Client cookie store query (1.20.5+)
        STORE_COOKIE,            // Store key-value cookie on client (1.20.5+)
        TRANSFER,                // Server transfer redirect (1.20.5+)
        CONFIG_CUSTOM_PAYLOAD,   // Configuration state plugin channel payload
        CONFIG_DISCONNECT,       // Configuration state disconnect
        CONFIG_FINISH,           // End configuration state / enter play state
        CONFIG_KEEP_ALIVE,       // Configuration state keep alive
        CONFIG_PING,             // Configuration state ping
        CONFIG_REGISTRY_DATA,    // Dynamic registries data push (1.20.5+)
        CONFIG_RESOURCE_PACK,    // Configuration state resource pack request
        UPDATE_ENABLED_FEATURES, // Feature flags update (1.20.5+)
        PROJECTILE_POWER,        // Wind charge / projectile power vector (1.21+)
        CUSTOM_REPORT_DETAILS,   // Custom crash/report metadata (1.21.2+)
        SERVER_LINKS,            // Server help/website links (1.21.2+)

        // =====================================================================
        // C2S (Client -> Server) Packets
        // =====================================================================
        // --- Core & Legacy C2S ---
        KEEP_ALIVE_RESPONSE = 500, // C2S keep-alive response
        PLAYER_DIGGING,            // Block dig state / drop item
        PLACE_BLOCK,               // Right click block placement
        HELD_ITEM,                 // Hotbar slot selection
        CLICK_WINDOW,              // Inventory slot click action
        CREATIVE_SLOT_ACTION,      // Creative mode item drop/pick
        FLYING,                    // Position + look + ground flag
        MOVEMENT,                  // Position-only movement
        LOOK_ONLY,                 // Look-only rotation
        TELEMETRY,                 // Legacy client telemetry payload
        ARM_ANIMATION,             // Arm swing notification

        CHAT_MESSAGE_C2S,        // Chat text or command string
        USE_ENTITY,              // Entity attack / interact
        PLAYER_MOVEMENT,         // Ground-flag-only packet
        CLOSE_WINDOW_C2S,        // Window close confirmation
        CONFIRM_TRANSACTION_C2S, // Inventory action ack
        ENCHANT_ITEM,            // Enchantment table button selection
        UPDATE_SIGN_C2S,         // Sign text submit
        CLIENT_ABILITIES,        // Flight / flying speed toggle
        TAB_COMPLETE,            // Command auto-complete request
        CLIENT_SETTINGS,         // Language, view distance, skin flags
        CLIENT_STATUS,           // Respawn / stat screen request
        PLUGIN_MESSAGE_C2S,      // Custom plugin channel data
        SPECTATE,                // Teleport to target entity in spectator
        RESOURCE_PACK_STATUS,    // Resource pack download/apply state

        // --- Modern & Configuration C2S (1.9 -> 26.3) ---
        TELEPORT_CONFIRM,          // Client position lock ack (1.9+)
        STEER_BOAT,                // Boat paddle controls (1.9+)
        VEHICLE_MOVE,              // Vehicle position & rotation (1.9+)
        C2S_RECIPE_BOOK_DATA,      // Recipe state / filtering (1.12+)
        SEEN_ADVANCEMENTS,         // Advancements tab interaction (1.12+)
        COMMAND_SUGGESTION_C2S,    // Command tree completion request (1.13+)
        SELECT_TRADE,              // Merchant trade index select (1.14+)
        SET_BEACON_EFFECT,         // Beacon primary/secondary selection (1.14+)
        SET_DIFFICULTY_C2S,        // Singleplayer difficulty change (1.14+)
        LOCK_DIFFICULTY_C2S,       // Lock difficulty toggle (1.14+)
        PONG_C2S,                  // Play-state ping response (1.17+)
        CHAT_COMMAND,              // Explicit command execution packet (1.19+)
        CHAT_ACK,                  // Chat signature acknowledgment (1.19.2+)
        CONFIG_ACK,                // Client configuration state transition ack (1.20.2+)
        COOKIE_RESPONSE,           // Cookie store data response (1.20.5+)
        CONFIG_CUSTOM_PAYLOAD_C2S, // Configuration plugin channel payload
        CONFIG_FINISH_C2S,         // Configuration state completion ack
        CONFIG_KEEP_ALIVE_C2S,     // Configuration state keep alive response
        CONFIG_PONG_C2S,           // Configuration state pong response
        CONFIG_RESOURCE_PACK_C2S,  // Configuration resource pack status
        CLIENT_TICK_END,           // Client frame/tick boundary signal (1.21.2+)

        COUNT
    };
}