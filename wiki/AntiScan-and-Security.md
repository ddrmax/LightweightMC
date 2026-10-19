# AntiScan & security

AntiScan is the server-side bot/scanner filter (`include/network/AntiScan.hpp`,
`src/network/AntiScan.cpp`). It is **not** an anticheat system: it drops connections that look like
HTTP scanners or known ad-network IPs before the server spends any protocol work on them.

## Configuration

| Key (`server.properties`) | Default | Effect |
| --- | --- | --- |
| `enable-antiscan` | `true` | Master switch, read in `NetworkManager::start()` and in `ReloadCommand` |
| `blocklist-url` | `https://raw.githubusercontent.com/pebblehost/hunter/master/ips.txt` | Remote blocklist (PebbleHost "hunter" list); empty string disables the download |

## Files on disk

| File | Created by | Purpose |
| --- | --- | --- |
| `antiscan_ips.txt` | AntiScan cache | The merged/loaded blocklist kept locally so a restart does not need the network |
| `custom_ips.txt` | `/antiscan block <ip>` (`AntiScan::addCustomIp`) | Locally-added IPs, persisted across restarts |

Both are plain text, one entry per line, loaded into a `std::unordered_set<std::string>` guarded by a
`std::shared_mutex` (`getBlockedCount()` reports the in-memory size).

## How a connection is screened

1. `NetworkManager::start(port)` calls `AntiScan::init(url)` when `enable-antiscan` is true: it loads the
   local cache, merges `custom_ips.txt`, and refreshes the remote list.
2. Every accepted socket is checked with `AntiScan::isBlocked(ip, port)`.
3. A hit goes to `AntiScan::handleBlockedClient(fd, ip, port)`, which:
   - logs `[AntiScan] Blocked scanner/bot attempt from <ip>:<port> (Fake HTTP 500 sent)`,
   - writes a **decoy `HTTP/1.1 500 Internal Server Error`** response (nginx-style body),
   - `close()`s the socket.

The decoy is deliberate: scanners that probe the port as if it were a web server record a plausible
HTTP failure instead of an odd TCP hang-up, so they keep rating the host as "not a game server".

## Command verbs

| Verb | Op | Behaviour today |
| --- | --- | --- |
| `/antiscan reload` | 3 | Hot-reloads the blocklist (`AntiScan::reload()`), reports the loaded count |
| `/antiscan stats` | 3 | Prints the blocked-IP count (`getBlockedCount()`) |
| `/antiscan block <ip>` | 3 | Adds the IP to the in-memory set and appends it to `custom_ips.txt` |
| `/antiscan unblock <ip>` | 3 | Removes it from memory |
| `/hunter …` | 3 | Alias of `/antiscan` |

## Bans, kicks and whitelist — current state

These verbs are **registered and level-gated but their backends are still TODO** (see
[Commands → Implementation status](Commands#implementation-status-as-compiled-today)). They acknowledge
the request and log it; they do **not** yet persist or disconnect:

| Verb | Missing backend |
| --- | --- |
| `/kick <player> [reason]` | no `0x40` (Disconnect) packet, socket not closed |
| `/ban <player> [reason]` | nothing written to `banned-players` storage |
| `/ban-ip <player\|IP> [reason]` | IP not added to AntiScan, socket not closed |
| `/pardon <player>` · `/pardon-ip <IP>` | ban entry not removed |
| `/whitelist <add\|remove\|on\|off> [player]` | no `whitelist.json` backend, joinability not gated |
| `/banlist [ips\|players]` | list not enumerated |

Working security actions today: **AntiScan block/unblock, the remote blocklist, and the decoy-response
drop path**. Until the ban backend lands, use `/antiscan block <ip>` to keep a scanner out and a
manual restart to clear an operator mistake.

## Checklist before publishing a server

| Item | Why |
| --- | --- |
| `enable-antiscan = true` and a reachable `blocklist-url` | Keeps scanner bots off the epoll loop |
| Review `custom_ips.txt` | Locally blocked IPs persist forever and survive restarts |
| Level 2+ grant only to trusted names | Level ≤ 0 players cannot run admin verbs, but any client can chat |
| Restart after changing `port` | The listener is opened once in `NetworkManager::start()` |
| Do not rely on `/ban` yet | It is acknowledgement-only — document it in front of players |

Related: [Configuration](Configuration), [Commands](Commands), [Op Levels](Op-Levels).
