#pragma once
#include <vector>
#include <cstdint>
#include <string_view>

namespace LightweightMC::Storage {

class RegionFile {
public:
    static std::vector<uint8_t> compressLZ4(const uint8_t* src, int srcSize);
    static std::vector<uint8_t> decompressLZ4(const uint8_t* src, int srcSize, int dstSize);
};

} // namespace LightweightMC::Storage