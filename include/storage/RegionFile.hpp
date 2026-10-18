#pragma once
#include <vector>
#include <cstdint>
#include <string_view>

namespace LightweightMC::Storage
{

    class RegionFile
    {
    public:
        static std::vector<uint8_t> compressLZ4(const uint8_t *src, int srcSize);
        static std::vector<uint8_t> decompressLZ4(const uint8_t *src, int srcSize, int dstSize);
        static std::vector<uint8_t> compressGzip(const uint8_t *src, int srcSize);
        static std::vector<uint8_t> decompressGzip(const uint8_t *src, int srcSize);
    };

} // namespace LightweightMC::Storage