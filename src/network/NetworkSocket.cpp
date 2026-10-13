// Socket bootstrap: creates the listening socket, initialises the singletons
// that NetworkManager owns, and shuts the server down on stop(). Everything
// here runs once at startup (start) or shutdown (stop); the epoll loop itself
// lives in NetworkEvents.cpp, packet framing in NetworkFraming.cpp, and
// player-state setters in NetworkPlayerMeta.cpp.
//
// See src/network/NetworkManager.cpp for the class-level invariant notes.

#include "network/NetworkManager.hpp"

#include "core/ConfigManager.hpp"
#include "core/utils.hpp"
#include "core/Logger.hpp"
#include "network/AntiScan.hpp"
#include "commands/TestPacketsCommand.hpp"
#include "network/protocol/ProtocolVersion.hpp"

#include "storage/WorldStorage.hpp"
#include "storage/Database.hpp"
#include "managers/WorldManager.hpp"
#include "world/GameRules.hpp"
#include "world/EntityManager.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <cstring>
#include <csignal>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <optional>

namespace LightweightMC::Network
{

    bool NetworkManager::start(uint16_t port)
    {
        antiscan = ConfigManager::getInstance().getBool("security", "enable-antiscan", true);

        const std::string url = ConfigManager::getInstance().getString("security", "blocklist-url");
        AntiScan::init(url);

        // Clients routinely write to a socket we already hung up on; without
        // this SIGPIPE kills the whole server. send() uses MSG_NOSIGNAL too,
        // but the belt-and-braces signal mask keeps third-party code paths
        // safe as well.
        std::signal(SIGPIPE, SIG_IGN);

        // -----------------------------------------------------------------
        // Multiworld init (Fix 3 -- pre-1.12 Minecraft dimension IDs).
        //
        // Standard Minecraft dimension-plane IDs follow the pre-1.12 vanilla
        // layout (Overworld / Nether / End), which are NOT sequential and
        // include a negative value:
        //    0   = Overworld   (world.db)          -- vanilla default plane
        //   -1   = Nether      (world-nether.db)   -- underworld
        //    1   = End         (world-end.db)      -- deepest plane
        // Aether (id 100, world-aether.db) is registered as a Minecraft
        // Legends-style special dimension: it shares the SQLite world_registry
        // (so /gotor aether still resolves by name) but lives in the custom-
        // allocator range because it is not one of the vanilla 0/-1/1 planes.
        //
        // Each active plane owns its own SQLite file so a chunk-edit in Nether
        // cannot leak into the Overworld and vice versa. Config flags ([world]
        // overworld-active / nether-active / end-active / aether-active) gate
        // whether the server allocates a WorldStorage instance for that plane
        // at startup; defaults are all true so a fresh server boots with
        // every vanilla dimension live.
        //
        // m_worlds is keyed by worldId (unordered_map<int32_t, unique_ptr>),
        // NOT indexed by it -- pre-1.12 IDs include -1 so static_casting a
        // negative worldId to std::size_t before a vector subscript is UB and
        // previously crashed the server on any Nether transition.
        // -----------------------------------------------------------------
        if (!m_database.init())
        {
            Core::Logger::error("Failed to initialize SQLite player database.");
            return false;
        }

        // Load the gamerules table from server_data.db into the singleton so
        // /gamerule and the world generator can query it later in the tick loop.
        World::GameRules::getInstance().init(&m_database);

        if (!m_database.initWorldRegistry())
        {
            Core::Logger::error("Failed to initialize multiworld registry.");
            return false;
        }

        // Register the standard planes and the Aether special dimension as
        // fixed IDs 0 / -1 / 1 / 100 bypassing the >=101 custom allocator
        // path. setWorldById is idempotent so a restart with different
        // active flags keeps the registry rows stable.
        struct StdPlane
        {
            int32_t id;
            const char *name;
            const char *style;
        };
        const StdPlane kStandardPlanes[4] = {
            {0, "overworld", "overworld"},
            {-1, "nether", "nether"},
            {1, "end", "end"},
            {100, "aether", "aether"},
        };
        for (const auto &plane : kStandardPlanes)
        {
            if (!m_database.setWorldById(plane.id, plane.name, plane.style))
            {
                Core::Logger::error("Failed to register standard world row in world_registry");
                return false;
            }
        }

        // m_worlds is an unordered_map, so insertion into an existing key
        // does not invalidate references held elsewhere; no reserve() is
        // required for correctness (bucket growth only touches the map's
        // own control structure, never the pointed-to WorldStorage).

        // Read config once: gen-style + 64-bit seed drive the Overworld
        // preset; Nether / End / Aether use canonical NETHER / END / AETHER
        // styles regardless of the server-wide gen preset.
        const std::string genStyle = ConfigManager::getInstance().getString("world", "gen-style", "overworld");
        const std::uint64_t worldSeed = ConfigManager::getInstance().get<std::uint64_t>("world", "world-seed", 0ULL);

        Storage::GenStyle overworldStyle = Storage::GenStyle::OVERWORLD;
        if (genStyle == "superflat")
            overworldStyle = Storage::GenStyle::SUPERFLAT;
        else if (genStyle == "test-grid")
            overworldStyle = Storage::GenStyle::TEST_GRID;
        else if (genStyle == "aether")
            overworldStyle = Storage::GenStyle::AETHER;
        else if (genStyle == "nether")
            overworldStyle = Storage::GenStyle::NETHER;
        else if (genStyle == "end")
            overworldStyle = Storage::GenStyle::END;
        else if (genStyle != "overworld")
            Core::Logger::warn("[NetworkManager] Unknown gen-style, falling back to overworld");

        const bool overworldActive = ConfigManager::getInstance().getBool("world", "overworld-active", true);
        const bool netherActive = ConfigManager::getInstance().getBool("world", "nether-active", true);
        const bool endActive = ConfigManager::getInstance().getBool("world", "end-active", true);
        const bool aetherActive = ConfigManager::getInstance().getBool("world", "aether-active", true);

        if (!overworldActive)
        {
            Core::Logger::error("Overworld (id=0) is disabled; refusing to start because every accessor "
                                "falls back to it when worldId is unknown or inactive.");
            return false;
        }

        struct PlaneSpec
        {
            int32_t id;
            const char *dbPath;
            Storage::GenStyle style;
            bool active;
        };
        const PlaneSpec planeSpecs[4] = {
            {0, "world.db", overworldStyle, overworldActive},
            {-1, "world-nether.db", Storage::GenStyle::NETHER, netherActive},
            {1, "world-end.db", Storage::GenStyle::END, endActive},
            {100, "world-aether.db", Storage::GenStyle::AETHER, aetherActive},
        };

        for (const auto &spec : planeSpecs)
        {
            if (!spec.active)
            {
                Core::Logger::info("Skipping inactive plane id=" + std::to_string(spec.id) +
                                   " (registry row is preserved; re-enable via [world] <name>-active=true)");
                continue;
            }
            auto storage = std::make_unique<Storage::WorldStorage>();
            if (!storage->init(spec.dbPath))
            {
                Core::Logger::error(std::string("Failed to initialize SQLite world storage ") + spec.dbPath);
                return false;
            }
            storage->setWorldSeed(worldSeed);
            storage->setGenerationStyle(spec.style);
            m_worlds.emplace(spec.id, std::move(storage));
        }

        // Auto-generate every active plane on first launch (or on
        // reactivation after a config flip) so the first /gotor transition
        // serves cached terrain rather than streaming cold chunks.
        /* for (auto &kv : m_worlds)
             if (kv.second)
                 kv.second->pregenerateWorld(20);*/

        Core::Logger::info("Multiworld ready: pre-1.12 dimension IDs "
                           "(Overworld #0 / Nether #-1 / End #1 / Aether #100 special); "
                           "custom worlds via /world create allocate IDs 101+");

        m_serverFd = socket(AF_INET, SOCK_STREAM, 0);
        int opt = 1;
        setsockopt(m_serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(m_serverFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            Core::Logger::error("Failed to bind socket to port.");
            return false;
        }

        listen(m_serverFd, SOMAXCONN);
        setNonBlocking(m_serverFd);

        m_epollFd = epoll_create1(0);
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = m_serverFd;
        epoll_ctl(m_epollFd, EPOLL_CTL_ADD, m_serverFd, &ev);

        // Wire /testpackets to the live sessions so the command can resolve
        // the issuing player's negotiated protocol and build era-correct
        // test packets. Captures `this` so the lookup goes through the
        // singleton (do not copy NetworkManager).
        TestPacketsCommand::setSessionProvider(
            [this](const std::string &name) -> std::optional<Player::PlayerSession>
            {
                for (auto &kv : m_clients)
                {
                    if (kv.second.username == name)
                        return kv.second;
                }
                return std::nullopt;
            });

        return true;
    }

    void NetworkManager::stop()
    {
        if (m_serverFd != -1)
            ::close(m_serverFd);
        if (m_epollFd != -1)
            ::close(m_epollFd);
        // Client sockets stay open: shutdown runs on the console thread, and
        // iterating m_clients from there would violate the invariant that
        // m_clients is only mutated on the epoll thread. The OS reaps them
        // on process exit.
    }

} // namespace LightweightMC::Network
