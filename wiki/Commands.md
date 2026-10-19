# Command reference

Each row is one array element `{command, aliases, usage, op_level, notes}` extracted from
the `Command` subclasses in `include/commands/*.hpp`. Handlers are registered in
`src/core/Server.cpp` (`Server::initServerCommands()`).

| Column | Meaning |
| --- | --- |
| Command | Registered verb — `Command::getName()`; prefix it with `/`. |
| Aliases | Extra verbs mapped to the same handler by `CommandManager::registerCommand()`. |
| Usage | `Command::getUsage()`, echoed back when argument syntax is wrong. |
| Op level | `Command::getRequiredOpLevel()`: minimum operator level allowed to run it. |
| Notes | Side effects, packets touched, known quirks. |
| Status | Handler wiring as compiled today: `wired` = performs a real backend action (subsystem call or `CommandContext` hook), `ack-only` = parses + replies with no backend yet, `stub (TODO in handler)` = an explicit `// TODO:` is left in the handler. |

Registered verbs: **84** (85 classes declared, 1 not registered).

## Basic

Source: `include/commands/BasicCommands.hpp`.

| Command | Aliases | Usage | Op level | Status | Description | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `help` | /? | `/help` | 0 | wired | Displays the list of available commands. | — |
| `stop` | /end, /shutdown, /exit | `/stop` | 4 | wired | Shuts down the server properly. | — |
| `reload` | — | `/reload` | 4 | wired | Reloads configuration files and modules. | — |
| `ping` | /pong | `/ping` | 0 | ack-only | Latency check -- replies with a short acknowledgement. | — |
| `hud` | /stats | `/hud` | 0 | wired | Pushes the scoreboard / tablist HUD to the caller. | — |

## Communication

Source: `include/commands/CommunicationCommands.hpp`.

| Command | Aliases | Usage | Op level | Status | Description | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `me` | — | `/me <action>` | 0 | ack-only | Displays an action message about the sender. | — |
| `particle` | — | `/particle <name> [pos]` | 2 | ack-only | Creates visual particles. | — |
| `playsound` | — | `/playsound <sound> <source> <targets>` | 2 | ack-only | Plays a sound. | — |
| `stopsound` | — | `/stopsound <targets> [source] [sound]` | 2 | ack-only | Stops a sound. | — |
| `teammsg` | /tm | `/teammsg <message>` | 0 | ack-only | Send a message to your team. | — |
| `tellraw` | — | `/tellraw <targets> <json>` | 2 | ack-only | Displays a raw JSON message to players. | — |
| `title` | — | `/title <target> <title\|subtitle\|actionbar\|clear\|reset> ...` | 2 | ack-only | Manages the titles displayed on the screen. | — |

## Debug & diagnostics

Source: `include/commands/DebugCommands.hpp`.

| Command | Aliases | Usage | Op level | Status | Description | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `debug` | — | `/debug <start\|stop>` | 3 | ack-only | Start or stop a debugging session. | — |
| `jfr` | — | `/jfr. <start\|stop>` | 4 | ack-only | Starts or stops JFR profiling. | Usage string has a stray dot (`/jfr.`); the registered verb is `jfr`. |
| `perf` | — | `/perf <start\|stop>` | 4 | ack-only | Captures metrics for 10 seconds. | — |

## Entity

Source: `include/commands/EntityCommands.hpp`.

| Command | Aliases | Usage | Op level | Status | Description | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `bossbar` | — | `/bossbar <add\|remove\|set\|get\|list>` | 2 | ack-only | Create and modify boss bars. | — |
| `damage` | — | `/damage <target> <amount> [damageType]` | 2 | ack-only | Deal damage to specific entities. | — |
| `kill` | — | `/kill [targets]` | 2 | ack-only | Kill entities. | — |
| `loot` | — | `/loot <target> <source>` | 2 | ack-only | Places items from an inventory slot on the ground. | — |
| `ride` | — | `/ride <target> <mount\|dismount>` | 2 | ack-only | Moves entities up or down. | — |
| `watch` | — | `/watch [target] [player]` | 2 | ack-only | Observe an entity in spectator mode. | Implemented by `SpectateCommand`; the verb is `/watch`. |
| `spreadplayers` | — | `/spreadplayers <center> <spreadDistance> <maxRange> <respectTeams> <targets>` | 2 | ack-only | Teleports entities to random locations. | — |
| `summon` | — | `/summon <entity> [pos] [nbt]` | 2 | ack-only | Summons an entity. | — |
| `data` | — | `/data <get\|merge\|modify\|remove> ...` | 2 | ack-only | Gets, merges, modifies and deletes NBT data. | — |
| `tag` | — | `/tag <target> <add\|remove\|list> [name]` | 2 | ack-only | Controls entity tags. | — |
| `team` | — | `/team <add\|remove\|join\|leave\|empty\|list\|option> ...` | 2 | ack-only | Control the teams. | — |
| `scoreboard` | /Sb | `/scoreboard <objectives\|players\|teams> ...` | 2 | ack-only | Manage scoreboard goals and players. | Alias is capitalised: `/Sb` works, `/sb` does not. |

## Player

Source: `include/commands/PlayerCommands.hpp`.

