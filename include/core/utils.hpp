#pragma once
#include <fstream>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <array>
#include <cstdint>
#include <string_view>
#define SECTION "\xC2\xA7"
#define LWMC_VER "0.0.15-alpha"

namespace LightweightMC::Core
{
    // Helper to retrieve RAM consumed by the process (in MB)
    static inline double getProcessRAM()
    {
        std::ifstream statm("/proc/self/statm");
        long pages = 0;
        if (statm >> pages)
        {
            return (pages * sysconf(_SC_PAGESIZE)) / (1024.0 * 1024.0);
        }
        return 0.0;
    }

    // Helper to retrieve the system CPU load
    static inline double getCPUUsage()
    {
        double load[1];
        if (getloadavg(load, 1) != -1)
        {
            return load[0];
        }
        return 0.0;
    }

    // Helper to retrieve the peer IP and port of a connected socket as "IP:Port"
    static inline std::string getIpAndPortFromFd(int fd)
    {
        sockaddr_in addr{};
        socklen_t addrLen = sizeof(addr);
        if (getpeername(fd, (struct sockaddr *)&addr, &addrLen) != 0)
        {
            return "Unknown IP:0";
        }

        char ipBuf[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, &addr.sin_addr, ipBuf, sizeof(ipBuf)))
        {
            return "Unknown IP:0";
        }

