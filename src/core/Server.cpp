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
#include <termios.h>
#include <unistd.h>
#include <vector>
#include <csignal>
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

        // Boucle maître
        while (m_running)
        {
            m_network.pollEvents(50); // 50ms = 1 tick (20 TPS)
            m_chunkManager.tickActiveChunks();
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