#include "storage/RegionFile.hpp"
#include <lz4.h>

namespace LightweightMC::Storage {

std::vector<uint8_t> RegionFile::compressLZ4(const uint8_t* src, int srcSize) {
    int maxDstSize = LZ4_compressBound(srcSize);
    std::vector<uint8_t> dst(maxDstSize);
    int compressedSize = LZ4_compress_default(
        reinterpret_cast<const char*>(src),
        reinterpret_cast<char*>(dst.data()),
        srcSize, maxDstSize
    );
    dst.resize(compressedSize);
    return dst;
}

std::vector<uint8_t> RegionFile::decompressLZ4(const uint8_t* src, int srcSize, int dstSize) {
    std::vector<uint8_t> dst(dstSize);
    LZ4_decompress_safe(
        reinterpret_cast<const char*>(src),
        reinterpret_cast<char*>(dst.data()),
        srcSize, dstSize
    );
    return dst;
}

} // namespace LightweightMC::Storage