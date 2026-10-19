# Game rules reference

Game rules live in `World::GameRules` (`src/world/GameRules.cpp`) and mirror Mojang's Java
rule set. Each entry is an array element `{name, type, default, category, notes}`; defaults
below are the vanilla values compiled into `registerDefaults()`.

- Names are matched **verbatim and case-sensitively** — legacy camel-case names such as `doFireTick` keep their spelling.
- `type` is `bool` or `int`; a persisted row is only applied when its stored type matches the compiled type.
- Read/write them with `/gamerule <rule> [value]` (op level 2), see [Commands](Commands).

## Chat

| Rule | Type | Default | Notes |
| --- | --- | --- | --- |
| `command_block_output` | bool | true | — |
| `log_admin_commands` | bool | true | — |
| `send_command_feedback` | bool | true | — |
| `show_advancement_messages` | bool | true | — |
| `show_death_messages` | bool | true | — |

## Drops

| Rule | Type | Default | Notes |
| --- | --- | --- | --- |
| `block_drops` | bool | true | — |
| `block_explosion_drop_decay` | bool | true | — |
| `entity_drops` | bool | true | — |
| `mob_drops` | bool | true | — |
| `mob_explosion_drop_decay` | bool | true | — |
| `projectiles_can_break_blocks` | bool | true | — |
| `tnt_explosion_drop_decay` | bool | false | — |

## Misc

| Rule | Type | Default | Notes |
| --- | --- | --- | --- |
| `command_blocks_work` | bool | true | — |
| `global_sound_events` | bool | true | — |
| `max_block_modifications` | int | 32768 | — |
| `max_command_forks` | int | 65536 | — |
| `max_command_sequence_length` | int | 65536 | — |
| `reduced_debug_info` | bool | false | — |
| `tnt_explodes` | bool | true | — |

## Player

| Rule | Type | Default | Notes |
| --- | --- | --- | --- |
| `forgive_dead_players` | bool | true | — |
| `max_entity_cramming` | int | 24 | World::EntityManager — per-chunk entity density cap. |
| `mob_griefing` | bool | true | World::EntityManager — mob/pixel targeting of players. |
| `raids` | bool | true | — |
| `universal_anger` | bool | false | World::EntityManager — species-wide aggro. |
| `allow_entering_nether_using_portals` | bool | true | — |
| `drowning_damage` | bool | true | — |
| `elytra_movement_check` | bool | true | — |
| `ender_pearls_vanish_on_death` | bool | true | — |
| `fall_damage` | bool | true | — |
| `fire_damage` | bool | true | — |
| `fire_spread_radius_around_player` | int | 128 | — |
| `freeze_damage` | bool | true | — |
| `immediate_respawn` | bool | false | — |
| `keep_inventory` | bool | false | — |
| `limited_crafting` | bool | false | — |
| `locator_bar` | bool | true | — |
| `natural_health_regeneration` | bool | true | — |
| `player_movement_check` | bool | true | — |
| `players_nether_portal_creative_delay` | int | 0 | — |
| `players_nether_portal_default_delay` | int | 80 | — |
| `players_sleeping_percentage` | int | 100 | — |
| `pvp` | bool | true | — |
| `respawn_radius` | int | 10 | — |
| `spectators_generate_chunks` | bool | true | — |

## Spawning

| Rule | Type | Default | Notes |
| --- | --- | --- | --- |
| `spawn_mobs` | bool | true | World::EntityManager — mob spawn pass. |
| `spawn_monsters` | bool | true | World::EntityManager — hostile spawn pass. |
| `spawn_patrols` | bool | true | World::EntityManager — patrol packs. |
| `spawn_phantoms` | bool | true | World::EntityManager — phantoms. |
| `spawn_wandering_traders` | bool | true | World::EntityManager — wandering traders. |
| `spawn_wardens` | bool | true | World::EntityManager — wardens. |
| `spawner_blocks_work` | bool | true | — |

## World Updates

| Rule | Type | Default | Notes |
| --- | --- | --- | --- |
| `advance_time` | bool | true | — |
| `advance_weather` | bool | true | — |
| `lava_source_conversion` | bool | false | — |
| `max_snow_accumulation_height` | int | 1 | — |
| `random_tick_speed` | int | 3 | — |
| `spread_vines` | bool | true | — |
| `water_source_conversion` | bool | true | — |

## Legacy / renamed rules still accepted by the server (1.13-1.20)

| Rule | Type | Default | Notes |
| --- | --- | --- | --- |
| `doFireTick` | bool | true | Renamed / pre-1.21 spelling kept for compatibility. |
| `functioncommandlimit` | int | 10000 | Renamed / pre-1.21 spelling kept for compatibility. |
| `recipesunlock` | bool | true | Renamed / pre-1.21 spelling kept for compatibility. |
| `respawnblocksexplode` | bool | true | Renamed / pre-1.21 spelling kept for compatibility. |
| `showbordereffect` | bool | true | Renamed / pre-1.21 spelling kept for compatibility. |
| `showcoordinates` | bool | true | Renamed / pre-1.21 spelling kept for compatibility. |
| `showdaysplayed` | bool | false | Renamed / pre-1.21 spelling kept for compatibility. |
| `showrecipemessages` | bool | true | Renamed / pre-1.21 spelling kept for compatibility. |
| `showtags` | bool | true | Renamed / pre-1.21 spelling kept for compatibility. |

