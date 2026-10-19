# Op levels (permission model)

Every command carries a **required operator level** (`Command::getRequiredOpLevel()`); every caller
carries a **current level** (`CommandContext::opLevel`). A command runs when
`ctx.opLevel >= getRequiredOpLevel()` — see the exact gate in
[`Commands`](Commands#how-a-command-line-is-matched-commandmanagerexecutecommand).

## Level table

| Level | Name | Who gets it | Can use commands of level |
| --- | --- | --- | --- |
| 0 | Player | Default for every joining client (`PlayerSession::opLevel{0}`) | 0 |
| 1 | Helper | Reserved — no command currently requires exactly 1, so a level-1 grant behaves like level 0 | 0–1 |
| 2 | Operator | Granted by `/op <player>` | 0–2 (in-game admin verbs: `/give`, `/kill`, `/gamerule`, …) |
| 3 | Trusted moderator | Same grant as level 2 **plus** bans, whitelist, diagnostics | 0–3 |
| 4 | Server owner | The **console** context only (`Server.cpp` sets `.opLevel = 4`) | everything |

Implementation notes:

- `PlayerSession::isOperator()` is simply `opLevel > 0`.
- `PlayerSession::setOperator(bool isOp, int level = 3)` — the default grant is **3**.
- The console context is hard-coded to 4 in both `Server::processConsoleInput()` and the console thread
  in `Server::startConsoleThread()`; you cannot lower it.
- `/op` and `/deop` are clamped: `uint8_t clamped = static_cast<uint8_t>(std::max(0, std::min(4, level)))`.

## Command verbs

| Verb | Op | Behaviour |
| --- | --- | --- |
| `/op <player>` | 3 | Grants operator rights — calls `NetworkManager::setPlayerOpLevel(name, 3)` |
| `/deop <player>` | 3 | Revokes them — same setter with level 0 |

`NetworkManager::setPlayerOpLevel` (`src/network/NetworkPlayerMeta.cpp`) does three things:

1. Mutates the live `ClientSession::opLevel` of the online `ClientState::PLAY` client with that username.
2. Persists the row through `Player::PlayerManager::getPlayerUuid(...)` +
   `m_database.savePlayerData(data)` so the level **survives a re-login**.
3. Returns `foundOnline || persisted` — the command still counts as success when the player was
   offline but the database row was written.

## Chat vs console

| Path | Context | Effective level | Reply destination |
| --- | --- | --- | --- |
| In-game chat (`PlayerHandler.cpp`) | `isConsole = false`, `opLevel = session.opLevel` | the player's own level | an S2C chat packet (1.8 id `0x02`) to **that client only** |
| Console thread / stdin | `isConsole = true`, `opLevel = 4` | owner | `[Command] …` log line, plus a broadcast to every PLAY client **only** when the command declares `affectsPlayerState()` |

The broadcast rule is worth remembering when you test: a console `/say` or `/gamemode` reaches every
connected client, while `/reload` or `/stop` stay on the console because they do not touch player state.

## Practical recipes

| Goal | Steps |
| --- | --- |
| Promote a player from the console | `/op alice` (level → 3, persisted) |
| Demote them | `/deop alice` (level → 0) |
| Check what a level can run | `/help` — the listing is filtered by the caller's level |
| Promote without a console | Sign in as an already-operator account (persisted level) and run `/op` if its level ≤ yours |

> **Caveat:** `/op` and `/deop` are wired through the `CommandContext::setOpLevel` hook, so they work in
> the running server. Verbs marked `ack-only` in [Commands](Commands#implementation-status-as-compiled-today)
> still need their backends; a level grant is not the same as a working command.

Related: [Commands](Commands), [AntiScan & Security](AntiScan-and-Security).
