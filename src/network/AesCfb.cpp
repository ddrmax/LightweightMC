// AES-128 core + CFB-128 keystream. Self-contained (no OpenSSL) so the
// whole server binary stays free of a crypto dependency; this cipher is
// only ever activated on demand by Authentication.cpp once a token
// verify relay hands back a session_key -- the default offline path
// never touches this code.
//
// The AES core here is textbook forward (SubBytes / ShiftRows /
// MixColumns / AddRoundKey). CFB only needs the *forward* cipher to
// produce the keystream; decrypt is the same keystream XORed back into
// the ciphertext.

#include "network/AesCfb.hpp"
#include <cstring>

namespace LightweightMC::Network
{
    namespace
    {
        constexpr std::uint8_t SBOX[256] = {
            0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
            0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
            0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xf8,0xc7,0xc8,
            0xef,0x2e,0xaa,0xfb,0xdb,0x48,0xe6,0x2c,0xb9,0x13,0x5e,0x0b,0x32,0x1a,0x78,0x3c,
            0x4e,0xae,0xa1,0x66,0x28,0xd1,0x41,0xd9,0xf8,0xe7,0x5b,0xac,0x90,0xb7,0x1e,0x46,
            0x8a,0x31,0x50,0x07,0x4b,0xae,0x19,0x2f,0xb3,0xbb,0x17,0x0e,0x07,0x3e,0x21,0x3a,
            0x08,0x06,0x3e,0x3b,0x56,0x72,0xc2,0x65,0xbe,0x63,0x7c,0x65,0x64,0x5a,0x44,0x75,
            0xba,0x12,0x1c,0xc0,0xae,0x73,0x57,0x5f,0x37,0xd7,0x70,0x14,0x27,0x36,0x21,0x35,
            0x0e,0xa4,0x25,0x18,0xb2,0x76,0x89,0xaa,0xca,0xbd,0x6b,0x47,0xbb,0x61,0xf6,0xca,
            0x4b,0x2b,0x3f,0x6a,0x31,0x4f,0x59,0xa0,0x9a,0x32,0xa4,0xca,0x19,0x8d,0x97,0x40,
            0x2e,0x87,0xef,0x28,0x07,0x75,0x9a,0x67,0xbf,0x58,0x0d,0x59,0x06,0x1d,0x06,0x0b,
            0x3e,0x2e,0x27,0x00,0x48,0x1b,0x53,0x73,0x84,0x7e,0xbb,0x76,0x93,0xbd,0x83,0x53,
            0x06,0x94,0x45,0x17,0xde,0x3c,0x4b,0x20,0x00,0x21,0x5c,0x0c,0x4c,0x7d,0x41,0x79,
            0x2b,0x1f,0x63,0xdc,0xe9,0x4a,0x4c,0x91,0x75,0x4f,0x10,0x21,0xc0,0xe0,0x43,0x17,
            0x00,0xe6,0xfb,0x53,0x07,0x21,0x35,0x4f,0x0e,0x43,0x9d,0x26,0x95,0x17,0x28,0x4e,
            0x43,0x43,0x0e,0x10,0x09,0x10,0xad,0x98,0x32,0x4c,0x19,0x40,0xeb,0x86,0x28,0x65,
        };
        constexpr std::uint8_t RCON[11] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};

        inline std::uint8_t xtime(std::uint8_t v)
        {
            return static_cast<std::uint8_t>((v << 1) ^ ((v & 0x80) ? 0x1b : 0x00));
        }
        inline std::uint8_t gmul(std::uint8_t a, std::uint8_t b)
        {
            std::uint8_t r = 0;
            while (b)
            {
                if (b & 1) r ^= a;
                a = xtime(a);
                b >>= 1;
            }
            return r;
        }

        inline void expandKey128(const std::uint8_t key[16], std::uint8_t out[176])
        {
            std::memcpy(out, key, 16);
            for (int i = 4; i < 16; ++i)
            {
                std::uint8_t t[4];
                std::memcpy(t, out + (i - 1) * 4, 4);
                if (i % 4 == 0)
                {
                    const std::uint8_t tmp = t[0];
                    t[0] = SBOX[t[1]];
                    t[1] = SBOX[t[2]];
                    t[2] = SBOX[t[3]];
                    t[3] = SBOX[tmp];
                    t[0] ^= RCON[i / 4];
                }
                for (int j = 0; j < 4; ++j)
                    out[i * 4 + j] = static_cast<std::uint8_t>(out[(i - 4) * 4 + j] ^ t[j]);
            }
        }

