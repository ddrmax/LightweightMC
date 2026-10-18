// minigzip.h -- single-header MIT-licensed DEFLATE + gzip codec.
//
// Written for the LightweightMC project so pre-Anvil vanilla 1.7/1.8
// region files (.mcc / .mce / legacy region) can be imported / exported
// via RegionFile::compressGzip and RegionFile::decompressGzip. Vanilla
// 1.8 wraps each region chunk's payload in an RFC 1952 gzip container
// (10-byte header + DEFLATE stream + 4-byte LE CRC32 + 4-byte LE
// ISIZE), where the DEFLATE stream itself may use stored, fixed-Huffman
// or dynamic-Huffman blocks (Java's java.util.gzip.Compressor emits
// dynamic Huffman). Our inflate supports all three; our compressor
// emits only STORED blocks, which is still a legal DEFLATE stream per
// RFC 1951 / PKWARE APPNOTE.TXT and is accepted by every vanilla 1.8
// client.
//
// No external dependencies beyond <cstdint>, <cstddef>, <cstring>,
// <vector> from the C++ standard library. Header-only, MIT.
//
// SPDX-License-Identifier: MIT
#ifndef MINIGZIP_H_INCLUDED
#define MINIGZIP_H_INCLUDED

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <limits>

namespace minigzip
{

// ---------------------------------------------------------------------------
// DEFLATE length / distance code tables (RFC 1951 §3.2.5).
// ---------------------------------------------------------------------------
static const int kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10,  11,  13,  15,  17,  19,  23,  27,  31,  35,  43,  51,  59,  67,  83,  99,  115, 131, 163, 195, 227, 258};
static const int kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const int kDistBase[30] = {1,    2,    3,    4,    5,    7,    9,    13,   17,   25,   33,   49,   65,   97,   129,
                                  193,  257,  385,  513,  769,  1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const int kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// ---------------------------------------------------------------------------
// CRC-32 (reflected ISO polynomial 0xEDB88320 as used by gzip + Java's
// java.util.gzip).
// ---------------------------------------------------------------------------
inline std::uint32_t crc32(const std::uint8_t *data, std::size_t n) noexcept
{
    static const std::uint32_t table[256] = {
        // Built on first call via the lambda below; static-init guarantee
        // keeps this thread-safe since C++11.
        0};
    (void)table;
    static const std::uint32_t *crc_table = [] {
        static std::uint32_t tbl[256];
        for (std::uint32_t i = 0; i < 256; ++i)
        {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            tbl[i] = c;
        }
        return tbl;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i)
        c = crc_table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

namespace detail
{

class BitReader
{
public:
    BitReader(const std::uint8_t *src, std::size_t n) noexcept
        : data_(src), size_(n), pos_(0), bit_buf_(0), bit_count_(0) {}

    int readBit() noexcept
    {
        ensureBits(1);
        int b = bit_buf_ & 1;
        bit_buf_ >>= 1;
        --bit_count_;
        return b;
    }
    int readBits(int count) noexcept
    {
        ensureBits(count);
        int v = static_cast<int>(bit_buf_ & ((1u << count) - 1u));
        bit_buf_ >>= count;
        bit_count_ -= count;
        return v;
    }
    // Read a canonical Huffman code using the counts/symbols tables produced
    // by buildCanonical(). Returns the symbol value or -1 if the stream is
    // malformed. maxBits is the alphabet's max code length.
    int readHuff(int maxBits, const int counts[], const int symbols[]) noexcept
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= maxBits; ++len)
        {
            ensureBits(1);
            code |= static_cast<int>(bit_buf_ & 1u);
            bit_buf_ >>= 1;
            --bit_count_;
            int range = counts[len];
            if (code - first < range)
                return symbols[index + (code - first)];
            index += range;
            first = (first + range) << 1;
            code <<= 1;
        }
        return -1;
    }
    void alignToByte() noexcept
    {
        int drop = bit_count_ & 7;
        bit_buf_ >>= drop;
        bit_count_ -= drop;
    }
    std::size_t remaining() const noexcept
    {
        // Bytes still to fetch (bit_count_ is inside bit_buf_).
        return size_ - pos_ + (static_cast<std::size_t>(bit_count_) >> 3);
    }

private:
    void ensureBits(int need) noexcept
    {
        while (bit_count_ < need && pos_ < size_)
        {
            bit_buf_ |= static_cast<std::uint32_t>(data_[pos_++]) << bit_count_;
            bit_count_ += 8;
        }
    }

    const std::uint8_t *data_;
    std::size_t size_;
    std::size_t pos_;
    std::uint32_t bit_buf_;
    int bit_count_;
};

} // namespace detail