| Command | Aliases | Usage | Op level | Status | Description | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `advancement` | — | `/advancement <grant\|revoke> <target>` | 2 | ack-only | Grant, withdraw or check player progress. | — |
| `attribute` | — | `/attribute <target> <attribute> ...` | 2 | ack-only | Views, adds, removes, or sets an entity attribute. | — |
| `clear` | — | `/clear [targets] [item]` | 2 | ack-only | Removes items from the player's inventory. | — |
| `effect` | — | `/effect <give\|clear> <targets> <effect>` | 2 | ack-only | Adds or removes status effects. | — |
| `enchant` | — | `/enchant <target> <enchantment> [level]` | 2 | ack-only | Adds an enchantment to a player's selected item. | — |
| `experience` | /xp | `/experience <add\|set\|query> <targets> <amount>` | 2 | ack-only | Adds or removes experience from the player. | — |
| `gamemode` | /gm | `/gamemode <survival\|creative\|adventure\|spectator> [target]` | 2 | wired | Defines a player's game mode. | — |
| `give` | — | `/give <target> <item> [count]` | 2 | ack-only | Give an item to a player. | — |
| `item` | — | `/item <replace\|modify> ...` | 2 | ack-only | Manipulates items in inventories. | — |
| `recipe` | — | `/recipe <give\|take> <target> <recipe\|*>` | 2 | ack-only | Give or take away recipes from a player. | — |

## Scripting

Source: `include/commands/ScriptingCommands.hpp`.

| Command | Aliases | Usage | Op level | Status | Description | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `execute` | — | `/execute <subcommand>` | 2 | ack-only | Executes another command under certain conditions. | — |
| `function` | — | `/function <name>` | 2 | ack-only | Executes a datapack function. | — |
| `return` | — | `/return <value\|fail\|run>` | 2 | ack-only | Controls the flow of execution in functions. | — |
| `schedule` | — | `/schedule <function> <time>` | 2 | ack-only | Delays the execution of a function. | — |
| `trigger` | — | `/trigger <objective> [add\|set] [value]` | 0 | ack-only | Sets a trigger to be activated by a non-op player. | — |

## Server & session

Source: `include/commands/ServerCommands.hpp`.

| Command | Aliases | Usage | Op level | Status | Description | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `antiscan` | /hunter | `/antiscan <reload\|stats\|block\|unblock> [ip]` | 3 | wired | Manages AntiScan protection and blocklist. | — |
| `tp` | — | `/tp <player> <target> OR /tp [player] <x> <y> <z> ('~' = keep current axis)` | 2 | wired | Teleports a player to another player or coordinates. | — |
| `version` | /about, /ver | `/version` | 0 | ack-only | Shows Minecraft server and protocol version. | — |
| `list` | /Online, /Who | `/list` | 0 | stub (TODO in handler) | Shows the list of online players. | — |
| `msg` | /Tell, /W, /Pm | `/msg <player> <message>` | 0 | stub (TODO in handler) | Send a private message to a player. | — |
| `say` | /Broadcast, /Bc | `/say <message>` | 2 | stub (TODO in handler) | Sends a global message broadcast as a server. | — |
| `op` | — | `/op <player>` | 3 | wired | Gives administrator rights to a player. | — |
| `deop` | — | `/deop <player>` | 3 | wired | Removes administrator rights from a player. | — |
| `whitelist` | /Wl | `/whitelist <add\|remove\|on\|off> [player]` | 3 | stub (TODO in handler) | Manage server whitelist. | — |
| `kick` | — | `/kick <player> [reason]` | 2 | stub (TODO in handler) | Kicks a player from the server. | — |
| `ban` | — | `/ban <player> [reason]` | 3 | stub (TODO in handler) | Bans a player from the server. | — |
| `ban-ip` | — | `/ban-ip <player\|IP> [reason]` | 3 | stub (TODO in handler) | Bans a player's IP address. | — |
| `pardon` | /Humban | `/pardon <pseudo>` | 3 | stub (TODO in handler) | Unban a player. | — |
| `pardon-ip` | /Unban ip | `/pardon-ip <IP>` | 3 | stub (TODO in handler) | Unbans an IP address. | — |
| `save-all` | /Save | `/save-all` | 4 | stub (TODO in handler) | Forces immediate saving of the world to disk. | — |
| `plugins` | /Pl | `/plugins [list\|load <so-path>\|unload <name>]` | 0 | wired (Phase 11 loader) | List or load/unload native `.so` plugins. | Registered lowercase: type `/plugins`; the class usage string shows `/Plugins ...` leading-uppercase form in help output. Lookup is case-sensitive. |
| `lwmc` | — | `/lwmc <entity list\|track>` | 3 | stub (TODO in handler) | Diagnostic commands and advanced server management. | — |
| `banlist` | — | `/banlist [ips\|players]` | 3 | ack-only | Shows the ban list. | — |
| `data pack` | — | `/datapack <list\|enable\|disable>` | 2 | ack-only | Checks loaded data packs. | Quirk: `getName()` returns `data pack` (with a space) but the tokenizer only reads the first token, so `/datapack` resolves to `/data` instead. |
| `defaultgamemode` | — | `/defaultgamemode <survival\|creative\|adventure\|spectator>` | 2 | ack-only | Sets the default game mode. | — |
| `difficulty` | — | `/difficulty <peaceful\|easy\|normal\|hard>` | 2 | ack-only | Sets the difficulty level. | — |
| `gamerule` | — | `/gamerule <rule> [value]` | 2 | wired | Sets or consults the value of a game rule. | — |
| `publish` | — | `/publish [port]` | 4 | ack-only | Opens a single player world to a local network. | — |
| `save-off` | — | `/save-off` | 4 | ack-only | Disable automatic server backups. | — |
| `save-on` | — | `/save-on` | 4 | ack-only | Enables automatic server backups. | — |
| `setidletimeout` | — | `/setidletimeout <minutes>` | 4 | stub (TODO in handler) | Sets the time before inactive players are kicked out. | — |

