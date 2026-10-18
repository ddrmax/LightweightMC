#include "storage/RegionFile.hpp"
#include <lz4.h>
#include "third_party/minigzip.h"

namespace LightweightMC::Storage
{

    std::vector<uint8_t> RegionFile::compressLZ4(const uint8_t *src, int srcSize)
    {
        int maxDstSize = LZ4_compressBound(srcSize);
        std::vector<uint8_t> dst(maxDstSize);
        int compressedSize = LZ4_compress_default(
            reinterpret_cast<const char *>(src),
            reinterpret_cast<char *>(dst.data()),
            srcSize, maxDstSize);
        dst.resize(compressedSize);
        return dst;
    }

    std::vector<uint8_t> RegionFile::decompressLZ4(const uint8_t *src, int srcSize, int dstSize)
    {
        std::vector<uint8_t> dst(dstSize);
        LZ4_decompress_safe(
            reinterpret_cast<const char *>(src),
            reinterpret_cast<char *>(dst.data()),
            srcSize, dstSize);
        return dst;
    }

    // Legacy MCC/MCE region-file codec. Vanilla Java clients emit gzip
    // streams produced by Java's `gzip` module (RFC 1952 container, raw
    // DEFLATE payload). The bundled third_party/minigzip.h implements
    // both directions without depending on an external network fetch
    // for zlib -- only the stored-block encoder + full DEFLATE decoder
    // are needed here. Vanilla client parsers accept any valid DEFLATE
    // stream, so emitting stored blocks on compress is safe.
    std::vector<uint8_t> RegionFile::compressGzip(const uint8_t *src, int srcSize)
    {
        std::vector<uint8_t> out;
        minigzip::gzip_compress(
            src, static_cast<std::size_t>(srcSize > 0 ? srcSize : 0), out);
        return out;
    }

    std::vector<uint8_t> RegionFile::decompressGzip(const uint8_t *src, int srcSize)
    {
        std::vector<uint8_t> out;
        minigzip::gzip_decompress(
            src, static_cast<std::size_t>(srcSize > 0 ? srcSize : 0), out);
        if (out.empty())
            return out;
        return out;
    }

} // namespace LightweightMC::Storage
