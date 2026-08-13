#include "core/hmac_sha256.h"
#include <cstring>

namespace {
inline uint32_t ror(uint32_t val, uint32_t bits) {
    return (val >> bits) | (val << (32 - bits));
}

const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef4a3f7, 0xc67178f2
};
} // namespace

Sha256::Sha256() {
    init();
}

void Sha256::init() {
    state_[0] = 0x6a09e667;
    state_[1] = 0xbb67ae85;
    state_[2] = 0x3c6ef372;
    state_[3] = 0xa54ff53a;
    state_[4] = 0x510e527f;
    state_[5] = 0x9b05688c;
    state_[6] = 0x1f83d9ab;
    state_[7] = 0x5be0cd19;
    count_ = 0;
}

void Sha256::transform(const uint8_t data[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(data[i * 4]) << 24) |
               (static_cast<uint32_t>(data[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(data[i * 4 + 2]) << 8) |
               (static_cast<uint32_t>(data[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + K256[i] + w[i];
        uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
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

void Sha256::update(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        buffer_[count_ % 64] = data[i];
        count_++;
        if (count_ % 64 == 0) {
            transform(buffer_);
        }
    }
}

void Sha256::final(uint8_t out[SHA256_HASH_SIZE]) {
    uint64_t total_bits = count_ * 8;
    size_t pad_idx = count_ % 64;
    buffer_[pad_idx++] = 0x80;

    if (pad_idx > 56) {
        while (pad_idx < 64) buffer_[pad_idx++] = 0x00;
        transform(buffer_);
        pad_idx = 0;
    }
    while (pad_idx < 56) buffer_[pad_idx++] = 0x00;

    for (int i = 7; i >= 0; --i) {
        buffer_[56 + (7 - i)] = static_cast<uint8_t>((total_bits >> (i * 8)) & 0xFF);
    }
    transform(buffer_);

    for (int i = 0; i < 8; ++i) {
        out[i * 4]     = static_cast<uint8_t>((state_[i] >> 24) & 0xFF);
        out[i * 4 + 1] = static_cast<uint8_t>((state_[i] >> 16) & 0xFF);
        out[i * 4 + 2] = static_cast<uint8_t>((state_[i] >> 8) & 0xFF);
        out[i * 4 + 3] = static_cast<uint8_t>(state_[i] & 0xFF);
    }
}

bool HmacSha256::calculate(const uint8_t* key, size_t key_len,
                           const uint8_t* data, size_t data_len,
                           uint8_t out[SHA256_HASH_SIZE]) {
    if (out == nullptr || (key == nullptr && key_len != 0) || (data == nullptr && data_len != 0)) {
        return false;
    }
    uint8_t k_pad[64];
    std::memset(k_pad, 0, sizeof(k_pad));

    if (key_len > 64) {
        Sha256 k_sha;
        k_sha.update(key, key_len);
        k_sha.final(k_pad);
    } else {
        std::memcpy(k_pad, key, key_len);
    }

    uint8_t ipad[64];
    uint8_t opad[64];
    for (size_t i = 0; i < 64; ++i) {
        ipad[i] = k_pad[i] ^ 0x36;
        opad[i] = k_pad[i] ^ 0x5c;
    }

    uint8_t inner_hash[SHA256_HASH_SIZE];
    Sha256 inner;
    inner.update(ipad, 64);
    inner.update(data, data_len);
    inner.final(inner_hash);

    Sha256 outer;
    outer.update(opad, 64);
    outer.update(inner_hash, SHA256_HASH_SIZE);
    outer.final(out);
    return true;
}

bool HmacSha256::calculateTruncated(const uint8_t* key, size_t key_len,
                                    const uint8_t* data, size_t data_len,
                                    uint8_t out_tag[HMAC_TAG_SIZE]) {
    if (out_tag == nullptr) return false;
    uint8_t full_hash[SHA256_HASH_SIZE];
    if (!calculate(key, key_len, data, data_len, full_hash)) return false;
    std::memcpy(out_tag, full_hash, HMAC_TAG_SIZE);
    return true;
}

bool constantTimeCompare(const uint8_t* a, const uint8_t* b, size_t len) {
    if (a == nullptr || b == nullptr) return false;
    uint8_t result = 0;
    for (size_t i = 0; i < len; ++i) {
        result |= (a[i] ^ b[i]);
    }
    return result == 0;
}