## System

Source: `include/commands/SystemCommands.hpp`.

| Command | Aliases | Usage | Op level | Status | Description | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `compute` | — | `/compute <provider> ...` | 2 | stub (TODO in handler) | Evaluates an internal number provider directly. | **Declared but not registered** in `Server::initServerCommands()` — unreachable in current builds. |

## Protocol test

Source: `include/commands/TestPacketsCommand.hpp`.

| Command | Aliases | Usage | Op level | Status | Description | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `testpackets` | /tpk, /packettest | `/testpackets [player]` | 3 | wired | Builds and logs the S2C test-packet set for protocol validation. | — |

## World

Source: `include/commands/WorldCommands.hpp`.

| Command | Aliases | Usage | Op level | Status | Description | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `clone` | — | `/clone <begin> <end> <destination>` | 2 | ack-only | Copying blocks from one location to another. | — |
| `fill` | — | `/fill <from> <to> <block>` | 2 | ack-only | Fills an area with a specific block. | — |
| `fillbiome` | — | `/fillbiome <from> <to> <biome>` | 2 | ack-only | Fills an area with a specific biome. | — |
| `forceload` | — | `/forceload <add\|remove\|remove all\|query>` | 2 | ack-only | Forces chunks to remain loaded permanently. | — |
| `locate` | — | `/locate <structure\|biome\|poi> <name>` | 2 | ack-only | Locate the nearest structure, biome or POINT. | — |
| `place` | — | `/place <feature\|structure\|template\|jigsaw> ...` | 2 | ack-only | Place a feature, jigsaw or structure. | — |
| `seed` | — | `/Seed` | 2 | ack-only | Shows the world seed. | Registered lowercase: type `/seed`; the usage string shows `/Seed` (cosmetic). |
| `setblock` | — | `/setblock <pos> <block>` | 2 | ack-only | Replaces a block with another block. | — |
| `setworldspawn` | — | `/setworldspawn [pos]` | 2 | ack-only | Defines the world spawn point. | — |
| `spawnpoint` | — | `/spawnpoint [targets] [pos]` | 2 | ack-only | Defines the spawn point of a player. | — |
| `time` | — | `/time <set\|add\|query> <value>` | 2 | ack-only | Change or view the world's game time. | — |
| `weather` | — | `/weather <clear\|rain\|thunder> [duration]` | 2 | ack-only | Defined by the weather. | — |
| `worldborder` | — | `/worldborder <center\|set\|add\|...> ...` | 2 | ack-only | Manages the edge of the world. | — |
| `gotor` | /gowe, /gworld | `/gotor <worldName> OR /gotor <player> <worldName>` | 2 | wired | Switches a player to another registered dimension plane. | — |
| `world` | — | `/world <list\|create <name> <style> [--inventory ...] [--force-gamemode ...] [--seed ...]\|info <name>>` | 3 | wired | Manages registered dimension planes (list \| create \| info). 0.0.15 phase 4 grew /world create's --inventory / --force-gamemode / --seed flags and a `flat` style alias. | — |

---

## Machine-readable command array

