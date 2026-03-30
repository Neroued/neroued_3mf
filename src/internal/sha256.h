// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

/// \file sha256.h
/// \brief Minimal SHA-256 + HMAC-SHA-256 (header-only, zero external dependencies).
/// Public-domain algorithm, used solely for watermark keystream generation.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace neroued_3mf::detail {

class SHA256 {
  public:
    static constexpr std::size_t kDigestSize = 32;
    static constexpr std::size_t kBlockSize = 64;

    SHA256() { Reset(); }

    void Reset() {
        state_[0] = 0x6a09e667u;
        state_[1] = 0xbb67ae85u;
        state_[2] = 0x3c6ef372u;
        state_[3] = 0xa54ff53au;
        state_[4] = 0x510e527fu;
        state_[5] = 0x9b05688cu;
        state_[6] = 0x1f83d9abu;
        state_[7] = 0x5be0cd19u;
        count_ = 0;
        buf_len_ = 0;
    }

    void Update(const uint8_t *data, std::size_t len) {
        while (len > 0) {
            std::size_t avail = kBlockSize - buf_len_;
            std::size_t n = len < avail ? len : avail;
            std::memcpy(buf_ + buf_len_, data, n);
            buf_len_ += n;
            data += n;
            len -= n;
            count_ += n;
            if (buf_len_ == kBlockSize) {
                Compress(buf_);
                buf_len_ = 0;
            }
        }
    }

    std::array<uint8_t, kDigestSize> Final() {
        uint64_t bits = count_ * 8;
        uint8_t pad = 0x80;
        Update(&pad, 1);
        pad = 0;
        while (buf_len_ != 56) { Update(&pad, 1); }
        uint8_t len_be[8];
        for (int i = 7; i >= 0; --i) {
            len_be[i] = static_cast<uint8_t>(bits);
            bits >>= 8;
        }
        Update(len_be, 8);

        std::array<uint8_t, kDigestSize> digest{};
        for (int i = 0; i < 8; ++i) {
            digest[4 * i] = static_cast<uint8_t>(state_[i] >> 24);
            digest[4 * i + 1] = static_cast<uint8_t>(state_[i] >> 16);
            digest[4 * i + 2] = static_cast<uint8_t>(state_[i] >> 8);
            digest[4 * i + 3] = static_cast<uint8_t>(state_[i]);
        }
        return digest;
    }

  private:
    static uint32_t Rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
    static uint32_t Ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
    static uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
    static uint32_t BigSigma0(uint32_t x) { return Rotr(x, 2) ^ Rotr(x, 13) ^ Rotr(x, 22); }
    static uint32_t BigSigma1(uint32_t x) { return Rotr(x, 6) ^ Rotr(x, 11) ^ Rotr(x, 25); }
    static uint32_t SmallSigma0(uint32_t x) { return Rotr(x, 7) ^ Rotr(x, 18) ^ (x >> 3); }
    static uint32_t SmallSigma1(uint32_t x) { return Rotr(x, 17) ^ Rotr(x, 19) ^ (x >> 10); }

    void Compress(const uint8_t *block) {
        static constexpr uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
            0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
            0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
            0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
            0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
            0xc67178f2,
        };

        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(block[4 * i]) << 24) | (uint32_t(block[4 * i + 1]) << 16) |
                   (uint32_t(block[4 * i + 2]) << 8) | uint32_t(block[4 * i + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            w[i] = SmallSigma1(w[i - 2]) + w[i - 7] + SmallSigma0(w[i - 15]) + w[i - 16];
        }

        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = h + BigSigma1(e) + Ch(e, f, g) + K[i] + w[i];
            uint32_t t2 = BigSigma0(a) + Maj(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    uint32_t state_[8]{};
    uint64_t count_ = 0;
    uint8_t buf_[kBlockSize]{};
    std::size_t buf_len_ = 0;
};

/// HMAC-SHA-256 (RFC 2104).
inline std::array<uint8_t, 32> HmacSHA256(const uint8_t *key, std::size_t key_len,
                                          const uint8_t *msg, std::size_t msg_len) {
    constexpr std::size_t B = SHA256::kBlockSize;
    uint8_t k_block[B]{};

    if (key_len > B) {
        SHA256 h;
        h.Update(key, key_len);
        auto hk = h.Final();
        std::memcpy(k_block, hk.data(), SHA256::kDigestSize);
    } else {
        std::memcpy(k_block, key, key_len);
    }

    uint8_t ipad[B], opad[B];
    for (std::size_t i = 0; i < B; ++i) {
        ipad[i] = k_block[i] ^ 0x36u;
        opad[i] = k_block[i] ^ 0x5cu;
    }

    SHA256 inner;
    inner.Update(ipad, B);
    inner.Update(msg, msg_len);
    auto ihash = inner.Final();

    SHA256 outer;
    outer.Update(opad, B);
    outer.Update(ihash.data(), SHA256::kDigestSize);
    return outer.Final();
}

} // namespace neroued_3mf::detail
