#pragma once

#include "core/CommandManager.hpp"
#include "core/Logger.hpp"

#include <string>
#include <memory>

class Player; // Forward declaration

class ChatManager
{
public:
    static ChatManager &getInstance()
    {
        static ChatManager instance;
        return instance;
    }

    ChatManager(const ChatManager &) = delete;
    ChatManager &operator=(const ChatManager &) = delete;

    // Broadcast a raw text or JSON message to all players.
    void broadcastMessage(const std::string &message, bool isSystem = false);

    // Private message from the console or a player to another player
    void sendPrivateMessage(const std::string &sender, const std::string &target, const std::string &message);

    // Processing a chat packet received from a client
    void handleIncomingChat(std::shared_ptr<Player> player, const std::string &rawMessage);

private:
    ChatManager() = default;
};