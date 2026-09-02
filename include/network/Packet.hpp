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
        static double readDouble(const uint8_t *buffer, size_t &offset);

        static inline int floorMod(int a, int b)
        {
            return (a % b + b) % b;
        }

        static inline int floorDiv(int a, int b)
        {
            return (a < 0) ? ((a - b + 1) / b) : (a / b);
        }
    };

} // namespace LightweightMC::Network