```json
[
  {
    "command": "help",
    "aliases": [
      "?"
    ],
    "usage": "/help",
    "op_level": 0,
    "description": "Displays the list of available commands.",
    "class": "HelpCommand",
    "source": "BasicCommands.hpp",
    "registered": true,
    "status": "wired",
    "notes": ""
  },
  {
    "command": "hud",
    "aliases": [
      "stats"
    ],
    "usage": "/hud",
    "op_level": 0,
    "description": "Pushes the scoreboard / tablist HUD to the caller.",
    "class": "HudCommand",
    "source": "BasicCommands.hpp",
    "registered": true,
    "status": "wired",
    "notes": ""
  },
  {
    "command": "list",
    "aliases": [
      "Online",
      "Who"
    ],
    "usage": "/list",
    "op_level": 0,
    "description": "Shows the list of online players.",
    "class": "ListCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "stub (TODO in handler)",
    "notes": ""
  },
  {
    "command": "me",
    "aliases": [],
    "usage": "/me <action>",
    "op_level": 0,
    "description": "Displays an action message about the sender.",
    "class": "MeCommand",
    "source": "CommunicationCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "msg",
    "aliases": [
      "Tell",
      "W",
      "Pm"
    ],
    "usage": "/msg <player> <message>",
    "op_level": 0,
    "description": "Send a private message to a player.",
    "class": "MsgCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "stub (TODO in handler)",
    "notes": ""
  },
  {
    "command": "ping",
    "aliases": [
      "pong"
    ],
    "usage": "/ping",
    "op_level": 0,
    "description": "Latency check -- replies with a short acknowledgement.",
    "class": "PingCommand",
    "source": "BasicCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "plugins",
    "aliases": [
      "Pl"
    ],
    "usage": "/Plugins [list|load <so-path>|unload <name>]",
    "op_level": 0,
    "description": "List all installed plugins.",
    "class": "PluginsCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "wired (Phase 11 loader; hook invocation deferred to Phase 30)",
    "notes": "Registered lowercase: type `/plugins`; the usage string shows `/Plugins` (cosmetic). Lookup is case-sensitive."
  },
  {
    "command": "teammsg",
    "aliases": [
      "tm"
    ],
    "usage": "/teammsg <message>",
    "op_level": 0,
    "description": "Send a message to your team.",
    "class": "TeammsgCommand",
    "source": "CommunicationCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "trigger",
    "aliases": [],
    "usage": "/trigger <objective> [add|set] [value]",
    "op_level": 0,
    "description": "Sets a trigger to be activated by a non-op player.",
    "class": "TriggerCommand",
    "source": "ScriptingCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "version",
    "aliases": [
      "about",
      "ver"
    ],
    "usage": "/version",
    "op_level": 0,
    "description": "Shows Minecraft server and protocol version.",
    "class": "VersionCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "advancement",
    "aliases": [],
    "usage": "/advancement <grant|revoke> <target>",
    "op_level": 2,
    "description": "Grant, withdraw or check player progress.",
    "class": "AdvancementCommand",
    "source": "PlayerCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "attribute",
    "aliases": [],
    "usage": "/attribute <target> <attribute> ...",
    "op_level": 2,
    "description": "Views, adds, removes, or sets an entity attribute.",
    "class": "AttributeCommand",
    "source": "PlayerCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "bossbar",
    "aliases": [],
    "usage": "/bossbar <add|remove|set|get|list>",
    "op_level": 2,
    "description": "Create and modify boss bars.",
    "class": "BossbarCommand",
    "source": "EntityCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "clear",
    "aliases": [],
    "usage": "/clear [targets] [item]",
    "op_level": 2,
    "description": "Removes items from the player's inventory.",
    "class": "ClearCommand",
    "source": "PlayerCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "clone",
    "aliases": [],
    "usage": "/clone <begin> <end> <destination>",
    "op_level": 2,
    "description": "Copying blocks from one location to another.",
    "class": "CloneCommand",
    "source": "WorldCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "compute",
    "aliases": [],
    "usage": "/compute <provider> ...",
    "op_level": 2,
    "description": "Evaluates an internal number provider directly.",
    "class": "ComputeCommand",
    "source": "SystemCommands.hpp",
    "registered": false,
    "status": "stub (TODO in handler)",
    "notes": ""
  },
  {
    "command": "damage",
    "aliases": [],
    "usage": "/damage <target> <amount> [damageType]",
    "op_level": 2,
    "description": "Deal damage to specific entities.",
    "class": "DamageCommand",
    "source": "EntityCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "data",
    "aliases": [],
    "usage": "/data <get|merge|modify|remove> ...",
    "op_level": 2,
    "description": "Gets, merges, modifies and deletes NBT data.",
    "class": "DataCommand",
    "source": "EntityCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "data pack",
    "aliases": [],
    "usage": "/datapack <list|enable|disable>",
    "op_level": 2,
    "description": "Checks loaded data packs.",
    "class": "DatapackCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": "Quirk: `getName()` returns `data pack` (with a space) but the tokenizer only reads the first token, so `/datapack` resolves to `/data` instead."
  },
  {
    "command": "defaultgamemode",
    "aliases": [],
    "usage": "/defaultgamemode <survival|creative|adventure|spectator>",
    "op_level": 2,
    "description": "Sets the default game mode.",
    "class": "DefaultGamemodeCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "difficulty",
    "aliases": [],
    "usage": "/difficulty <peaceful|easy|normal|hard>",
    "op_level": 2,
    "description": "Sets the difficulty level.",
    "class": "DifficultyCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "effect",
    "aliases": [],
    "usage": "/effect <give|clear> <targets> <effect>",
    "op_level": 2,
    "description": "Adds or removes status effects.",
    "class": "EffectCommand",
    "source": "PlayerCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "enchant",
    "aliases": [],
    "usage": "/enchant <target> <enchantment> [level]",
    "op_level": 2,
    "description": "Adds an enchantment to a player's selected item.",
    "class": "EnchantCommand",
    "source": "PlayerCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "execute",
    "aliases": [],
    "usage": "/execute <subcommand>",
    "op_level": 2,
    "description": "Executes another command under certain conditions.",
    "class": "ExecuteCommand",
    "source": "ScriptingCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "experience",
    "aliases": [
      "xp"
    ],
    "usage": "/experience <add|set|query> <targets> <amount>",
    "op_level": 2,
    "description": "Adds or removes experience from the player.",
    "class": "ExperienceCommand",
    "source": "PlayerCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "fill",
    "aliases": [],
    "usage": "/fill <from> <to> <block>",
    "op_level": 2,
    "description": "Fills an area with a specific block.",
    "class": "FillCommand",
    "source": "WorldCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "fillbiome",
    "aliases": [],
    "usage": "/fillbiome <from> <to> <biome>",
    "op_level": 2,
    "description": "Fills an area with a specific biome.",
    "class": "FillBiomeCommand",
    "source": "WorldCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "forceload",
    "aliases": [],
    "usage": "/forceload <add|remove|remove all|query>",
    "op_level": 2,
    "description": "Forces chunks to remain loaded permanently.",
    "class": "ForceloadCommand",
    "source": "WorldCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "function",
    "aliases": [],
    "usage": "/function <name>",
    "op_level": 2,
    "description": "Executes a datapack function.",
    "class": "FunctionCommand",
    "source": "ScriptingCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "gamemode",
    "aliases": [
      "gm"
    ],
    "usage": "/gamemode <survival|creative|adventure|spectator> [target]",
    "op_level": 2,
    "description": "Defines a player's game mode.",
    "class": "GamemodeCommand",
    "source": "PlayerCommands.hpp",
    "registered": true,
    "status": "wired",
    "notes": ""
  },
  {
    "command": "gamerule",
    "aliases": [],
    "usage": "/gamerule <rule> [value]",
    "op_level": 2,
    "description": "Sets or consults the value of a game rule.",
    "class": "GameruleCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "wired",
    "notes": ""
  },
  {
    "command": "give",
    "aliases": [],
    "usage": "/give <target> <item> [count]",
    "op_level": 2,
    "description": "Give an item to a player.",
    "class": "GiveCommand",
    "source": "PlayerCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "gotor",
    "aliases": [
      "gowe",
      "gworld"
    ],
    "usage": "/gotor <worldName> OR /gotor <player> <worldName>",
    "op_level": 2,
    "description": "Switches a player to another registered dimension plane.",
    "class": "GotorCommand",
    "source": "WorldCommands.hpp",
    "registered": true,
    "status": "wired",
    "notes": ""
  },
  {
    "command": "item",
    "aliases": [],
    "usage": "/item <replace|modify> ...",
    "op_level": 2,
    "description": "Manipulates items in inventories.",
    "class": "ItemCommand",
    "source": "PlayerCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "kick",
    "aliases": [],
    "usage": "/kick <player> [reason]",
    "op_level": 2,
    "description": "Kicks a player from the server.",
    "class": "KickCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "stub (TODO in handler)",
    "notes": ""
  },
  {
    "command": "kill",
    "aliases": [],
    "usage": "/kill [targets]",
    "op_level": 2,
    "description": "Kill entities.",
    "class": "KillCommand",
    "source": "EntityCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "locate",
    "aliases": [],
    "usage": "/locate <structure|biome|poi> <name>",
    "op_level": 2,
    "description": "Locate the nearest structure, biome or POINT.",
    "class": "LocateCommand",
    "source": "WorldCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "loot",
    "aliases": [],
    "usage": "/loot <target> <source>",
    "op_level": 2,
    "description": "Places items from an inventory slot on the ground.",
    "class": "LootCommand",
    "source": "EntityCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "particle",
    "aliases": [],
    "usage": "/particle <name> [pos]",
    "op_level": 2,
    "description": "Creates visual particles.",
    "class": "ParticleCommand",
    "source": "CommunicationCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "place",
    "aliases": [],
    "usage": "/place <feature|structure|template|jigsaw> ...",
    "op_level": 2,
    "description": "Place a feature, jigsaw or structure.",
    "class": "PlaceCommand",
    "source": "WorldCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "playsound",
    "aliases": [],
    "usage": "/playsound <sound> <source> <targets>",
    "op_level": 2,
    "description": "Plays a sound.",
    "class": "PlaysoundCommand",
    "source": "CommunicationCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "recipe",
    "aliases": [],
    "usage": "/recipe <give|take> <target> <recipe|*>",
    "op_level": 2,
    "description": "Give or take away recipes from a player.",
    "class": "RecipeCommand",
    "source": "PlayerCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "return",
    "aliases": [],
    "usage": "/return <value|fail|run>",
    "op_level": 2,
    "description": "Controls the flow of execution in functions.",
    "class": "ReturnCommand",
    "source": "ScriptingCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "ride",
    "aliases": [],
    "usage": "/ride <target> <mount|dismount>",
    "op_level": 2,
    "description": "Moves entities up or down.",
    "class": "RideCommand",
    "source": "EntityCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "say",
    "aliases": [
      "Broadcast",
      "Bc"
    ],
    "usage": "/say <message>",
    "op_level": 2,
    "description": "Sends a global message broadcast as a server.",
    "class": "SayCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "stub (TODO in handler)",
    "notes": ""
  },
  {
    "command": "schedule",
    "aliases": [],
    "usage": "/schedule <function> <time>",
    "op_level": 2,
    "description": "Delays the execution of a function.",
    "class": "ScheduleCommand",
    "source": "ScriptingCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "scoreboard",
    "aliases": [
      "Sb"
    ],
    "usage": "/scoreboard <objectives|players|teams> ...",
    "op_level": 2,
    "description": "Manage scoreboard goals and players.",
    "class": "ScoreboardCommand",
    "source": "EntityCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": "Alias is capitalised: `/Sb` works, `/sb` does not."
  },
  {
    "command": "seed",
    "aliases": [],
    "usage": "/Seed",
    "op_level": 2,
    "description": "Shows the world seed.",
    "class": "SeedCommand",
    "source": "WorldCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": "Registered lowercase: type `/seed`; the usage string shows `/Seed` (cosmetic)."
  },
  {
    "command": "setblock",
    "aliases": [],
    "usage": "/setblock <pos> <block>",
    "op_level": 2,
    "description": "Replaces a block with another block.",
    "class": "SetBlockCommand",
    "source": "WorldCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "setworldspawn",
    "aliases": [],
    "usage": "/setworldspawn [pos]",
    "op_level": 2,
    "description": "Defines the world spawn point.",
    "class": "SetWorldSpawnCommand",
    "source": "WorldCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "spawnpoint",
    "aliases": [],
    "usage": "/spawnpoint [targets] [pos]",
    "op_level": 2,
    "description": "Defines the spawn point of a player.",
    "class": "SpawnPointCommand",
    "source": "WorldCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "spreadplayers",
    "aliases": [],
    "usage": "/spreadplayers <center> <spreadDistance> <maxRange> <respectTeams> <targets>",
    "op_level": 2,
    "description": "Teleports entities to random locations.",
    "class": "SpreadPlayersCommand",
    "source": "EntityCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "stopsound",
    "aliases": [],
    "usage": "/stopsound <targets> [source] [sound]",
    "op_level": 2,
    "description": "Stops a sound.",
    "class": "StopsoundCommand",
    "source": "CommunicationCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "summon",
    "aliases": [],
    "usage": "/summon <entity> [pos] [nbt]",
    "op_level": 2,
    "description": "Summons an entity.",
    "class": "SummonCommand",
    "source": "EntityCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "tag",
    "aliases": [],
    "usage": "/tag <target> <add|remove|list> [name]",
    "op_level": 2,
    "description": "Controls entity tags.",
    "class": "TagCommand",
    "source": "EntityCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "team",
    "aliases": [],
    "usage": "/team <add|remove|join|leave|empty|list|option> ...",
    "op_level": 2,
    "description": "Control the teams.",
    "class": "TeamCommand",
    "source": "EntityCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "tellraw",
    "aliases": [],
    "usage": "/tellraw <targets> <json>",
    "op_level": 2,
    "description": "Displays a raw JSON message to players.",
    "class": "TellrawCommand",
    "source": "CommunicationCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "time",
    "aliases": [],
    "usage": "/time <set|add|query> <value>",
    "op_level": 2,
    "description": "Change or view the world's game time.",
    "class": "TimeCommand",
    "source": "WorldCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "title",
    "aliases": [],
    "usage": "/title <target> <title|subtitle|actionbar|clear|reset> ...",
    "op_level": 2,
    "description": "Manages the titles displayed on the screen.",
    "class": "TitleCommand",
    "source": "CommunicationCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "tp",
    "aliases": [],
    "usage": "/tp <player> <target> OR /tp [player] <x> <y> <z> ('~' = keep current axis)",
    "op_level": 2,
    "description": "Teleports a player to another player or coordinates.",
    "class": "TeleportCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "wired",
    "notes": ""
  },
  {
    "command": "watch",
    "aliases": [],
    "usage": "/watch [target] [player]",
    "op_level": 2,
    "description": "Observe an entity in spectator mode.",
    "class": "SpectateCommand",
    "source": "EntityCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": "Implemented by `SpectateCommand`; the verb is `/watch`."
  },
  {
    "command": "weather",
    "aliases": [],
    "usage": "/weather <clear|rain|thunder> [duration]",
    "op_level": 2,
    "description": "Defined by the weather.",
    "class": "WeatherCommand",
    "source": "WorldCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "worldborder",
    "aliases": [],
    "usage": "/worldborder <center|set|add|...> ...",
    "op_level": 2,
    "description": "Manages the edge of the world.",
    "class": "WorldBorderCommand",
    "source": "WorldCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "antiscan",
    "aliases": [
      "hunter"
    ],
    "usage": "/antiscan <reload|stats|block|unblock> [ip]",
    "op_level": 3,
    "description": "Manages AntiScan protection and blocklist.",
    "class": "AntiScanCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "wired",
    "notes": ""
  },
  {
    "command": "ban",
    "aliases": [],
    "usage": "/ban <player> [reason]",
    "op_level": 3,
    "description": "Bans a player from the server.",
    "class": "BanCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "stub (TODO in handler)",
    "notes": ""
  },
  {
    "command": "ban-ip",
    "aliases": [],
    "usage": "/ban-ip <player|IP> [reason]",
    "op_level": 3,
    "description": "Bans a player's IP address.",
    "class": "BanIpCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "stub (TODO in handler)",
    "notes": ""
  },
  {
    "command": "banlist",
    "aliases": [],
    "usage": "/banlist [ips|players]",
    "op_level": 3,
    "description": "Shows the ban list.",
    "class": "BanlistCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "debug",
    "aliases": [],
    "usage": "/debug <start|stop>",
    "op_level": 3,
    "description": "Start or stop a debugging session.",
    "class": "DebugCommand",
    "source": "DebugCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "deop",
    "aliases": [],
    "usage": "/deop <player>",
    "op_level": 3,
    "description": "Removes administrator rights from a player.",
    "class": "DeopCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "wired",
    "notes": ""
  },
  {
    "command": "lwmc",
    "aliases": [],
    "usage": "/lwmc <entity list|track>",
    "op_level": 3,
    "description": "Diagnostic commands and advanced server management.",
    "class": "LwmcCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "stub (TODO in handler)",
    "notes": ""
  },
  {
    "command": "op",
    "aliases": [],
    "usage": "/op <player>",
    "op_level": 3,
    "description": "Gives administrator rights to a player.",
    "class": "OpCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "wired",
    "notes": ""
  },
  {
    "command": "pardon",
    "aliases": [
      "Humban"
    ],
    "usage": "/pardon <pseudo>",
    "op_level": 3,
    "description": "Unban a player.",
    "class": "PardonCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "stub (TODO in handler)",
    "notes": ""
  },
  {
    "command": "pardon-ip",
    "aliases": [
      "Unban ip"
    ],
    "usage": "/pardon-ip <IP>",
    "op_level": 3,
    "description": "Unbans an IP address.",
    "class": "PardonIpCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "stub (TODO in handler)",
    "notes": ""
  },
  {
    "command": "testpackets",
    "aliases": [
      "tpk",
      "packettest"
    ],
    "usage": "/testpackets [player]",
    "op_level": 3,
    "description": "Builds and logs the S2C test-packet set for protocol validation.",
    "class": "TestPacketsCommand",
    "source": "TestPacketsCommand.hpp",
    "registered": true,
    "status": "wired",
    "notes": ""
  },
  {
    "command": "whitelist",
    "aliases": [
      "Wl"
    ],
    "usage": "/whitelist <add|remove|on|off> [player]",
    "op_level": 3,
    "description": "Manage server whitelist.",
    "class": "WhitelistCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "stub (TODO in handler)",
    "notes": ""
  },
  {
    "command": "world",
    "aliases": [],
    "usage": "/world <list|create <name> <style> [--inventory ...] [--force-gamemode ...] [--seed ...]|info <name>>",
    "op_level": 3,
    "description": "Manages registered dimension planes (list | create | info). 0.0.15 phase 4: /world create grew --inventory / --force-gamemode / --seed flags plus a flat->CREATIVE style alias.",
    "class": "WorldCommand",
    "source": "WorldCommands.hpp",
    "registered": true,
    "status": "wired",
    "notes": ""
  },
  {
    "command": "jfr",
    "aliases": [],
    "usage": "/jfr. <start|stop>",
    "op_level": 4,
    "description": "Starts or stops JFR profiling.",
    "class": "JfrCommand",
    "source": "DebugCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": "Usage string has a stray dot (`/jfr.`); the registered verb is `jfr`."
  },
  {
    "command": "perf",
    "aliases": [],
    "usage": "/perf <start|stop>",
    "op_level": 4,
    "description": "Captures metrics for 10 seconds.",
    "class": "PerfCommand",
    "source": "DebugCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "publish",
    "aliases": [],
    "usage": "/publish [port]",
    "op_level": 4,
    "description": "Opens a single player world to a local network.",
    "class": "PublishCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "reload",
    "aliases": [],
    "usage": "/reload",
    "op_level": 4,
    "description": "Reloads configuration files and modules.",
    "class": "ReloadCommand",
    "source": "BasicCommands.hpp",
    "registered": true,
    "status": "wired",
    "notes": ""
  },
  {
    "command": "save-all",
    "aliases": [
      "Save"
    ],
    "usage": "/save-all",
    "op_level": 4,
    "description": "Forces immediate saving of the world to disk.",
    "class": "SaveAllCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "stub (TODO in handler)",
    "notes": ""
  },
  {
    "command": "save-off",
    "aliases": [],
    "usage": "/save-off",
    "op_level": 4,
    "description": "Disable automatic server backups.",
    "class": "SaveOffCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "save-on",
    "aliases": [],
    "usage": "/save-on",
    "op_level": 4,
    "description": "Enables automatic server backups.",
    "class": "SaveOnCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "ack-only",
    "notes": ""
  },
  {
    "command": "setidletimeout",
    "aliases": [],
    "usage": "/setidletimeout <minutes>",
    "op_level": 4,
    "description": "Sets the time before inactive players are kicked out.",
    "class": "SetIdleTimeoutCommand",
    "source": "ServerCommands.hpp",
    "registered": true,
    "status": "stub (TODO in handler)",
    "notes": ""
  },
  {
    "command": "stop",
    "aliases": [
      "end",
      "shutdown",
      "exit"
    ],
    "usage": "/stop",
    "op_level": 4,
    "description": "Shuts down the server properly.",
    "class": "StopCommand",
    "source": "BasicCommands.hpp",
    "registered": true,
    "status": "wired",
    "notes": ""
  }
]
```