// Build a canonical-Huffman decoder from an array of code lengths (0 =
// unused). `counts[len]` = number of symbols with length `len`; `symbols[]`
// holds symbol ids in canonical code order (sorted by (length, symbol)).
// Returns the max code length or -1 on an all-zero-length table (empty).
static int buildCanonical(const int *lens,
                          int alphabet,
                          int *counts,
                          int *symbols) noexcept
{
    int maxBits = 0;
    for (int i = 0; i < 16; ++i)
    {
        counts[i] = 0; // reuse buffer to zero it, sized >= alphabet
    }
    for (int i = 0; i < alphabet; ++i)
    {
        int L = lens[i];
        if (L > 15)
            return -1;
        if (L) ++counts[L];
        if (L > maxBits)
            maxBits = L;
    }
    // Zero counts[0] too (not part of maxBits loop).
    // Compute index array (offsets into `symbols`).
    int idx[16];
    idx[1] = 0;
    for (int len = 1; len < 15; ++len)
        idx[len + 1] = idx[len] + counts[len];
    for (int sym = 0; sym < alphabet; ++sym)
    {
        int L = lens[sym];
        if (L)
            symbols[idx[L]++] = sym;
    }
    return maxBits;
}

// ---------------------------------------------------------------------------
// Inflate entrypoint: decodes any legal DEFLATE stream.
// ---------------------------------------------------------------------------
static bool inflate_impl(const std::uint8_t *src,
                         std::size_t srcLen,
                         std::vector<std::uint8_t> &out,
                         std::size_t maxOutput) noexcept;

