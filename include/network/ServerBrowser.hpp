#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Vanilla-1.8 master-server / server-list ping handler. The vanilla
// client's AddServer / Bluetymaps / community listings work by connecting
// to the game port and sending an HTTP-style GET
//
//     GET / HTTP/1.1\r\nHost: <ip>:<port>\r\nUser-Agent: Yahoo!\r\n\r\n
//
// and expecting a *status datagram* back on the same socket (NOT wrapped
// in Minecraft varint framing). The datagram layout is:
//
//   0xFE 0xFD 0xFF                    magic "server list ping" marker
//   short protocolVersion             47 for MC 1.8 (era-derived)
//   byte  'j'                         always 0x6A
//   int 1                             constant
//   int 0                             constant
//   string serverName \x00 serverMotd  name/motd pair
//   string version                     engine build label
//   short playerCount                 currently PLAY-state players
//   short maxPlayers                   [server] max-players
//   byte 0                            always 0
//
// ServerBrowser::handle(fd, net) writes that datagram through `net`'s
// socket directly (bypassing sendPacket's varint framing because the
// browser reply is not a Minecraft packet) and closes the socket. handle()
// returns true when it recognised the request as a browser ping so the
// caller skips the normal handshake state machine on that fd.
//
// ServerBrowser::maybeActivePing(port, net) sends an outbound ping on
// boot to each configured master-server-urls entry when [auth]
// master-server-ping = true (default off; the vanilla server pings
// itself on config change but here we treat the ping as opt-in to keep
// the server's traffic profile static).
#include "network/NetworkManager.hpp"
#include "core/ConfigManager.hpp"
#include "core/Logger.hpp"

namespace LightweightMC::Network
{
    namespace ServerBrowser
    {
        // Bytes the caller has already buffered (from the initial accept
        // read) so handle() can look at the "GET " prefix without a second
        // read syscall. `net` provides the live PLAY-state player count
        // for the status datagram.
        bool handleFromBuffer(const uint8_t *bytes, std::size_t n, int clientFd,
                              LightweightMC::Network::NetworkManager &net);

    } // namespace ServerBrowser
} // namespace LightweightMC::Network