        return std::string(ipBuf) + ":" + std::to_string(ntohs(addr.sin_port));
    }
    // Helper to retrieve the peer IP and port of a connected socket whith IP as a string and Port as unsigned 16 bit integer
    static inline bool getIpAndPortFromFd(int fd, std::string &outIp, uint16_t &outPort)
    {
        sockaddr_in addr{};
        socklen_t addrLen = sizeof(addr);

        if (getpeername(fd, reinterpret_cast<struct sockaddr *>(&addr), &addrLen) != 0)
        {
            return false;
        }

        char ipBuf[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &addr.sin_addr, ipBuf, sizeof(ipBuf)) == nullptr)
        {
            return false;
        }

        outIp = ipBuf;
        outPort = ntohs(addr.sin_port);

        return true;
    }

    namespace detail
    {
        struct MD5Context
        {
            uint32_t state[4]{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
            uint32_t count[2]{0, 0};
            uint8_t buffer[64]{0};
        };

        inline void md5_transform(uint32_t state[4], const uint8_t block[64])
        {
            auto rotate_left = [](uint32_t x, int n)
            { return (x << n) | (x >> (32 - n)); };
            uint32_t a = state[0], b = state[1], c = state[2], d = state[3], x[16];
            for (int i = 0; i < 16; ++i)
            {
                x[i] = static_cast<uint32_t>(block[i * 4]) |
                       (static_cast<uint32_t>(block[i * 4 + 1]) << 8) |
                       (static_cast<uint32_t>(block[i * 4 + 2]) << 16) |
                       (static_cast<uint32_t>(block[i * 4 + 3]) << 24);
            }

#define FF(a, b, c, d, x, s, ac)                          \
    {                                                     \
        (a) += (((b) & (c)) | ((~b) & (d))) + (x) + (ac); \
        (a) = rotate_left((a), (s)) + (b);                \
    }
#define GG(a, b, c, d, x, s, ac)                          \
    {                                                     \
        (a) += (((b) & (d)) | ((c) & (~d))) + (x) + (ac); \
        (a) = rotate_left((a), (s)) + (b);                \
    }
#define HH(a, b, c, d, x, s, ac)               \
    {                                          \
        (a) += ((b) ^ (c) ^ (d)) + (x) + (ac); \
        (a) = rotate_left((a), (s)) + (b);     \
    }
#define II(a, b, c, d, x, s, ac)                  \
    {                                             \
        (a) += ((c) ^ ((b) | (~d))) + (x) + (ac); \
        (a) = rotate_left((a), (s)) + (b);        \
    }

            FF(a, b, c, d, x[0], 7, 0xd76aa478);
            FF(d, a, b, c, x[1], 12, 0xe8c7b756);
            FF(c, d, a, b, x[2], 17, 0x242070db);
            FF(b, c, d, a, x[3], 22, 0xc1bdceee);
            FF(a, b, c, d, x[4], 7, 0xf57c0faf);
            FF(d, a, b, c, x[5], 12, 0x4787c62a);
            FF(c, d, a, b, x[6], 17, 0xa8304613);
            FF(b, c, d, a, x[7], 22, 0xfd469501);
            FF(a, b, c, d, x[8], 7, 0x698098d8);
            FF(d, a, b, c, x[9], 12, 0x8b44f7af);
            FF(c, d, a, b, x[10], 17, 0xffff5bb1);
            FF(b, c, d, a, x[11], 22, 0x895cd7be);
            FF(a, b, c, d, x[12], 7, 0x6b901122);
            FF(d, a, b, c, x[13], 12, 0xfd987193);
            FF(c, d, a, b, x[14], 17, 0xa679438e);
            FF(b, c, d, a, x[15], 22, 0x49b40821);

            GG(a, b, c, d, x[1], 5, 0xf61e2562);
            GG(d, a, b, c, x[6], 9, 0xc040b340);
            GG(c, d, a, b, x[11], 14, 0x265e5a51);
            GG(b, c, d, a, x[0], 20, 0xe9b6c7aa);
            GG(a, b, c, d, x[5], 5, 0xd62f105d);
            GG(d, a, b, c, x[10], 9, 0x02441453);
            GG(c, d, a, b, x[15], 14, 0xd8a1e681);
            GG(b, c, d, a, x[4], 20, 0xe7d3fbc8);
            GG(a, b, c, d, x[9], 5, 0x21e1cde6);
            GG(d, a, b, c, x[14], 9, 0xc33707d6);
            GG(c, d, a, b, x[3], 14, 0xf4d50d87);
            GG(b, c, d, a, x[8], 20, 0x455a14ed);
            GG(a, b, c, d, x[13], 5, 0xa9e3e905);
            GG(d, a, b, c, x[2], 9, 0xfcefa3f8);
            GG(c, d, a, b, x[7], 14, 0x676f02d9);
            GG(b, c, d, a, x[12], 20, 0x8d2a4c8a);

            HH(a, b, c, d, x[5], 4, 0xfffa3942);
            HH(d, a, b, c, x[8], 11, 0x8771f681);
            HH(c, d, a, b, x[11], 16, 0x6d9d6122);
            HH(b, c, d, a, x[14], 23, 0xfde5380c);
            HH(a, b, c, d, x[1], 4, 0xa4beea44);
            HH(d, a, b, c, x[4], 11, 0x4bdecfa9);
            HH(c, d, a, b, x[7], 16, 0xf6bb4b60);
            HH(b, c, d, a, x[10], 23, 0xbebfbc70);
            HH(a, b, c, d, x[13], 4, 0x289b7ec6);
            HH(d, a, b, c, x[0], 11, 0xeaa127fa);
            HH(c, d, a, b, x[3], 16, 0xd4ef3085);
            HH(b, c, d, a, x[6], 23, 0x04881d05);
            HH(a, b, c, d, x[9], 4, 0xd9d4d039);
            HH(d, a, b, c, x[12], 11, 0xe6db99e5);
            HH(c, d, a, b, x[15], 16, 0x1fa27cf8);
            HH(b, c, d, a, x[2], 23, 0xc4ac5665);

            II(a, b, c, d, x[0], 6, 0xf4292244);
            II(d, a, b, c, x[7], 10, 0x432aff97);
            II(c, d, a, b, x[14], 15, 0xab9423a7);
            II(b, c, d, a, x[5], 21, 0xfc93a039);
            II(a, b, c, d, x[12], 6, 0x655b59c3);
            II(d, a, b, c, x[3], 10, 0x8f0ccc92);
            II(c, d, a, b, x[10], 15, 0xffeff47d);
            II(b, c, d, a, x[1], 21, 0x85845dd1);
            II(a, b, c, d, x[8], 6, 0x6fa87e4f);
            II(d, a, b, c, x[15], 10, 0xfe2ce6e0);
            II(c, d, a, b, x[6], 15, 0xa3014314);
            II(b, c, d, a, x[13], 21, 0x4e0811a1);
            II(a, b, c, d, x[4], 6, 0xf7537e82);
            II(d, a, b, c, x[11], 10, 0xbd3af235);
            II(c, d, a, b, x[2], 15, 0x2ad7d2bb);
            II(b, c, d, a, x[9], 21, 0xeb86d391);
#undef FF
#undef GG
#undef HH
#undef II

            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
        }
    }

    inline std::array<uint8_t, 16> md5(const std::string_view data)
    {
        detail::MD5Context ctx;
        size_t input_len = data.size();
        size_t index = 0, part_len = 0;

        for (size_t i = 0; i < input_len; ++i)
        {
            ctx.buffer[(ctx.count[0] >> 3) & 0x3F] = static_cast<uint8_t>(data[i]);
            ctx.count[0] += 8;
            if (ctx.count[0] < 8)
                ctx.count[1]++;
            if (((ctx.count[0] >> 3) & 0x3F) == 0)
            {
                detail::md5_transform(ctx.state, ctx.buffer);
            }
        }

        uint8_t bits[8];
        for (int i = 0; i < 8; ++i)
        {
            bits[i] = static_cast<uint8_t>((ctx.count[i >= 4 ? 1 : 0] >> ((i % 4) * 8)) & 0xFF);
        }

        index = (ctx.count[0] >> 3) & 0x3F;
        part_len = (index < 56) ? (56 - index) : (120 - index);

        static const uint8_t PADDING[64] = {0x80};
        std::string_view pad(reinterpret_cast<const char *>(PADDING), part_len);

        for (size_t i = 0; i < part_len; ++i)
        {
            ctx.buffer[(ctx.count[0] >> 3) & 0x3F] = static_cast<uint8_t>(pad[i]);
            ctx.count[0] += 8;
            if (ctx.count[0] < 8)
                ctx.count[1]++;
            if (((ctx.count[0] >> 3) & 0x3F) == 0)
            {
                detail::md5_transform(ctx.state, ctx.buffer);
            }
        }

        for (int i = 0; i < 8; ++i)
        {
            ctx.buffer[56 + i] = bits[i];
        }
        detail::md5_transform(ctx.state, ctx.buffer);

        std::array<uint8_t, 16> digest{};
        for (int i = 0; i < 16; ++i)
        {
            digest[i] = static_cast<uint8_t>((ctx.state[i / 4] >> ((i % 4) * 8)) & 0xFF);
        }
        return digest;
    }
} // namespace LightweightMC::Core