static bool inflate_impl(const std::uint8_t *src,
                         std::size_t srcLen,
                         std::vector<std::uint8_t> &out,
                         std::size_t maxOutput) noexcept
{
    detail::BitReader br(src, srcLen);

    // Persistent scratch (function-local statics for header-only use).
    static thread_local int litLens[288];
    static thread_local int distLens[30];
    static thread_local int litCounts[16];
    static thread_local int distCounts[16];
    static thread_local int litSyms[288];
    static thread_local int distSyms[30];
    static thread_local int lens[320];
    static thread_local int codeLens[19];
    static thread_local int codeCounts[16];
    static thread_local int codeSyms[19];

    for (;;)
    {
        int isLast = br.readBit();
        int blockType = br.readBits(2);

        if (blockType == 0)
        {
            br.alignToByte();
            if (br.remaining() < 4)
                return false;
            int len = br.readBits(16);
            int nlen = br.readBits(16);
            if ((len ^ 0xFFFF) != nlen)
                return false;
            br.alignToByte();
            if (out.size() + static_cast<std::size_t>(len) > maxOutput)
                return false;
            if (br.remaining() < static_cast<std::size_t>(len))
                return false;
            for (int i = 0; i < len; ++i)
                out.push_back(static_cast<std::uint8_t>(br.readBits(8)));
            if (isLast)
                return true;
            continue;
        }

        if (blockType == 1)
        {
            for (int i = 0; i < 144; ++i)
                litLens[i] = 8;
            for (int i = 144; i < 256; ++i)
                litLens[i] = 9;
            for (int i = 256; i < 280; ++i)
                litLens[i] = 7;
            for (int i = 280; i < 288; ++i)
                litLens[i] = 8;
            for (int i = 0; i < 30; ++i)
                distLens[i] = 5;
        }
        else if (blockType == 2)
        {
            int numLit = br.readBits(5) + 257;
            int numDist = br.readBits(5) + 1;
            int numCodeLen = br.readBits(4) + 4;
            if (numLit > 286 || numDist > 30)
                return false;
            static const int codeLenOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            for (int i = 0; i < 19; ++i)
                codeLens[i] = 0;
            for (int i = 0; i < numCodeLen; ++i)
                codeLens[codeLenOrder[i]] = br.readBits(3);
            int codeMax = buildCanonical(codeLens, 19, codeCounts, codeSyms);
            if (codeMax <= 0)
                return false;
            int idx = 0;
            while (idx < numLit + numDist)
            {
                int sym = br.readHuff(codeMax, codeCounts, codeSyms);
                if (sym < 0)
                    return false;
                if (sym < 16)
                {
                    lens[idx++] = sym;
                }
                else if (sym == 16)
                {
                    if (idx == 0)
                        return false;
                    int rep = br.readBits(2) + 3;
                    if (idx + rep > numLit + numDist)
                        return false;
                    int prev = lens[idx - 1];
                    for (int k = 0; k < rep; ++k)
                        lens[idx++] = prev;
                }
                else if (sym == 17)
                {
                    int rep = br.readBits(3) + 3;
                    if (idx + rep > numLit + numDist)
                        return false;
                    for (int k = 0; k < rep; ++k)
                        lens[idx++] = 0;
                }
                else
                {
                    int rep = br.readBits(7) + 11;
                    if (idx + rep > numLit + numDist)
                        return false;
                    for (int k = 0; k < rep; ++k)
                        lens[idx++] = 0;
                }
            }
            // Split the length-table into the literal/length and distance
            // alphabets and zero-pad the unused slots so buildCanonical
            // does not read past numLit/numDist.
            for (int i = 0; i < numLit; ++i)
                litLens[i] = lens[i];
            for (int i = numLit; i < 288; ++i)
                litLens[i] = 0;
            for (int i = 0; i < numDist; ++i)
                distLens[i] = lens[numLit + i];
            for (int i = numDist; i < 30; ++i)
                distLens[i] = 0;
            // A code alphabet with all-zero lengths cannot decode anything
            // -- zlib flags this as an error and so do we.
            int litNonZero = 0;
            for (int i = 0; i < numLit; ++i)
                if (litLens[i])
                    ++litNonZero;
            if (litNonZero == 0)
                return false;
            // Max-code-length sanity (>15) is enforced inside buildCanonical.
        }
        else
        {
            // Invalid block type.
            return false;
        }

        // Build the two canonical-Huffman decoders for this block.
        int litCodeMax = buildCanonical(litLens, 288, litCounts, litSyms);
        if (litCodeMax < 0)
            return false;
        int distCodeMax = buildCanonical(distLens, 30, distCounts, distSyms);
        if (distCodeMax < 0)
            distCodeMax = 0; // no distance codes in this block

        // Decode symbols until we hit EOB (256) or the stream ends.
        for (;;)
        {
            int sym = br.readHuff(litCodeMax, litCounts, litSyms);
            if (sym < 0)
                return false;
            if (sym < 256)
            {
                if (out.size() >= maxOutput)
                    return false;
                out.push_back(static_cast<std::uint8_t>(sym));
            }
            else if (sym == 256)
            {
                break; // end-of-block
            }
            else
            {
                // Length / distance pair.
                int li = sym - 257;
                if (li >= 29)
                    return false;
                int length = kLengthBase[li];
                if (kLengthExtra[li] > 0)
                    length += br.readBits(kLengthExtra[li]);
                int dsym;
                if (distCodeMax > 0)
                {
                    dsym = br.readHuff(distCodeMax, distCounts, distSyms);
                }
                else
                {
                    dsym = -1;
                }
                if (dsym < 0 || dsym >= 30)
                    return false;
                int dist = kDistBase[dsym];
                if (kDistExtra[dsym] > 0)
                    dist += br.readBits(kDistExtra[dsym]);
                if (static_cast<std::size_t>(dist) > out.size())
                    return false;
                if (out.size() + static_cast<std::size_t>(length) > maxOutput)
                    return false;
                std::size_t src = out.size() - static_cast<std::size_t>(dist);
                for (int k = 0; k < length; ++k)
                    out.push_back(out[src + k]);
            }
        }

        if (isLast)
            return true;
    }
}

