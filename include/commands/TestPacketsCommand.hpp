#pragma once

#include "core/CommandManager.hpp"
#include "core/Logger.hpp"
#include "network/Packet.hpp"
#include "network/protocol/BaseProtocol.hpp"
#include "network/protocol/PacketKind.hpp"
#include "player/PlayerSession.hpp"

#include <cstdio>
#include <optional>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// /testpackets — Phase 0 protocol validation command.
//
// Builds the canonical S2C packet set (Join Game, Chat, Keep Alive, Spawn
// Position, Block Change, Add Player) through the session's BaseProtocol and
// logs each one with its resolved wire ID. This lets an operator verify, for a
// given protocol version, that framing/ID mapping produces the expected bytes
// before enabling compression.
// ---------------------------------------------------------------------------
class TestPacketsCommand : public Command
{
public:
    std::string getName() const override { return "testpackets"; }
    std::string getDescription() const override { return "Builds and logs the S2C test-packet set for protocol validation."; }
    std::string getUsage() const override { return "/testpackets [player]"; }
    std::vector<std::string> getAliases() const override { return {"tpk", "packettest"}; }
    int getRequiredOpLevel() const override { return 3; }

    void execute(const CommandContext &ctx, const std::vector<std::string> &args) override
    {
        if (args.size() > 1)
        {
            ctx.reply("Usage: " + getUsage());
            return;
        }

        // Resolve the issuing player's live session to learn which protocol to
        // build packets for. Console invocations fall back to default 1.8 (47).
        LightweightMC::Player::PlayerSession session;
        if (!ctx.isConsole && m_sessionProvider)
        {
            auto it = m_sessionProvider(ctx.sender);
            if (it.has_value())
                session = *it;
        }
        if (session.protocol == nullptr)
            session.protocol = LightweightMC::Network::Protocol::BaseProtocol::create(session.clientProtocol);

        const auto &proto = *session.protocol;
        ctx.reply("TestPackets: building S2C packet set for protocol " + std::to_string(session.clientProtocol) +
                  " (" + proto.versionName() + ")");

        int sent = 0;
        logPacket(proto, LightweightMC::Network::Protocol::PacketKind::JOIN_GAME, buildJoinGame(), ++sent);
        logPacket(proto, LightweightMC::Network::Protocol::PacketKind::CHAT, buildChat("TestPackets: S2C chat ok"), ++sent);
        logPacket(proto, LightweightMC::Network::Protocol::PacketKind::KEEP_ALIVE, buildKeepAlive(1), ++sent);
        logPacket(proto, LightweightMC::Network::Protocol::PacketKind::SPAWN_POSITION, buildSpawnPosition(session), ++sent);
        logPacket(proto, LightweightMC::Network::Protocol::PacketKind::BLOCK_CHANGE, buildBlockChange(), ++sent);
        logPacket(proto, LightweightMC::Network::Protocol::PacketKind::ADD_PLAYER, buildAddPlayer(session), ++sent);

        ctx.reply("TestPackets: " + std::to_string(sent) + " packet(s) built and logged. Check server console for wire IDs.");
    }

    // Optional provider used to resolve the issuing player's live session. Set by
    // NetworkManager at startup; defaults to a no-op (console-only operation).
    using SessionProvider = std::function<std::optional<LightweightMC::Player::PlayerSession>(const std::string &name)>;
    static void setSessionProvider(SessionProvider provider) { m_sessionProvider = provider; }

private:
    static inline SessionProvider m_sessionProvider = [](const std::string &) -> std::optional<LightweightMC::Player::PlayerSession> { return std::nullopt; };

    // --- Packet payload builders (1.8 layouts, per EraAdapter legacy behavior) ---
    static std::vector<uint8_t> buildJoinGame()
    {
        std::vector<uint8_t> p;
        LightweightMC::Network::Packet::writeInt(p, 1);                       // Entity ID
        p.push_back(1);                                                       // Gamemode (Creative)
        p.push_back(0);                                                       // Dimension
        p.push_back(1);                                                       // Difficulty
        p.push_back(100);                                                     // Max Players (1.8+)
        LightweightMC::Network::Packet::writeString(p, "flat");               // Level Type
        p.push_back(0);                                                       // Reduced Debug Info
        return p;
    }

    static std::vector<uint8_t> buildChat(const std::string &text)
    {
        std::vector<uint8_t> p;
        LightweightMC::Network::Packet::writeString(p, text);
        return p;
    }

    static std::vector<uint8_t> buildKeepAlive(int32_t id)
    {
        std::vector<uint8_t> p;
        LightweightMC::Network::Packet::writeInt(p, id);
        return p;
    }

    static std::vector<uint8_t> buildSpawnPosition(const LightweightMC::Player::PlayerSession &s)
    {
        std::vector<uint8_t> p;
        LightweightMC::Network::Packet::writeInt(p, s.spawnX);
        LightweightMC::Network::Packet::writeInt(p, s.spawnY);
        LightweightMC::Network::Packet::writeInt(p, s.spawnZ);
        return p;
    }

    static std::vector<uint8_t> buildBlockChange()
    {
        std::vector<uint8_t> p;
        // Position (int) + block id (varint) — 1.8 layout.
        LightweightMC::Network::Packet::writeInt(p, 0);     // x
        LightweightMC::Network::Packet::writeInt(p, 64);    // y
        LightweightMC::Network::Packet::writeInt(p, 0);     // z
        LightweightMC::Network::Packet::writeVarInt(p, 1);  // block (stone)
        return p;
    }

    static std::vector<uint8_t> buildAddPlayer(const LightweightMC::Player::PlayerSession &s)
    {
        std::vector<uint8_t> p;
        LightweightMC::Network::Packet::writeVarInt(p, 1); // Entity ID
        for (int i = 0; i < 12; ++i)
            p.push_back(0);                                // UUID (zeroed for test)
        LightweightMC::Network::Packet::writeInt(p, static_cast<int32_t>(s.x * 32.0));
        LightweightMC::Network::Packet::writeInt(p, static_cast<int32_t>(s.y * 32.0));
        LightweightMC::Network::Packet::writeInt(p, static_cast<int32_t>(s.z * 32.0));
        p.push_back(static_cast<uint8_t>(s.yaw));
        p.push_back(static_cast<uint8_t>(s.pitch));
        LightweightMC::Network::Packet::writeShort(p, 0);
        p.push_back(0x7F);
        return p;
    }

    // --- Logging helper ---
    void logPacket(const LightweightMC::Network::Protocol::BaseProtocol &proto,
                   LightweightMC::Network::Protocol::PacketKind kind, std::vector<uint8_t> payload, int index) const
    {
        const int32_t id = proto.s2cPacketId(kind);
        if (id < 0)
        {
            LightweightMC::Core::Logger::warn("[TestPackets] #" + std::to_string(index) + " kind=" +
                                              std::to_string(static_cast<int>(kind)) + " not supported for this protocol");
            return;
        }
        std::vector<uint8_t> framed = proto.encodeFrame(payload);
        LightweightMC::Core::Logger::info("[TestPackets] #" + std::to_string(index) + " kind=" +
                                         std::to_string(static_cast<int>(kind)) + " wireId=0x" + toHex(id) +
                                         " payloadBytes=" + std::to_string(payload.size()) +
                                         " framedBytes=" + std::to_string(framed.size()));
    }

    static std::string toHex(int32_t v)
    {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%X", static_cast<unsigned>(v));
        return buf;
    }
};
