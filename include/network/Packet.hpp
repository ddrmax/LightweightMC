#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace LightweightMC::Network
{

    class Packet
    {
    public:
        static int32_t readVarInt(const uint8_t *buffer, size_t &offset);
        static int32_t readVarInt(const uint8_t *buffer, size_t &offset, size_t maxLen);
        static void writeVarInt(std::vector<uint8_t> &buffer, int32_t value);

        static std::string readString(const uint8_t *buffer, size_t &offset);
        static void writeString(std::vector<uint8_t> &vec, const std::string &str);

        static void writeDouble(std::vector<uint8_t> &vec, double val);
        static void writeFloat(std::vector<uint8_t> &vec, float val);
        static void writeInt(std::vector<uint8_t> &vec, int32_t val);

        static uint64_t readUInt64(const uint8_t *buffer, size_t &offset);
        static int16_t readShort(const uint8_t *buffer, size_t &offset);
        static void writeShort(std::vector<uint8_t> &vec, int16_t val);

        // Append a 16-bit value in the chunk block-data wire order (big-endian).
        // Explicit shift/mask so the byte order is fixed regardless of host endianness --
        // swapping these two lines would make every block decode to garbage on the client.
        static inline void writeU16BE(std::vector<uint8_t> &vec, uint16_t val)
        {
            vec.push_back(static_cast<uint8_t>(val & 0xFF));
            vec.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
        }
        static inline void writeU16BE2(std::vector<uint8_t> &vec, uint16_t val)
        {
            vec.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
            vec.push_back(static_cast<uint8_t>(val & 0xFF));
        }
        // One-time sanity check that the big-endian write and read helpers agree, so a future
        // edit can never silently swap one side. If they disagree, every block in a chunk packet
        // decodes to a wrong ID and renders as air ("no blocks show up"). Returns true when OK.
        static inline bool chunkU16RoundTripConsistent()
        {
            std::vector<uint8_t> buf;
            writeU16BE(buf, 0x1234);
            // The read side (see decodeChunk) reassembles as (hi << 8) | lo.
            uint16_t back = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
            return back == 0x1234 && buf.size() == 2;
        }
        static double readDouble(const uint8_t *buffer, size_t &offset);

        // Bounds-checked variants of the read helpers above. Every field is validated
        // against `packetEnd` so that malformed or truncated data can never make
        // Packet::read* walk past the buffer (stack/heap corruption). On failure they
        // return a sentinel value and leave `offset` untouched.
        static int32_t readVarIntBounded(const uint8_t *buffer, size_t &offset, size_t packetEnd, size_t minBytes);
        static int16_t readShortBounded(const uint8_t *buffer, size_t &offset, size_t packetEnd);
        static double readDoubleBounded(const uint8_t *buffer, size_t &offset, size_t packetEnd);
        static uint64_t readUInt64Bounded(const uint8_t *buffer, size_t &offset, size_t packetEnd);
        static int32_t readByteBounded(const uint8_t *buffer, size_t &offset, size_t packetEnd);
        static std::string readStringBounded(const uint8_t *buffer, size_t &offset, size_t packetEnd);

        static inline int floorMod(int a, int b)
        {
            return (a % b + b) % b;
        }

        // Decode the Y coordinate from a modern (1.8+) packed position long.
        // The y field is 12 bits wide (0..4095): values >= 2048 are negative, so
        // absY = raw - 4096. This supports the full modern world range (-64..319).
        static inline int decodeModernY(uint64_t posLong)
        {
            int32_t rawY = static_cast<int32_t>((posLong >> 26) & 0xFFF);
            return (rawY >= 2048) ? rawY - 4096 : rawY;
        }

        static inline void writeByte(std::vector<uint8_t> &vec, uint8_t val) { vec.push_back(val); }
        static inline void writeSpawnObject(std::vector<uint8_t> &vec, int32_t entityId, double x, double y, double z) {
            writeVarInt(vec, 0x0E);
            writeVarInt(vec, entityId);
            vec.push_back(2);
            writeInt(vec, static_cast<int32_t>(x * 32));
            writeInt(vec, static_cast<int32_t>(y * 32));
            writeInt(vec, static_cast<int32_t>(z * 32));
            vec.push_back(0); vec.push_back(0);
            writeInt(vec, 1);
            writeShort(vec, 0); writeShort(vec, 0); writeShort(vec, 0);
        }
        static inline void writeEntityMetadataItem(std::vector<uint8_t> &vec, int32_t entityId, int16_t itemId, uint8_t count, uint16_t damage) {
            writeVarInt(vec, 0x1C);
            writeVarInt(vec, entityId);
            vec.push_back((5 << 5) | 10);
            writeShort(vec, itemId);
            vec.push_back(count);
            writeShort(vec, static_cast<int16_t>(damage));
            vec.push_back(0);
            vec.push_back(0x7F);
        }
        static inline void writeCollectItem(std::vector<uint8_t> &vec, int32_t collectedId, int32_t collectorId) {
            writeVarInt(vec, 0x0D);
            writeVarInt(vec, collectedId);
            writeVarInt(vec, collectorId);
        }
        static inline void writeDestroyEntities(std::vector<uint8_t> &vec, int32_t entityId) {
            writeVarInt(vec, 0x13);
            writeVarInt(vec, 1);
            writeVarInt(vec, entityId);
        }
        static inline void writeSetSlot(std::vector<uint8_t> &vec, uint8_t windowId, int16_t slot, int16_t itemId, uint8_t count, int16_t damage) {
            writeVarInt(vec, 0x29);
            vec.push_back(windowId);
            writeShort(vec, slot);
            writeShort(vec, itemId);
            if (itemId != -1) { vec.push_back(count); writeShort(vec, damage); vec.push_back(0); }
        }
        static inline int floorDiv(int a, int b)
        {
            return (a < 0) ? ((a - b + 1) / b) : (a / b);
        }
    };

} // namespace LightweightMC::Network