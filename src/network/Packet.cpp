#include "network/Packet.hpp"

namespace LightweightMC::Network
{

    int32_t Packet::readVarInt(const uint8_t *buffer, size_t &offset)
    {
        int32_t value = 0;
        int32_t position = 0;
        uint8_t byte;

        while (true)
        {
            byte = buffer[offset++];
            value |= (byte & 0x7F) << position;
            if ((byte & 0x80) == 0)
                break;
            position += 7;
            if (position >= 32)
                return 0;
        }
        return value;
    }
    int32_t Packet::readVarInt(const uint8_t *buffer, size_t &offset, size_t maxLen)
    {
        int32_t numRead = 0;
        int32_t value = 0;
        uint8_t byte;
        do
        {
            if (offset >= maxLen)
                return -1; // Incomplete
            byte = buffer[offset++];
            int32_t value = (byte & 0x7F);
            value |= (value << (7 * numRead));
            numRead++;
            if (numRead > 5)
                return -1; // Protocol Error
        } while ((byte & 0x80) != 0);
        return value;
    }

    void Packet::writeVarInt(std::vector<uint8_t> &buffer, int32_t value)
    {
        uint32_t uval = static_cast<uint32_t>(value);
        while (true)
        {
            if ((uval & ~0x7F) == 0)
            {
                buffer.push_back(uval);
                return;
            }
            buffer.push_back((uval & 0x7F) | 0x80);
            uval >>= 7;
        }
    }

    std::string Packet::readString(const uint8_t *buffer, size_t &offset)
    {
        int32_t len = Packet::readVarInt(buffer, offset);
        if (len < 0 || len > 32767)
            return "";
        std::string str(reinterpret_cast<const char *>(buffer + offset), len);
        offset += len;
        return str;
    }

    void Packet::writeString(std::vector<uint8_t> &vec, const std::string &str)
    {
        Packet::writeVarInt(vec, static_cast<int32_t>(str.size()));
        vec.insert(vec.end(), str.begin(), str.end());
    }

    void Packet::writeDouble(std::vector<uint8_t> &vec, double val)
    {
        uint64_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        for (int i = 7; i >= 0; --i)
            vec.push_back((bits >> (i * 8)) & 0xFF);
    }

    void Packet::writeFloat(std::vector<uint8_t> &vec, float val)
    {
        uint32_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        for (int i = 3; i >= 0; --i)
            vec.push_back((bits >> (i * 8)) & 0xFF);
    }

    void Packet::writeInt(std::vector<uint8_t> &vec, int32_t val)
    {
        for (int i = 3; i >= 0; --i)
            vec.push_back((val >> (i * 8)) & 0xFF);
    }

    uint64_t Packet::readUInt64(const uint8_t *buffer, size_t &offset)
    {
        uint64_t val = 0;
        for (int i = 0; i < 8; ++i)
        {
            val = (val << 8) | buffer[offset++];
        }
        return val;
    }

    int16_t Packet::readShort(const uint8_t *buffer, size_t &offset)
    {
        int16_t val = (buffer[offset] << 8) | buffer[offset + 1];
        offset += 2;
        return val;
    }

    void Packet::writeShort(std::vector<uint8_t> &vec, int16_t val)
    {
        vec.push_back((val >> 8) & 0xFF);
        vec.push_back(val & 0xFF);
    }

    double Packet::readDouble(const uint8_t *buffer, size_t &offset)
    {
        uint64_t bits = 0;
        for (int i = 0; i < 8; ++i)
        {
            bits = (bits << 8) | buffer[offset++];
        }
        double val;
        std::memcpy(&val, &bits, sizeof(val));
        return val;
    }

} // namespace LightweightMC::Network