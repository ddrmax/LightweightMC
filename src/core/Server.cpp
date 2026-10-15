#include "core/Server.hpp"
#include "core/Logger.hpp"
#include <iostream>
#include <algorithm>
#include "core/ConfigManager.hpp"
#include "core/CommandManager.hpp"
#include "commands/BasicCommands.hpp"
#include "commands/ServerCommands.hpp"
#include "commands/SystemCommands.hpp"
#include "commands/PlayerCommands.hpp"
#include "commands/WorldCommands.hpp"
#include "commands/EntityCommands.hpp"
#include "commands/CommunicationCommands.hpp"
#include "commands/DebugCommands.hpp"
#include "commands/ScriptingCommands.hpp"
#include "commands/TestPacketsCommand.hpp"
#include "network/Packet.hpp"
#include <termios.h>
#include <unistd.h>
#include <vector>
#include <csignal>
#include <chrono>
namespace LightweightMC::Core
{

    Server::Server() : m_database("world_data.db") {}
    Server::~Server() { stop(); }

    void Server::printBanner()
    {
        std::cout << "\033[1;32m"
                  << R"(M""MMMMMMMM oo          dP         dP                       oo          dP         dP   M"""""`'"""`YM MM'""""'YMM)" << "\n"
                  << R"(M  MMMMMMMM             88         88                                   88         88   M  mm.  mm.  M M' .mmm. `M)" << "\n"
                  << R"(M  MMMMMMMM dP .d8888b. 88d888b. d8888P dP  dP  dP .d8888b. dP .d8888b. 88d888b. d8888P M  MMM  MMM  M M  MMMMMooM)" << "\n"
                  << R"(M  MMMMMMMM 88 88'  `88 88'  `88   88   88  88  88 88ooood8 88 88'  `88 88'  `88   88   M  MMM  MMM  M M  MMMMMMMM)" << "\n"
                  << R"(M  MMMMMMMM 88 88.  .88 88    88   88   88.88b.88' 88.  ... 88 88.  .88 88    88   88   M  MMM  MMM  M M. `MMM' .M)" << "\n"
                  << R"(M         M dP `8888P88 dP    dP   dP   8888P Y8P  `88888P' dP `8888P88 dP    dP   dP   M  MMM  MMM  M MM.     .dM)" << "\n"
                  << R"(MMMMMMMMMMM         .88                                             .88                 MMMMMMMMMMMMMM MMMMMMMMMMM)" << "\n"
                  << R"(                d8888P                                          d8888P                                            )" << "\n"
                  << "\033[0m"
                  << "\033[1;36m       -- Native C++ Engine | Event-Driven & Zero-Tick --\033[0m\n\n";
    }
    void Server::initServerCommands()
    {
        auto &cmdMgr = CommandManager::getInstance();

        // Command registering
        cmdMgr.registerCommand(std::make_shared<HelpCommand>());
        cmdMgr.registerCommand(std::make_shared<StopCommand>(m_running));
        cmdMgr.registerCommand(std::make_shared<ReloadCommand>());
        cmdMgr.registerCommand(std::make_shared<PingCommand>());
        cmdMgr.registerCommand(std::make_shared<HudCommand>());

        cmdMgr.registerCommand(std::make_shared<AntiScanCommand>());
        cmdMgr.registerCommand(std::make_shared<TeleportCommand>());
        cmdMgr.registerCommand(std::make_shared<VersionCommand>());
        cmdMgr.registerCommand(std::make_shared<ListCommand>());
        cmdMgr.registerCommand(std::make_shared<MsgCommand>());
        cmdMgr.registerCommand(std::make_shared<SayCommand>());

        // --- Administration and moderation commands ---
        cmdMgr.registerCommand(std::make_shared<OpCommand>());
        cmdMgr.registerCommand(std::make_shared<DeopCommand>());
        cmdMgr.registerCommand(std::make_shared<WhitelistCommand>());
        cmdMgr.registerCommand(std::make_shared<KickCommand>());
        cmdMgr.registerCommand(std::make_shared<BanCommand>());
        cmdMgr.registerCommand(std::make_shared<BanIpCommand>());
        cmdMgr.registerCommand(std::make_shared<PardonCommand>());
        cmdMgr.registerCommand(std::make_shared<PardonIpCommand>());
        cmdMgr.registerCommand(std::make_shared<SaveAllCommand>());
        // --- LightweightMC Extensions ---
        cmdMgr.registerCommand(std::make_shared<PluginsCommand>());
        cmdMgr.registerCommand(std::make_shared<LwmcCommand>());
        // --- Protocol validation (Phase 0) ---
        cmdMgr.registerCommand(std::make_shared<TestPacketsCommand>());

        // --- Administration & Config ---
        cmdMgr.registerCommand(std::make_shared<BanlistCommand>());
        cmdMgr.registerCommand(std::make_shared<DatapackCommand>());
        cmdMgr.registerCommand(std::make_shared<DefaultGamemodeCommand>());
        cmdMgr.registerCommand(std::make_shared<DifficultyCommand>());
        cmdMgr.registerCommand(std::make_shared<GameruleCommand>());
        cmdMgr.registerCommand(std::make_shared<PublishCommand>());
        cmdMgr.registerCommand(std::make_shared<SaveOffCommand>());
        cmdMgr.registerCommand(std::make_shared<SaveOnCommand>());
        cmdMgr.registerCommand(std::make_shared<SetIdleTimeoutCommand>());

        // --- Player, Inventory & Attributes ---
        cmdMgr.registerCommand(std::make_shared<AdvancementCommand>());
        cmdMgr.registerCommand(std::make_shared<AttributeCommand>());
        cmdMgr.registerCommand(std::make_shared<ClearCommand>());
        cmdMgr.registerCommand(std::make_shared<EffectCommand>());
        cmdMgr.registerCommand(std::make_shared<EnchantCommand>());
        cmdMgr.registerCommand(std::make_shared<ExperienceCommand>());
        cmdMgr.registerCommand(std::make_shared<GamemodeCommand>());
        cmdMgr.registerCommand(std::make_shared<GiveCommand>());
        cmdMgr.registerCommand(std::make_shared<ItemCommand>());
        cmdMgr.registerCommand(std::make_shared<RecipeCommand>());

        // --- World, Blocks & Chunks ---
        cmdMgr.registerCommand(std::make_shared<CloneCommand>());
        cmdMgr.registerCommand(std::make_shared<FillCommand>());
        cmdMgr.registerCommand(std::make_shared<FillBiomeCommand>());
        cmdMgr.registerCommand(std::make_shared<ForceloadCommand>());
        cmdMgr.registerCommand(std::make_shared<LocateCommand>());
        cmdMgr.registerCommand(std::make_shared<PlaceCommand>());
        cmdMgr.registerCommand(std::make_shared<SeedCommand>());
        cmdMgr.registerCommand(std::make_shared<SetBlockCommand>());
        cmdMgr.registerCommand(std::make_shared<SetWorldSpawnCommand>());
        cmdMgr.registerCommand(std::make_shared<SpawnPointCommand>());
        cmdMgr.registerCommand(std::make_shared<TimeCommand>());
        cmdMgr.registerCommand(std::make_shared<WeatherCommand>());
        cmdMgr.registerCommand(std::make_shared<WorldBorderCommand>());
        // Multiworld dimension-plane commands (see
        // src/network/NetworkWorlds.cpp for the API backing these).
        cmdMgr.registerCommand(std::make_shared<GotorCommand>());
        cmdMgr.registerCommand(std::make_shared<WorldCommand>());

        // --- Entities & Combat ---
        cmdMgr.registerCommand(std::make_shared<BossbarCommand>());
        cmdMgr.registerCommand(std::make_shared<DamageCommand>());
        cmdMgr.registerCommand(std::make_shared<KillCommand>());
        cmdMgr.registerCommand(std::make_shared<LootCommand>());
        cmdMgr.registerCommand(std::make_shared<RideCommand>());
        cmdMgr.registerCommand(std::make_shared<SpectateCommand>());
        cmdMgr.registerCommand(std::make_shared<SpreadPlayersCommand>());
        cmdMgr.registerCommand(std::make_shared<SummonCommand>());

        // --- NBT, Tags & Scoreboard ---
        cmdMgr.registerCommand(std::make_shared<DataCommand>());
        cmdMgr.registerCommand(std::make_shared<TagCommand>());
        cmdMgr.registerCommand(std::make_shared<TeamCommand>());
        cmdMgr.registerCommand(std::make_shared<ScoreboardCommand>());

        // --- Execution & Scripts ---
        cmdMgr.registerCommand(std::make_shared<ExecuteCommand>());
        cmdMgr.registerCommand(std::make_shared<FunctionCommand>());
        cmdMgr.registerCommand(std::make_shared<ReturnCommand>());
        cmdMgr.registerCommand(std::make_shared<ScheduleCommand>());
        cmdMgr.registerCommand(std::make_shared<TriggerCommand>());

        // --- Moderation, Chat & Visuals ---
        cmdMgr.registerCommand(std::make_shared<MeCommand>());
        cmdMgr.registerCommand(std::make_shared<ParticleCommand>());
        cmdMgr.registerCommand(std::make_shared<PlaysoundCommand>());
        cmdMgr.registerCommand(std::make_shared<StopsoundCommand>());
        cmdMgr.registerCommand(std::make_shared<TeammsgCommand>());
        cmdMgr.registerCommand(std::make_shared<TellrawCommand>());
        cmdMgr.registerCommand(std::make_shared<TitleCommand>());

        // --- Debug & Diagnostic ---
        cmdMgr.registerCommand(std::make_shared<DebugCommand>());
        cmdMgr.registerCommand(std::make_shared<JfrCommand>());
        cmdMgr.registerCommand(std::make_shared<PerfCommand>());
    }

    // Example of a processing loop for the system console
    void Server::processConsoleInput(const std::string &inputLine)
    {
        CommandContext consoleCtx{
            .sender = "Console",
            .isConsole = true,
            .opLevel = 4 // The console has full OP rights (level 4)
        };

        // Allow the console to op/deop players: update the live session and persist.
        consoleCtx.setOpLevel = [this](const std::string &name, int level) -> bool
        {
            uint8_t clamped = static_cast<uint8_t>(std::max(0, std::min(4, level)));
            return m_network.setPlayerOpLevel(name, clamped);
        };

        // Allow the console to change a player's game mode. The hook updates
        // the live session, persists the change to the database, and (when
        // the player is online) pushes a Tab List (0x38) update to the
        // client so survival/creative/adventure/spectator is observable
        // immediately without requiring a reconnect.
        consoleCtx.setGameMode = [this](const std::string &name, int level) -> bool
        {
            uint8_t clamped = static_cast<uint8_t>(std::max(0, std::min(3, level)));
            return m_network.setPlayerGameMode(name, clamped);
        };

        // Broadcast console replies to every PLAY client only when the
        // command actually touched player state (see Command::affectsPlayerState
        // which CommandManager::executeCommand pushes into ctx.broadcastThisReply).
        consoleCtx.broadcastCallback = [this](const std::string &msg)
        {
            std::vector<uint8_t> chatPayload;
            LightweightMC::Network::Packet::writeString(chatPayload,
                                                        R"({"text":")" + msg + R"("})");
            chatPayload.push_back(1); // chat-visible flag
            m_network.broadcastPacket(0x02, chatPayload, -1);
        };

        // Chat/console /tp: teleport an online player to absolute coords or
        // to another player's current world position. Delegates to
        // NetworkManager's teleport helpers which look up the live session,
        // mutate x/y/z, and push the S2C Position packet (1.8 id 0x08).
        consoleCtx.teleportPlayerToCoords = [this](const std::string &name, double x, double y, double z) -> bool
        {
            return m_network.teleportToCoords(name, x, y, z);
        };
        consoleCtx.teleportPlayerToTarget = [this](const std::string &mover, const std::string &target) -> bool
        {
            return m_network.teleportToPlayer(mover, target);
        };

        // Multiworld: /gotor <worldName> moves a live session to the named
        // plane. The NetworkManager implementation resolves the name via the
        // world_registry SQLite table, snaps to the target plane's surface,
        // clears loadedChunks (so the client re-streams terrain from the new
        // plane) and pushes an S2C Position packet (0x08) so the client
        // animates the jump.
        consoleCtx.switchPlayerWorld = [this](const std::string &playerName, const std::string &worldName) -> bool
        {
            const int32_t id = m_network.lookupWorldId(worldName);
            // lookupWorldId returns the -2147483648 sentinel when the plane is
            // missing; negative ids are legal (Nether is -1 pre-1.12), so test
            // the sentinel rather than "id < 0".
            if (id == -2147483648)
                return false;
            return m_network.setPlayerWorldId(playerName, id);
        };
        // /world create <name> <style>: allocate a fresh >=100 id in the
        // world_registry SQLite table and open a per-plane SQLite file.
        // 0.0.15 phase 4 widens the closure to carry --inventory /
        // --force-gamemode / --seed from the console command handler
        // through to NetworkManager::createWorld; the /world create
        // sub-branch in WorldCommands.hpp always supplies all five
        // arguments so the closure just forwards them.
        consoleCtx.createWorld = [this](const std::string &worldName, const std::string &style,
                                        int32_t inventoryPolicy, int32_t forceGameMode, int64_t seed) -> int32_t
        {
            return m_network.createWorld(worldName, style, inventoryPolicy, forceGameMode, seed);
        };
        consoleCtx.listWorlds = [this]() -> std::vector<std::string>
        {
            return m_network.listWorldNames();
        };
        consoleCtx.describeWorld = [this](const std::string &worldName, int32_t &outId, std::string &outStyle, std::string &outDbPath) -> bool
        {
            return m_network.describeWorld(worldName, outId, outStyle, outDbPath);
        };
        // 0.0.16 world-control hooks: /setworldspawn, /seed, /setblock,
        // /place and /spawnpoint. Console-thread invocations default to the
        // Overworld (senderWorldId = 0) for /setblock and /place since the
        // console thread is not tied to a live session.
        consoleCtx.setWorldSpawn = [this](const std::string &worldName, double x, double y, double z) -> bool
        {
            return m_network.setWorldSpawnByName(worldName, x, y, z);
        };
        consoleCtx.getWorldSpawn = [this](const std::string &worldName, double &x, double &y, double &z) -> bool
        {
            return m_network.getWorldSpawnByName(worldName, x, y, z);
        };
        consoleCtx.getWorldSeed = [this](const std::string &worldName) -> int64_t
        {
            return m_network.getWorldSeedByName(worldName);
        };
        consoleCtx.setWorldSeed = [this](const std::string &worldName, int64_t seed) -> bool
        {
            return m_network.setWorldSeedByName(worldName, seed);
        };
        consoleCtx.setBlockAtPlane = [this](int32_t planeId, int wx, int wy, int wz, uint16_t combined) -> bool
        {
            return m_network.setBlockAtPlane(planeId, wx, wy, wz, combined);
        };
        consoleCtx.setPlayerSpawn = [this](const std::string &name, double x, double y, double z) -> bool
        {
            return m_network.updatePlayerSpawnByName(name, x, y, z);
        };

        CommandManager::getInstance().executeCommand(consoleCtx, inputLine);
    }

    // Global reference to store original termios for signal handler cleanup
    static struct termios g_orig_termios;
    static bool g_termios_saved = false;

    static void restoreTerminalOnSignal(int sig)
    {
        if (g_termios_saved)
        {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
        }
        // Re-raise or exit cleanly depending on signal
        if (sig == SIGSEGV || sig == SIGABRT)
        {
            std::signal(sig, SIG_DFL);
            std::raise(sig);
        }
        else
        {
            std::_Exit(128 + sig);
        }
    }

    // RAII guard for thread execution safety
    struct TerminalModeGuard
    {
        struct termios orig;
        TerminalModeGuard()
        {
            tcgetattr(STDIN_FILENO, &orig);
            g_orig_termios = orig;
            g_termios_saved = true;

            // Catch signals to prevent terminal corruption on crash
            std::signal(SIGINT, restoreTerminalOnSignal);
            std::signal(SIGTERM, restoreTerminalOnSignal);
            std::signal(SIGSEGV, restoreTerminalOnSignal);
            std::signal(SIGABRT, restoreTerminalOnSignal);
        }

        ~TerminalModeGuard()
        {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
        }
    };

    void Server::startConsoleThread(const std::atomic<bool> &running)
    {
        // 1. Structure for passing arguments to the POSIX thread
        struct ThreadArgs
        {
            const std::atomic<bool> &running;
            Network::NetworkManager *network;
        };

        auto *args = new ThreadArgs{running, &m_network};

        // 2. C++ lambda convertible to a C function pointer
        auto threadRoutine = [](void *arg) -> void *
        {
            auto *threadArgs = static_cast<ThreadArgs *>(arg);
            const auto &isRunning = threadArgs->running;
            Network::NetworkManager *network = threadArgs->network;

            CommandContext consoleCtx{
                .sender = "Console",
                .isConsole = true,
                .opLevel = 4};

            consoleCtx.setOpLevel = [network](const std::string &name, int level) -> bool
            {
                uint8_t clamped = static_cast<uint8_t>(std::max(0, std::min(4, level)));
                return network->setPlayerOpLevel(name, clamped);
            };

            // Mirror the wiring done in processConsoleInput so the
            // /gamemode command issued from the actual console thread
            // changes the live session and persists to SQLite.
            consoleCtx.setGameMode = [network](const std::string &name, int level) -> bool
            {
                uint8_t clamped = static_cast<uint8_t>(std::max(0, std::min(3, level)));
                return network->setPlayerGameMode(name, clamped);
            };

            // Multiworld: /gotor <worldName> and /world create <name> <style>.
            consoleCtx.switchPlayerWorld = [network](const std::string &playerName, const std::string &worldName) -> bool
            {
                const int32_t id = network->lookupWorldId(worldName);
                // Sentinel check, not "id < 0": Nether's id is legitimately -1.
                if (id == -2147483648)
                    return false;
                return network->setPlayerWorldId(playerName, id);
            };
            consoleCtx.createWorld = [network](const std::string &worldName, const std::string &style,
                                               int32_t inventoryPolicy, int32_t forceGameMode, int64_t seed) -> int32_t
            {
                return network->createWorld(worldName, style, inventoryPolicy, forceGameMode, seed);
            };
            consoleCtx.listWorlds = [network]() -> std::vector<std::string>
            {
                return network->listWorldNames();
            };
            consoleCtx.describeWorld = [network](const std::string &worldName, int32_t &outId, std::string &outStyle, std::string &outDbPath) -> bool
            {
                return network->describeWorld(worldName, outId, outStyle, outDbPath);
            };

            // Broadcast console replies to every PLAY client only when the
            // command mutated player state (Command::affectsPlayerState ->
            // CommandManager::executeCommand writes it into ctx.broadcastThisReply).
            consoleCtx.broadcastCallback = [network](const std::string &msg)
            {
                std::vector<uint8_t> chatPayload;
                LightweightMC::Network::Packet::writeString(chatPayload,
                                                            R"({"text":")" + msg + R"("})");
                chatPayload.push_back(1); // chat-visible flag
                network->broadcastPacket(0x02, chatPayload, -1);
            };

            // Chat/console /tp: teleport an online player to absolute coords
            // or to another player's current world position. Delegates to
            // NetworkManager's teleport helpers which look up the live
            // session, mutate x/y/z, and push the S2C Position packet
            // (1.8 id 0x08).
            consoleCtx.teleportPlayerToCoords = [network](const std::string &name, double x, double y, double z) -> bool
            {
                return network->teleportToCoords(name, x, y, z);
            };
            consoleCtx.teleportPlayerToTarget = [network](const std::string &mover, const std::string &target) -> bool
            {
                return network->teleportToPlayer(mover, target);
            };

            // 0.0.16 world-control hooks: /setworldspawn, /seed, /setblock,
            // /place and /spawnpoint. The threaded console's ctx is not tied
            // to a live session either, so /setblock + /place default to the
            // Overworld (senderWorldId = 0).
            consoleCtx.setWorldSpawn = [network](const std::string &worldName, double x, double y, double z) -> bool
            {
                return network->setWorldSpawnByName(worldName, x, y, z);
            };
            consoleCtx.getWorldSpawn = [network](const std::string &worldName, double &x, double &y, double &z) -> bool
            {
                return network->getWorldSpawnByName(worldName, x, y, z);
            };
            consoleCtx.getWorldSeed = [network](const std::string &worldName) -> int64_t
            {
                return network->getWorldSeedByName(worldName);
            };
            consoleCtx.setWorldSeed = [network](const std::string &worldName, int64_t seed) -> bool
            {
                return network->setWorldSeedByName(worldName, seed);
            };
            consoleCtx.setBlockAtPlane = [network](int32_t planeId, int wx, int wy, int wz, uint16_t combined) -> bool
            {
                return network->setBlockAtPlane(planeId, wx, wy, wz, combined);
            };
            consoleCtx.setPlayerSpawn = [network](const std::string &name, double x, double y, double z) -> bool
            {
                return network->updatePlayerSpawnByName(name, x, y, z);
            };

            std::vector<std::string> history;

            // RAII Guard guarantees tcsetattr restoration on thread exit or exception
            TerminalModeGuard termGuard;

            struct termios raw = termGuard.orig;
            raw.c_lflag &= ~(ICANON | ECHO);

            while (isRunning)
            {
                tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

                std::string currentLine;
                int historyIndex = static_cast<int>(history.size());
                char c;

                while (isRunning && read(STDIN_FILENO, &c, 1) == 1)
                {
                    if (c == '\n' || c == '\r')
                    {
                        std::cout << "\r\n";
                        break;
                    }
                    else if (c == '\033')
                    {
                        char seq[2];
                        if (read(STDIN_FILENO, &seq[0], 1) == 1 && read(STDIN_FILENO, &seq[1], 1) == 1)
                        {
                            if (seq[0] == '[')
                            {
                                if (seq[1] == 'A') // Up arrow
                                {
                                    if (!history.empty() && historyIndex > 0)
                                    {
                                        historyIndex--;
                                        std::cout << "\r\033[K" << history[historyIndex] << std::flush;
                                        currentLine = history[historyIndex];
                                    }
                                }
                                else if (seq[1] == 'B') // Down arrow
                                {
                                    if (historyIndex < static_cast<int>(history.size()))
                                    {
                                        historyIndex++;
                                        std::cout << "\r\033[K";
                                        if (historyIndex < static_cast<int>(history.size()))
                                        {
                                            currentLine = history[historyIndex];
                                            std::cout << currentLine;
                                        }
                                        else
                                        {
                                            currentLine = "";
                                        }
                                        std::cout << std::flush;
                                    }
                                }
                            }
                        }
                    }
                    else if (c == 127 || c == 8) // Backspace
                    {
                        if (!currentLine.empty())
                        {
                            currentLine.pop_back();
                            std::cout << "\b \b" << std::flush;
                        }
                    }
                    else if (c >= 32 && c <= 126) // Printable characters
                    {
                        currentLine += c;
                        std::cout << c << std::flush;
                    }
                }

                // Restore canonical mode for standard logging output
                tcsetattr(STDIN_FILENO, TCSAFLUSH, &termGuard.orig);

                if (!currentLine.empty())
                {
                    history.push_back(currentLine);
                    CommandManager::getInstance().executeCommand(consoleCtx, currentLine);
                }
            }

            delete threadArgs;
            return nullptr;
        };

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 256 * 1024);

        pthread_t threadId;
        if (pthread_create(&threadId, &attr, threadRoutine, args) == 0)
        {
            pthread_detach(threadId);
        }
        else
        {
            delete args;
        }

        pthread_attr_destroy(&attr);
    }
    void Server::start()
    {
        printBanner();
        initServerCommands();

        IniParser config;
        ConfigManager::getInstance().load("server.properties");
        uint16_t port = ConfigManager::getInstance().get<uint16_t>("server", "port", 25565);
        int maxPlayers = ConfigManager::getInstance().get<int>("server", "max-players", 50);
        std::string motd = ConfigManager::getInstance().getString("server", "motd", "A C++ Minecraft Server");

        bool enableAntiScan = ConfigManager::getInstance().getBool("security", "enable-antiscan", true);

        // Displaying loaded settings
        Logger::info("[CONFIG] Server Port: " + std::to_string(port));
        Logger::info("[CONFIG] Max Players: " + std::to_string(maxPlayers));
        Logger::info("[CONFIG] MOTD: " + motd);
        Logger::info(std::string("[CONFIG] AntiScan Enabled: ") + (enableAntiScan ? "Yes" : "No"));
        m_running = true;
        Logger::info("SQLite Database Init...");
        m_database.init();

        Logger::info("Starting Network Manager (epoll)...");
        m_network.start(port);
        startConsoleThread(m_running);
        Logger::info("Server is ready and listening. Type /help for available commands. ");

        // Boucle maître — fixed-accumulator 20 TPS tick loop.
        // Previously pollEvents(50) alone was treated as "1 tick = 50 ms",
        // but epoll_wait returns as soon as any socket event fires, which
        // means the loop iterates far more often than 20/s on a busy server
        // and tickActiveChunks() re-runs hundreds of times per second. Use
        // an explicit accumulator so the world tick stays fixed at 20 Hz
        // regardless of network activity. pollEvents uses a smaller timeout
        // (10 ms) so we do not introduce up-to-50 ms input latency.
        using namespace std::chrono_literals;
        auto lastTick = std::chrono::steady_clock::now();
        constexpr auto tickInterval = 50ms; // 20 TPS
        while (m_running)
        {
            m_network.pollEvents(10); // small slice => input stays responsive

            auto now = std::chrono::steady_clock::now();
            while (now - lastTick >= tickInterval)
            {
                if (!m_running)
                    break;
                m_chunkManager.tickActiveChunks();
                // Periodic 1.8+ chunk-stream flush. Fires at 5 Hz (throttle
                // by running every 4th 50ms tick) so the async WorldStorage
                // generation worker's outputs reach idle or slow-moving
                // players who stopped moving before their last crossing's
                // prefetch queue drained. Moving players already get their
                // chunks via updateChunksAroundPlayer at each crossing; this
                // pass only handles the "stopped moving" tail and lets the
                // 2ms inter-packet sleep in sendMapChunkBulk pace wire
                // delivery to whatever the client is currently ready to
                // receive.
                static int chunkStreamThrottle = 0;
                if ((++chunkStreamThrottle & 0x3) == 0)
                    m_network.tickChunkStreaming(5);
                lastTick += tickInterval;

                // If we are more than 5 ticks behind (server stalled), snap
                // the accumulator to now so we do not run a catch-up burst.
                if (std::chrono::steady_clock::now() - lastTick > 5 * tickInterval)
                    lastTick = std::chrono::steady_clock::now();
            }
        }
    }

    void Server::stop()
    {
        if (m_running)
        {
            m_running = false;
            m_network.stop();
            Logger::info("Server shutdown with success.");
        }
    }

} // namespace LightweightMC::Core