        inline void encryptBlock128(std::uint8_t block[16], const std::uint8_t rk[176])
        {
            for (int i = 0; i < 16; ++i) block[i] ^= rk[i];
            for (int round = 1; round <= 9; ++round)
            {
                for (int i = 0; i < 16; ++i) block[i] = SBOX[block[i]];
                std::uint8_t tmp[16];
                std::memcpy(tmp, block, 16);
                for (int c = 0; c < 4; ++c)
                    for (int r = 0; r < 4; ++r)
                        block[c * 4 + r] = tmp[((c + r) % 4) * 4 + r];
                for (int c = 0; c < 4; ++c)
                {
                    const std::uint8_t a0 = block[c * 4 + 0];
                    const std::uint8_t a1 = block[c * 4 + 1];
                    const std::uint8_t a2 = block[c * 4 + 2];
                    const std::uint8_t a3 = block[c * 4 + 3];
                    block[c * 4 + 0] = static_cast<std::uint8_t>(gmul(a0, 2) ^ gmul(a1, 3) ^ a2 ^ a3);
                    block[c * 4 + 1] = static_cast<std::uint8_t>(a0 ^ gmul(a1, 2) ^ gmul(a2, 3) ^ a3);
                    block[c * 4 + 2] = static_cast<std::uint8_t>(a0 ^ a1 ^ gmul(a2, 2) ^ gmul(a3, 3));
                    block[c * 4 + 3] = static_cast<std::uint8_t>(gmul(a0, 3) ^ a1 ^ a2 ^ gmul(a3, 2));
                }
                for (int i = 0; i < 16; ++i) block[i] ^= rk[round * 16 + i];
            }
            for (int i = 0; i < 16; ++i) block[i] = SBOX[block[i]];
            std::uint8_t tmp[16];
            std::memcpy(tmp, block, 16);
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    block[c * 4 + r] = tmp[((c + r) % 4) * 4 + r];
            for (int i = 0; i < 16; ++i) block[i] ^= rk[160 + i];
        }
    } // anonymous

    void AesCfb128::setKey(const std::uint8_t *key16)
    {
        std::memcpy(m_key, key16, 16);
        expandKey128(m_key, m_roundKeys);
        m_armed = true;
    }

    void AesCfb128::setIv(const std::uint8_t *iv16)
    {
        std::memcpy(m_iv, iv16, 16);
        std::memcpy(m_state, m_iv, 16);
    }

    void AesCfb128::reset()
    {
        std::memcpy(m_state, m_iv, 16);
    }

    void AesCfb128::encrypt(std::uint8_t *buf, std::size_t n)
    {
        if (!m_armed) return;
        std::size_t i = 0;
        while (i < n)
        {
            std::uint8_t block[16];
            std::memcpy(block, m_state, 16);
            encryptBlock128(block, m_roundKeys);
            for (std::size_t j = 0; i < n && j < 16; ++i, ++j)
            {
                const std::uint8_t out = static_cast<std::uint8_t>(buf[i] ^ block[j]);
                buf[i] = out;
                m_state[j] = out;
            }
        }
    }

    void AesCfb128::decrypt(std::uint8_t *buf, std::size_t n)
    {
        if (!m_armed) return;
        std::size_t i = 0;
        while (i < n)
        {
            std::uint8_t block[16];
            std::memcpy(block, m_state, 16);
            encryptBlock128(block, m_roundKeys);
            std::uint8_t cipher[16];
            const std::size_t take = (n - i) < 16 ? (n - i) : 16;
            std::memcpy(cipher, buf + i, take);
            for (std::size_t j = 0; j < take; ++j)
            {
                buf[i + j] = static_cast<std::uint8_t>(cipher[j] ^ block[j]);
                m_state[j] = cipher[j];
            }
            i += take;
        }
    }
} // namespace LightweightMC::Network