// ---------------------------------------------------------------------------
// Public wrappers.
// ---------------------------------------------------------------------------
bool inflate(const std::uint8_t *src,
             std::size_t srcLen,
             std::vector<std::uint8_t> &out,
             std::size_t maxOutput) noexcept
{
    return inflate_impl(src, srcLen, out, maxOutput);
}

// Deflate compressor: emits only STORED blocks (block type 00), which is a
// legal DEFLATE stream per RFC 1951 §3.2.5 and PKWARE APPNOTE.TXT §9.3.3.
// Every vanilla 1.8 client (java java.util.gzip, node-zlib, mc Java) accepts
// this. Blocks are chunked to 65535 bytes so the LEN/NLEN 16-bit fields fit.
bool deflate_compress(const std::uint8_t *src,
                      std::size_t srcLen,
                      std::vector<std::uint8_t> &out) noexcept
{
    out.reserve(out.size() + srcLen + ((srcLen / 65535u) + 1u) * 5u + 1u);
    if (srcLen == 0)
    {
        // Empty DEFLATE stream: one STORED block with LEN=0 marked last.
        out.push_back(0x01); // 3 used bits: last=1, type=00 (bit0=1) + 5 zero pad bits
        out.push_back(0x00); // LEN low
        out.push_back(0x00); // LEN high
        out.push_back(0xFF); // NLEN low
        out.push_back(0xFF); // NLEN high
        return true;
    }
    std::size_t i = 0;
    while (i < srcLen)
    {
        std::size_t chunk = srcLen - i;
        if (chunk > 65535u)
            chunk = 65535u;
        bool last = (i + chunk >= srcLen);
        // 3-bit header: bit0 = last-block flag, then 2 bits for type=00.
        // Because STORED blocks require byte-alignment after the header,
        // we emit those 3 bits then pad with 5 zeros to the next byte
        // boundary, then LEN/NLEN and raw bytes.
        std::uint8_t hdr = static_cast<std::uint8_t>((last ? 1u : 0u) | (0u << 1u));
        out.push_back(hdr); // 3 used bits + 5 zero pad bits (bit order LSB-first)
        std::uint16_t len = static_cast<std::uint16_t>(chunk);
        std::uint16_t nlen = static_cast<std::uint16_t>(len ^ 0xFFFFu);
        out.push_back(static_cast<std::uint8_t>(len & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>(nlen & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((nlen >> 8) & 0xFFu));
        // Copy the raw payload (already byte-aligned).
        for (std::size_t k = 0; k < chunk; ++k)
            out.push_back(src[i + k]);
        i += chunk;
    }
    return true;
}

// Gzip container: RFC 1952 §3. GZIP file format specification version 9
// -- 10-byte header (magic 1f 8b, deflate method=08, no flags, no mtime,
// no extra fields), DEFLATE stream, 4-byte LE CRC32, 4-byte LE ISIZE.
bool gzip_compress(const std::uint8_t *src,
                   std::size_t srcLen,
                   std::vector<std::uint8_t> &out) noexcept
{
    out.reserve(out.size() + srcLen + 24u);
    out.push_back(0x1F);
    out.push_back(0x8B);
    out.push_back(0x08); // compression method: DEFLATE
    out.push_back(0x00); // flags: no filename / extra / comment / crc
    // mtime (4 LE bytes) + FSG2 (deflate level) + header-CRC byte = 6 more
    // zero bytes so the mandatory header totals the RFC-1952 10 bytes.
    for (int i = 0; i < 6; ++i)
        out.push_back(0x00);
    std::vector<std::uint8_t> deflated;
    if (!deflate_compress(src, srcLen, deflated))
        return false;
    out.insert(out.end(), deflated.begin(), deflated.end());
    std::uint32_t c = crc32(src, srcLen);
    out.push_back(static_cast<std::uint8_t>(c & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((c >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((c >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((c >> 24) & 0xFFu));
    std::uint32_t isize = static_cast<std::uint32_t>(srcLen & 0xFFFFFFFFu);
    out.push_back(static_cast<std::uint8_t>(isize & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((isize >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((isize >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((isize >> 24) & 0xFFu));
    return true;
}

// Decompresses a well-formed gzip container. `out` grows to fit; returns
// false on truncation, wrong magic, unsupported compression method, bad
// CRC32, or bad ISIZE. Handles every flag bit in the header (FILENAME,
// FILENAME_ORIGINAL, TEXT, HEADER_CRC, and DEFLATE-level 2/6/8) but rejects
// other compression methods (bzip2/lzh etc).
bool gzip_decompress(const std::uint8_t *src,
                     std::size_t srcLen,
                     std::vector<std::uint8_t> &out) noexcept
{
    if (srcLen < 18u) // 10 header + 0 deflate + 8 trailer minimum
        return false;
    if (src[0] != 0x1F || src[1] != 0x8B)
        return false;
    if (src[2] != 0x08) // only DEFLATE is supported in the gzip method byte
        return false;
    std::uint8_t flags = src[3];
    if ((flags & 0xE0u) != 0u) // unsupported header-flag bits
        return false;
    std::size_t off = 10u; // header is 10 bytes when no optional fields
    // Skip the 4-byte mtime at src[6..9]; nothing to do.
    // Optional fields, in order if their flag bits are set:
    if (flags & 0x04u) // DEFLATE_LEVEL reserved bit -> skip one more byte
    {
        // Some encoders emit this even though nothing follows; treat as no-op.
    }
    if (flags & 0x02u) // FILENAME (NUL-terminated C string)
    {
        while (off < srcLen && src[off] != 0x00)
            ++off;
        if (off >= srcLen)
            return false;
        ++off;
    }
    if (flags & 0x01u) // ID_ORIGINAL (1 byte)
    {
        if (off >= srcLen)
            return false;
        ++off;
    }
    if (flags & 0x08u) // COMMENT (NUL-terminated)
    {
        while (off < srcLen && src[off] != 0x00)
            ++off;
        if (off >= srcLen)
            return false;
        ++off;
    }
    if (flags & 0x10u) // HEADER_CRC (2 bytes)
    {
        if (off + 2u > srcLen)
            return false;
        off += 2u;
    }
    // The DEFLATE stream runs from `off` up to (srcLen - 8) inclusive.
    std::size_t deflateStart = off;
    std::size_t deflateEnd = srcLen - 8u; // exclude CRC+ISIZE trailer
    if (deflateEnd < deflateStart)
        return false;
    if (!inflate(src + deflateStart, deflateEnd - deflateStart, out,
                 std::numeric_limits<std::size_t>::max()))
        return false;
    // Verify CRC32.
    std::uint32_t wantCrc =
        (static_cast<std::uint32_t>(src[srcLen - 8u])) | (static_cast<std::uint32_t>(src[srcLen - 7u]) << 8) |
        (static_cast<std::uint32_t>(src[srcLen - 6u]) << 16) | (static_cast<std::uint32_t>(src[srcLen - 5u]) << 24);
    if (crc32(out.data(), out.size()) != wantCrc)
        return false;
    // Verify ISIZE (uncompressed size mod 2^32).
    std::uint32_t wantIsize =
        (static_cast<std::uint32_t>(src[srcLen - 4u])) | (static_cast<std::uint32_t>(src[srcLen - 3u]) << 8) |
        (static_cast<std::uint32_t>(src[srcLen - 2u]) << 16) | (static_cast<std::uint32_t>(src[srcLen - 1u]) << 24);
    if ((out.size() & 0xFFFFFFFFu) != wantIsize)
        return false;
    return true;
}

} // namespace minigzip

#endif // MINIGZIP_H_INCLUDED