## Implementation status (as compiled today)

`wired` means the handler performs a real backend action — a subsystem call (`NetworkManager`, `GameRules`, `WorldStorage`, `AntiScan`, …) or a `CommandContext` hook injected by the server (`setOpLevel`, `setGameMode`, `teleportPlayerToCoords`, `switchPlayerWorld`, …). `ack-only` means it parses arguments and replies, but no backend action exists yet: **the reply you see on screen is not proof that anything changed**. Treat those verbs as roadmap items before relying on them:

| Status | Count | Verbs |
| --- | --- | --- |
| ack-only | 58 | `/advancement`, `/attribute`, `/banlist`, `/bossbar`, `/clear`, `/clone`, `/damage`, `/data`, `/data pack`, `/debug`, `/defaultgamemode`, `/difficulty`, `/effect`, `/enchant`, `/execute`, `/experience`, `/fill`, `/fillbiome`, `/forceload`, `/function`, `/give`, `/item`, `/jfr`, `/kill`, `/locate`, `/loot`, `/me`, `/particle`, `/perf`, `/ping`, `/place`, `/playsound`, `/publish`, `/recipe`, `/return`, `/ride`, `/save-off`, `/save-on`, `/schedule`, `/scoreboard`, `/seed`, `/setblock`, `/setworldspawn`, `/spawnpoint`, `/spreadplayers`, `/stopsound`, `/summon`, `/tag`, `/team`, `/teammsg`, `/tellraw`, `/time`, `/title`, `/trigger`, `/version`, `/watch`, `/weather`, `/worldborder` |
| stub (TODO in handler) | 13 | `/ban`, `/ban-ip`, `/compute`, `/kick`, `/list`, `/lwmc`, `/msg`, `/pardon`, `/pardon-ip`, `/save-all`, `/say`, `/setidletimeout`, `/whitelist` |
| wired | 14 | `/antiscan`, `/deop`, `/gamemode`, `/gamerule`, `/gotor`, `/help`, `/hud`, `/op`, `/plugins`, `/reload`, `/stop`, `/testpackets`, `/tp`, `/world` |

