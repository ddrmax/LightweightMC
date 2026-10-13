#pragma once

#include "core/Logger.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <sstream>
#include <functional>

struct CommandContext
{
    std::string sender;
    bool isConsole{false};
    int opLevel{0};

    // Set by CommandManager::executeCommand before invoking cmd->execute()
    // so reply() can broadcast to the whole server only for commands that
    // actually change a player's state (op, kick, ban, gamemode, ...).
    // Mutable so executeCommand can set it through a const reference.
    mutable bool broadcastThisReply{false};

    void reply(const std::string &message) const
    {
        if (isConsole)
        {
            LightweightMC::Core::Logger::info(std::string("[Command] ") + message);
            if (broadcastThisReply && broadcastCallback)
            {
                // The console command changed state for one or more
                // players -- deliver the same message to every connected
                // client so the players on the wire see what happened.
                broadcastCallback(message);
            }
        }
        else if (replyCallback)
        {
            // Chat path: send a chat packet back to the client who issued
            // the command.
            replyCallback(message);
        }
    }

    // Optional callback used by /op and /deop to change a player's operator
    // status. It must update the live session (if the player is online) and
    // persist the new level to the database. Returns true on success.
    // Defaults to a no-op so commands can be exercised from the console.
    std::function<bool(const std::string &playerName, int opLevel)> setOpLevel = [](const std::string &, int) -> bool
    {
        return false;
    };

    // Optional callback used by /gamemode to change a player's game mode.
    // It must update the live session (if the player is online), persist the
    // value to the database, and forward the update to the client (typically
    // via a Tab List "Update Player" packet). `mode` uses Minecraft's
    // enumeration: 0=survival, 1=creative, 2=adventure, 3=spectator.
    // Defaults to a no-op so commands can be exercised from a bare console.
    std::function<bool(const std::string &playerName, int mode)> setGameMode = [](const std::string &, int) -> bool
    {
        return false;
    };

    // Optional callback used by reply() to deliver chat messages to the
    // player who issued the command. Defaults to a no-op so commands can be
    // exercised from the console (or logged only).
    std::function<void(const std::string &)> replyCallback = [](const std::string &) {};

    // Optional callback used by reply() when a console command mutated a
    // player (or players) on the wire. Server-side implementations wire
    // this to NetworkManager::broadcastPacket(CHAT, ...) so players see
    // what the console did. Defaults to a no-op so bare console runs
    // still work.
    std::function<void(const std::string &)> broadcastCallback = [](const std::string &) {};

    // Optional hook used by /hud (aka /stats) to push the Scoreboard +
    // TablistHeaderFooter packets to the caller's client. Chat contexts
    // wire this to a ScoreboardManager::sendScoreboard(clientFd, clients,
    // packetSender) closure; console contexts can wire a broadcast
    // variant. Defaults to a no-op.
    std::function<void()> sendHud = []() {};

    // Optional hook used by /tp to teleport an online player to absolute
    // world coordinates. Server-side implementations wire this to
    // NetworkManager::teleportToCoords(name, x, y, z) so the packet
    // reaches the target's client. Defaults to a no-op that reports
    // failure so a bare console run can still parse the command line.
    std::function<bool(const std::string &playerName, double x, double y, double z)> teleportPlayerToCoords =
        [](const std::string &, double, double, double) -> bool { return false; };

    // Optional hook used by /tp to teleport one player to the current
    // world position of another player. Server-side implementations
    // wire this to NetworkManager::teleportToPlayer(mover, target).
    // Defaults to a no-op so a bare console run can still parse the
    // command line without a live session table.
    std::function<bool(const std::string &moverName, const std::string &targetName)> teleportPlayerToTarget =
        [](const std::string &, const std::string &) -> bool { return false; };

    // Optional hook used by /gotor <worldName> to move a live session to a
    // different dimension plane. Multiworld fix: replaces the historical
    // single-plane assumption with a proper session-aware world switch.
    // Server-side implementations wire this to NetworkManager::setPlayerWorldId
    // (which resolves the world name via the world_registry table, snaps the
    // player to the surface of the new plane, and clears loadedChunks so the
    // new plane's chunks stream to the client fresh).
    std::function<bool(const std::string &playerName, const std::string &worldName)> switchPlayerWorld =
        [](const std::string &, const std::string &) -> bool { return false; };

    // Optional hook used by /world create <name> <style>. Server-side
    // implementations wire this to NetworkManager::createWorld, which
    // allocates the next >=101 id in the world_registry SQLite table (0/-1/
    // 1 are fixed to Overworld/Nether/End; 100 is the reserved Aether
    // special-dimension slot), opens a per-world SQLite file (world-<name>.db),
    // and inserts a fresh WorldStorage into NetworkManager::m_worlds
    // (an unordered_map keyed by worldId so negative pre-1.12 IDs like
    // Nether's -1 do not index out of bounds). Returns the assigned
    // worldId or -1 on failure.
    std::function<int32_t(const std::string &worldName, const std::string &style)> createWorld =
        [](const std::string &, const std::string &) -> int32_t { return -1; };

    // Optional hook used by /world list / /gotor to enumerate registered
    // planes. Server-side implementations typically return "overworld",
    // "nether", "end" plus any registered custom names; the console thread
    // does not enumerate SQLite rows directly so this closure stays
    // default-empty for bare console runs.
    std::function<std::vector<std::string>()> listWorlds =
        []() -> std::vector<std::string> { return {}; };

    // Optional hook used by /world info <name>. Server-side implementations
    // wire this to NetworkManager::describeWorld, which resolves the name
    // through the world_registry SQLite table and fills the out parameters
    // with the plane's worldId (Overworld=0, Nether=-1, End=1, Aether=100,
    // custom>=101), its style name and the per-plane SQLite db path it
    // serves. Returns false when the name is not registered.
    std::function<bool(const std::string &worldName, int32_t &outId, std::string &outStyle, std::string &outDbPath)> describeWorld =
        [](const std::string &, int32_t &, std::string &, std::string &) -> bool { return false; };
};

class Command
{
public:
    virtual ~Command() = default;
    virtual std::string getName() const = 0;
    virtual std::string getDescription() const = 0;
    virtual std::string getUsage() const = 0;
    virtual std::vector<std::string> getAliases() const { return {}; } // List of aliases
    virtual int getRequiredOpLevel() const { return 0; }

    // Declarative metadata: does running this command change the state of
    // one or more connected players? Console contexts use this to decide
    // whether reply() should also broadcast the outcome to every PLAY
    // client. Chat contexts ignore it (the chat path already sends to the
    // caller's client via replyCallback).
    virtual bool affectsPlayerState() const { return false; }

    virtual void execute(const CommandContext &ctx, const std::vector<std::string> &args) = 0;
};

class CommandManager
{
public:
    static CommandManager &getInstance()
    {
        static CommandManager instance;
        return instance;
    }

    CommandManager(const CommandManager &) = delete;
    CommandManager &operator=(const CommandManager &) = delete;

    void registerCommand(std::shared_ptr<Command> cmd);
    bool executeCommand(const CommandContext &ctx, const std::string &commandLine);

    // Returns the map of main commands (excluding aliases) for /help.
    const std::unordered_map<std::string, std::shared_ptr<Command>> &getCommands() const
    {
        return m_primaryCommands;
    }

private:
    CommandManager() = default;

    // m_commands contains the primary name AND all aliases pointing to the same std::shared_ptrs.
    std::unordered_map<std::string, std::shared_ptr<Command>> m_commands;
    // Filtered map containing only the primary name (for help display)
    std::unordered_map<std::string, std::shared_ptr<Command>> m_primaryCommands;
};