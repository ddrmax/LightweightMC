#pragma once
#include "network/protocol/PacketKind.hpp"
#include "network/protocol/ProtocolVersion.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace LightweightMC::Network::Protocol
{
    // ---------------------------------------------------------------------------
    // BaseProtocol — version-specific framing, packet IDs and field encodings.
    //
    // This class complements (and does NOT replace) EraAdapter:
    //   - EraAdapter     : block/item/chunk encoding per era (unchanged).
    //   - BaseProtocol   : wire framing, S2C/C2S packet ID mapping, position/
    //                      chat/login field encodings.
    //
    // The virtuals below carry the 1.8 (protocol 47) behavior as defaults so
    // that the oldest supported clients work out of the box; subclasses only
    // override what actually changes per version range:
    //   - LegacyProtocol : 1.7.x - 1.12.x (protocol 4 - 210)
    //   - ModernProtocol : 1.13  - 1.20.x (protocol 315 - 766)
    //   - LatestProtocol : 1.21+           (protocol 769+)
    class BaseProtocol
    {
    public:
        virtual ~BaseProtocol() = default;

        // ------------------------------------------------------------------
        // Identity
        // ------------------------------------------------------------------
        // Negotiated protocol number (e.g. 47 for 1.8).
        virtual int32_t protocolVersion() const = 0;
        // Era bucket used by the EraAdapter selection logic.
        virtual Era era() const { return Era::LEGACY; }
        // Human-readable version range, e.g. "1.7.x-1.12.x".
        virtual std::string versionName() const = 0;

        // ------------------------------------------------------------------
        // Framing (compression is added here in Phase 4, gated per-session)
        // ------------------------------------------------------------------
        virtual bool isCompressionEnabled() const { return false; }
        virtual int32_t compressionThreshold() const { return -1; }
        // Builds the wire frame for a packet body. Default: plain varint length prefix.
        virtual std::vector<uint8_t> encodeFrame(const std::vector<uint8_t> &body) const;
        // Parses one frame from buf[0..len). Returns true and fills outBody on success,
        // false if fewer bytes than needed are present. Default: plain varint length prefix.
        virtual bool decodeFrame(const uint8_t *buf, size_t len, std::vector<uint8_t> &outBody) const;

        // ------------------------------------------------------------------
        // Packet ID mapping (the core of "no version-specific rewrites")
        // ------------------------------------------------------------------
        // Wire ID for a semantic S2C packet kind. Returns -1 if the kind is not
        // defined for this protocol version (caller must skip the send).
        virtual int32_t s2cPacketId(PacketKind kind) const = 0;
        // Semantic C2S packet kind for a wire ID. Returns PacketKind::UNKNOWN
        // when the ID is not mapped (unknown packets are dropped by callers).
        virtual PacketKind c2sPacketKind(int32_t wireId) const = 0;

        // ------------------------------------------------------------------
        // Field-level encodings that differ across versions.
        // Defaults implement the 1.8 behavior (double x/y/z, float yaw/pitch).
        // ------------------------------------------------------------------
        virtual void encodePosition(std::vector<uint8_t> &out, double x, double y, double z, float yaw, float pitch) const;
        virtual void decodePosition(const uint8_t *d, size_t &off, size_t end, double &x, double &y, double &z, float &yaw, float &pitch) const;

        // Chat text encoding (legacy § color codes by default).
        virtual std::string encodeChat(const std::string &text) const { return text; }

        // Builds the Login Success payload. Default: 1.8 layout (UUID + name).
        virtual void buildLoginSuccess(std::vector<uint8_t> &out, const std::string &uuid, const std::string &name) const;

        // ------------------------------------------------------------------
        // Factory — returns a process-lifetime singleton for the version range
        // that matches the negotiated protocol number.
        // ------------------------------------------------------------------
        static BaseProtocol *create(int32_t protocolVersion);
    };
} // namespace LightweightMC::Network::Protocol