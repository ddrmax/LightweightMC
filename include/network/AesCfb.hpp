#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace LightweightMC::Network
{
    // Minimal AES-128 in CFB-128 mode. Kept self-contained (no OpenSSL) so
    // the whole server stays link-light on embedded targets and so the
    // legacy + token-auth code paths never drag a crypto library in when
    // the config has auth-mode = offline (the default).
    //
    // Minecraft's login handshake (when enabled) uses CFB-128 with the
    // AES key derived from the login token and IV = the same key. Both
    // directions (server→client and client→server) keep their own
    // independent state so a single AesCfb128 instance per direction is
    // the right shape:
    //
    //     Network::Auth::AesCfb128 s2c, c2s;   // separate keystreams
    //     s2c.setKey(key16); s2c.setIv(iv16);
    //     s2c.encrypt(out, n);  // after login packet is acked
    //     c2s.setKey(key16); c2s.setIv(iv16);
    //     c2s.decrypt(in, n);   // before dispatching to the state machine
    //
    // This module is instantiated on demand by Authentication.cpp; the
    // rest of the codebase stays cipher-agnostic.
    class AesCfb128
    {
    public:
        AesCfb128() = default;

        // Load a 16-byte AES key. Recomputes the 10-round key schedule.
        void setKey(const uint8_t *key16);
        // Load a 16-byte IV (defaults to zero). Resets the internal
        // state vector used by encrypt/decrypt.
        void setIv(const uint8_t *iv16);

        // XOR `n` bytes of `buf` with the keystream produced by AES-
        // encrypting the current state vector, then feed the *ciphertext*
        // (i.e. the buffer AFTER the XOR) back into the state -- the
        // standard CFB-128 forward path.
        void encrypt(uint8_t *buf, std::size_t n);
        // Symmetric counterpart: XOR `n` bytes with the same keystream,
        // feeding the *ciphertext* (the buffer BEFORE the XOR) back into
        // the state.
        void decrypt(uint8_t *buf, std::size_t n);

        // Reset the state vector back to the last setIv value; used when
        // a caller wants to reuse the same key across a batch of packets
        // but restart the keystream.
        void reset();

        // True when setKey() has been called at least once.
        bool armed() const { return m_armed; }

    private:
        uint8_t m_key[16]{};
        uint8_t m_iv[16]{};
        uint8_t m_state[16]{};
        uint8_t m_roundKeys[176]{};
        bool m_armed{false};
    };
} // namespace LightweightMC::Network