## Persistence

- Compiled defaults are the base layer; overrides live in a SQLite `gamerules` table
  (columns `name`, `type`, `bool_value`, `int_value`) inside `server_data.db`.
- `GameRules::init()` overlays persisted rows **only when the stored type matches** the compiled type, so renamed or retyped rules fall back to vanilla defaults.
- Startup log: `[GAMERULES] Loaded N game rules (M persisted overrides).`

## Rules wired into engine subsystems

| Rule | Consumer | Behaviour |
| --- | --- | --- |
| `max_entity_cramming` | World::EntityManager — per-chunk entity density cap. | Engine falls back to the safe default. |
| `mob_griefing` | World::EntityManager — mob/pixel targeting of players. | Engine falls back to the safe default. |
| `universal_anger` | World::EntityManager — species-wide aggro. | Engine falls back to the safe default. |
| `spawn_mobs` | World::EntityManager — mob spawn pass. | Spawn / behaviour pass is skipped. |
| `spawn_monsters` | World::EntityManager — hostile spawn pass. | Spawn / behaviour pass is skipped. |
| `spawn_patrols` | World::EntityManager — patrol packs. | Spawn / behaviour pass is skipped. |
| `spawn_phantoms` | World::EntityManager — phantoms. | Spawn / behaviour pass is skipped. |
| `spawn_wandering_traders` | World::EntityManager — wandering traders. | Spawn / behaviour pass is skipped. |
| `spawn_wardens` | World::EntityManager — wardens. | Spawn / behaviour pass is skipped. |

## Machine-readable rules array