(85 classes are scanned; the single **not registered** class additionally has no handler call site.)

## Op-level distribution

| Op level | Who can run it | Count | Verbs |
| --- | --- | --- | --- |
| 0 | Everyone — plain players, visitors, whitelisted clients. | 10 | `/help`, `/hud`, `/list`, `/me`, `/msg`, `/ping`, `/plugins`, `/teammsg`, `/trigger`, `/version` |
| 2 | Operator — in-game admin verbs. | 53 | `/advancement`, `/attribute`, `/bossbar`, `/clear`, `/clone`, `/compute`, `/damage`, `/data`, `/data pack`, `/defaultgamemode`, `/difficulty`, `/effect`, `/enchant`, `/execute`, `/experience`, `/fill`, `/fillbiome`, `/forceload`, `/function`, `/gamemode`, `/gamerule`, `/give`, `/gotor`, `/item`, `/kick`, `/kill`, `/locate`, `/loot`, `/particle`, `/place`, `/playsound`, `/recipe`, `/return`, `/ride`, `/say`, `/schedule`, `/scoreboard`, `/seed`, `/setblock`, `/setworldspawn`, `/spawnpoint`, `/spreadplayers`, `/stopsound`, `/summon`, `/tag`, `/team`, `/tellraw`, `/time`, `/title`, `/tp`, `/watch`, `/weather`, `/worldborder` |
| 3 | Trusted moderator — bans, whitelist, diagnostics. | 13 | `/antiscan`, `/ban`, `/ban-ip`, `/banlist`, `/debug`, `/deop`, `/lwmc`, `/op`, `/pardon`, `/pardon-ip`, `/testpackets`, `/whitelist`, `/world` |
| 4 | Server owner — console-level verbs (world save, profiler, publish). | 9 | `/jfr`, `/perf`, `/publish`, `/reload`, `/save-all`, `/save-off`, `/save-on`, `/setidletimeout`, `/stop` |

> No command requires level **1**, so the level-3 grant from `/op <player>` unlocks
> every level ≤ 3 verb. Level 0 verbs are usable by anyone in chat.

## How a command line is matched (`CommandManager::executeCommand`)

1. A leading `/` is stripped, so the chat path (`src/network/handlers/play/PlayerHandler.cpp`)
   and the console path use the same parser.
2. The line is tokenised with `std::istringstream`: arguments are **whitespace separated**, with
   **no quoting support** — multi-word arguments become separate tokens.
3. Lookup is an exact, **case-sensitive** hit against primary names *and* aliases.
4. On a miss the client receives `Unknown command. Type "/help" for help.`
5. On `ctx.opLevel < getRequiredOpLevel()` it receives `You do not have permission to execute this command.`
6. Handler exceptions are caught and logged; the client sees `An internal error occurred while executing this command.`
7. `/help` lists only verbs whose required level is ≤ the caller level.

Related pages: [Op levels](Op-Levels), [Configuration](Configuration), [Game Rules](Game-Rules), [Testing](Testing).

