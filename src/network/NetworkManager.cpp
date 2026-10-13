// -----------------------------------------------------------------------------
// NetworkManager implementation is split by responsibility across several
// translation units. Each file owns a distinct concern and the class
// interface in include/network/NetworkManager.hpp is unchanged so
// existing callers keep working. See the sibling .cpps:
//
//   NetworkSocket.cpp   -- socket bootstrap (start, stop, listen socket,
//                          wire the /testpackets command to live sessions).
//   NetworkEvents.cpp   -- the epoll lifecycle (pollEvents, handleClientData,
//                          handleDisconnect, flushSendBuffer, setNonBlocking).
//   NetworkFraming.cpp  -- wire-level packet framing and era-aware ID mapping
//                          (sendPacket, broadcastPacket).
//   NetworkPlayerMeta.cpp -- player-state setters (setPlayerOpLevel,
//                             setPlayerGameMode).
//
// Networking / player-state thread-safety invariant:
// All mutation of NetworkManager::m_clients happens on the main epoll thread
// (pollEvents -> handleClientData / handleDisconnect). The console thread only
// touches CommandManager and reads via the injected lambdas -- it never
// inserts or erases from m_clients. The previous mutex was applied only to
// handleDisconnect and was therefore a no-op; drop it and document the
// invariant instead so we do not pay a lock on every send/broadcast.
//
// This file intentionally contains no code. Keeping it here (rather than
// deleting it) preserves a grep-able landing point for anyone looking for
// "NetworkManager.cpp" in the tree.
// -----------------------------------------------------------------------------