```json
[
  {
    "name": "command_block_output",
    "type": "bool",
    "default": "true",
    "category": "Chat"
  },
  {
    "name": "log_admin_commands",
    "type": "bool",
    "default": "true",
    "category": "Chat"
  },
  {
    "name": "send_command_feedback",
    "type": "bool",
    "default": "true",
    "category": "Chat"
  },
  {
    "name": "show_advancement_messages",
    "type": "bool",
    "default": "true",
    "category": "Chat"
  },
  {
    "name": "show_death_messages",
    "type": "bool",
    "default": "true",
    "category": "Chat"
  },
  {
    "name": "block_drops",
    "type": "bool",
    "default": "true",
    "category": "Drops"
  },
  {
    "name": "block_explosion_drop_decay",
    "type": "bool",
    "default": "true",
    "category": "Drops"
  },
  {
    "name": "entity_drops",
    "type": "bool",
    "default": "true",
    "category": "Drops"
  },
  {
    "name": "mob_drops",
    "type": "bool",
    "default": "true",
    "category": "Drops"
  },
  {
    "name": "mob_explosion_drop_decay",
    "type": "bool",
    "default": "true",
    "category": "Drops"
  },
  {
    "name": "projectiles_can_break_blocks",
    "type": "bool",
    "default": "true",
    "category": "Drops"
  },
  {
    "name": "tnt_explosion_drop_decay",
    "type": "bool",
    "default": "false",
    "category": "Drops"
  },
  {
    "name": "command_blocks_work",
    "type": "bool",
    "default": "true",
    "category": "Misc"
  },
  {
    "name": "global_sound_events",
    "type": "bool",
    "default": "true",
    "category": "Misc"
  },
  {
    "name": "max_block_modifications",
    "type": "int",
    "default": "32768",
    "category": "Misc"
  },
  {
    "name": "max_command_forks",
    "type": "int",
    "default": "65536",
    "category": "Misc"
  },
  {
    "name": "max_command_sequence_length",
    "type": "int",
    "default": "65536",
    "category": "Misc"
  },
  {
    "name": "reduced_debug_info",
    "type": "bool",
    "default": "false",
    "category": "Misc"
  },
  {
    "name": "tnt_explodes",
    "type": "bool",
    "default": "true",
    "category": "Misc"
  },
  {
    "name": "forgive_dead_players",
    "type": "bool",
    "default": "true",
    "category": "Player"
  },
  {
    "name": "max_entity_cramming",
    "type": "int",
    "default": "24",
    "category": "Player"
  },
  {
    "name": "mob_griefing",
    "type": "bool",
    "default": "true",
    "category": "Player"
  },
  {
    "name": "raids",
    "type": "bool",
    "default": "true",
    "category": "Player"
  },
  {
    "name": "universal_anger",
    "type": "bool",
    "default": "false",
    "category": "Player"
  },
  {
    "name": "allow_entering_nether_using_portals",
    "type": "bool",
    "default": "true",
    "category": "Player"
  },
  {
    "name": "drowning_damage",
    "type": "bool",
    "default": "true",
    "category": "Player"
  },
  {
    "name": "elytra_movement_check",
    "type": "bool",
    "default": "true",
    "category": "Player"
  },
  {
    "name": "ender_pearls_vanish_on_death",
    "type": "bool",
    "default": "true",
    "category": "Player"
  },
  {
    "name": "fall_damage",
    "type": "bool",
    "default": "true",
    "category": "Player"
  },
  {
    "name": "fire_damage",
    "type": "bool",
    "default": "true",
    "category": "Player"
  },
  {
    "name": "fire_spread_radius_around_player",
    "type": "int",
    "default": "128",
    "category": "Player"
  },
  {
    "name": "freeze_damage",
    "type": "bool",
    "default": "true",
    "category": "Player"
  },
  {
    "name": "immediate_respawn",
    "type": "bool",
    "default": "false",
    "category": "Player"
  },
  {
    "name": "keep_inventory",
    "type": "bool",
    "default": "false",
    "category": "Player"
  },
  {
    "name": "limited_crafting",
    "type": "bool",
    "default": "false",
    "category": "Player"
  },
  {
    "name": "locator_bar",
    "type": "bool",
    "default": "true",
    "category": "Player"
  },
  {
    "name": "natural_health_regeneration",
    "type": "bool",
    "default": "true",
    "category": "Player"
  },
  {
    "name": "player_movement_check",
    "type": "bool",
    "default": "true",
    "category": "Player"
  },
  {
    "name": "players_nether_portal_creative_delay",
    "type": "int",
    "default": "0",
    "category": "Player"
  },
  {
    "name": "players_nether_portal_default_delay",
    "type": "int",
    "default": "80",
    "category": "Player"
  },
  {
    "name": "players_sleeping_percentage",
    "type": "int",
    "default": "100",
    "category": "Player"
  },
  {
    "name": "pvp",
    "type": "bool",
    "default": "true",
    "category": "Player"
  },
  {
    "name": "respawn_radius",
    "type": "int",
    "default": "10",
    "category": "Player"
  },
  {
    "name": "spectators_generate_chunks",
    "type": "bool",
    "default": "true",
    "category": "Player"
  },
  {
    "name": "spawn_mobs",
    "type": "bool",
    "default": "true",
    "category": "Spawning"
  },
  {
    "name": "spawn_monsters",
    "type": "bool",
    "default": "true",
    "category": "Spawning"
  },
  {
    "name": "spawn_patrols",
    "type": "bool",
    "default": "true",
    "category": "Spawning"
  },
  {
    "name": "spawn_phantoms",
    "type": "bool",
    "default": "true",
    "category": "Spawning"
  },
  {
    "name": "spawn_wandering_traders",
    "type": "bool",
    "default": "true",
    "category": "Spawning"
  },
  {
    "name": "spawn_wardens",
    "type": "bool",
    "default": "true",
    "category": "Spawning"
  },
  {
    "name": "spawner_blocks_work",
    "type": "bool",
    "default": "true",
    "category": "Spawning"
  },
  {
    "name": "advance_time",
    "type": "bool",
    "default": "true",
    "category": "World Updates"
  },
  {
    "name": "advance_weather",
    "type": "bool",
    "default": "true",
    "category": "World Updates"
  },
  {
    "name": "lava_source_conversion",
    "type": "bool",
    "default": "false",
    "category": "World Updates"
  },
  {
    "name": "max_snow_accumulation_height",
    "type": "int",
    "default": "1",
    "category": "World Updates"
  },
  {
    "name": "random_tick_speed",
    "type": "int",
    "default": "3",
    "category": "World Updates"
  },
  {
    "name": "spread_vines",
    "type": "bool",
    "default": "true",
    "category": "World Updates"
  },
  {
    "name": "water_source_conversion",
    "type": "bool",
    "default": "true",
    "category": "World Updates"
  },
  {
    "name": "doFireTick",
    "type": "bool",
    "default": "true",
    "category": "Legacy / renamed rules still accepted by the server (1.13-1.20)"
  },
  {
    "name": "functioncommandlimit",
    "type": "int",
    "default": "10000",
    "category": "Legacy / renamed rules still accepted by the server (1.13-1.20)"
  },
  {
    "name": "recipesunlock",
    "type": "bool",
    "default": "true",
    "category": "Legacy / renamed rules still accepted by the server (1.13-1.20)"
  },
  {
    "name": "respawnblocksexplode",
    "type": "bool",
    "default": "true",
    "category": "Legacy / renamed rules still accepted by the server (1.13-1.20)"
  },
  {
    "name": "showbordereffect",
    "type": "bool",
    "default": "true",
    "category": "Legacy / renamed rules still accepted by the server (1.13-1.20)"
  },
  {
    "name": "showcoordinates",
    "type": "bool",
    "default": "true",
    "category": "Legacy / renamed rules still accepted by the server (1.13-1.20)"
  },
  {
    "name": "showdaysplayed",
    "type": "bool",
    "default": "false",
    "category": "Legacy / renamed rules still accepted by the server (1.13-1.20)"
  },
  {
    "name": "showrecipemessages",
    "type": "bool",
    "default": "true",
    "category": "Legacy / renamed rules still accepted by the server (1.13-1.20)"
  },
  {
    "name": "showtags",
    "type": "bool",
    "default": "true",
    "category": "Legacy / renamed rules still accepted by the server (1.13-1.20)"
  }
]
```

