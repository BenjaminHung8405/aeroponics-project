#include "core/hmac_sha256.h"
#include <cstring>

#if defined(ATMEGA8_NODE_BUILD)
#include <avr/pgmspace.h>

namespace {
const uint32_t K256_AVR[64] PROGMEM = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL, 0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL,
    0xc19bf174UL, 0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL, 0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL, 0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664cUL, 0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL, 0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};
inline uint32_t rotr(uint32_t x, uint8_t n) { return (x >> n) | (x << (32U - n)); }
void transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[16];
    for (uint8_t i = 0; i < 16; ++i) w[i] = (uint32_t(block[i*4]) << 24) | (uint32_t(block[i*4+1]) << 16) | (uint32_t(block[i*4+2]) << 8) | block[i*4+3];
    uint32_t a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
    for (uint8_t i=0; i<64; ++i) {
        uint8_t j=i&15;
        if (i>=16) w[j] += (rotr(w[(j+1)&15],7)^rotr(w[(j+1)&15],18)^(w[(j+1)&15]>>3)) + w[(j+9)&15] + (rotr(w[(j+14)&15],17)^rotr(w[(j+14)&15],19)^(w[(j+14)&15]>>10));
        uint32_t s1=rotr(e,6)^rotr(e,11)^rotr(e,25), ch=(e&f)^((~e)&g);
        uint32_t t1=h+s1+ch+pgm_read_dword(&K256_AVR[i])+w[j];
        uint32_t s0=rotr(a,2)^rotr(a,13)^rotr(a,22), maj=(a&b)^(a&c)^(b&c), t2=s0+maj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d; state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}
void initState(uint32_t s[8]) { s[0]=0x6a09e667UL;s[1]=0xbb67ae85UL;s[2]=0x3c6ef372UL;s[3]=0xa54ff53aUL;s[4]=0x510e527fUL;s[5]=0x9b05688cUL;s[6]=0x1f83d9abUL;s[7]=0x5be0cd19UL; }
void digest(const uint8_t* data, size_t len, uint8_t out[32]) {
    uint8_t block[128] = {}; uint32_t s[8]; initState(s);
    for (size_t i=0;i<len;++i) block[i]=data[i];
    block[len]=0x80;
    const uint16_t bits = uint16_t(len * 8U);
    const uint8_t length_offset = (len > 55U) ? 120U : 56U;
    block[length_offset] = uint8_t(bits >> 8);
    block[length_offset + 1U] = uint8_t(bits);
    transform(s, block); if (len > 55) transform(s, block+64);
    for (uint8_t i=0;i<8;++i) { out[i*4]=uint8_t(s[i]>>24);out[i*4+1]=uint8_t(s[i]>>16);out[i*4+2]=uint8_t(s[i]>>8);out[i*4+3]=uint8_t(s[i]); }
}
}

bool HmacSha256::calculateTruncated(const uint8_t* key, size_t key_len, const uint8_t* data, size_t data_len, uint8_t out_tag[HMAC_TAG_SIZE]) {
    if (!key || !data || !out_tag || key_len != 16 || data_len > 55) return false;
    uint8_t inner[128] = {}, outer[128] = {}, hash[32];
    for (uint8_t i=0;i<64;++i) { inner[i]=0x36; outer[i]=0x5c; if (i<16) { inner[i]^=key[i]; outer[i]^=key[i]; } }
    std::memcpy(inner+64, data, data_len); digest(inner, 64+data_len, hash);
    std::memcpy(outer+64, hash, 32); digest(outer, 96, hash); std::memcpy(out_tag, hash, HMAC_TAG_SIZE); return true;
}
bool constantTimeCompare(const uint8_t* a, const uint8_t* b, size_t len) { if (!a || !b) return false; uint8_t r=0; for(size_t i=0;i<len;++i) r|=a[i]^b[i]; return r==0; }
#else


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
    // A rolling 16-word schedule keeps the AVR stack bounded (the previous
    // implementation reserved 256 bytes for all 64 words).
    uint32_t w[16];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(data[i * 4]) << 24) |
               (static_cast<uint32_t>(data[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(data[i * 4 + 2]) << 8) |
               (static_cast<uint32_t>(data[i * 4 + 3]));
    }
    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];

    for (int i = 0; i < 64; ++i) {
        if (i >= 16) {
            const uint8_t slot = static_cast<uint8_t>(i & 15);
            const uint32_t s0 = ror(w[(slot + 1) & 15], 7) ^
                                ror(w[(slot + 1) & 15], 18) ^
                                (w[(slot + 1) & 15] >> 3);
            const uint32_t s1 = ror(w[(slot + 14) & 15], 17) ^
                                ror(w[(slot + 14) & 15], 19) ^
                                (w[(slot + 14) & 15] >> 10);
            w[slot] += s0 + w[(slot + 9) & 15] + s1;
        }
        uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + K256[i] + w[i & 15];
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
    } else if (key_len > 0) {
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

#endif
