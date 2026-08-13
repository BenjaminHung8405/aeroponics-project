#pragma once

#include <cstdint>
#include <cstddef>

constexpr size_t SHA256_HASH_SIZE = 32;
// Actuator commands require at least 128 bits of authentication strength.
constexpr size_t HMAC_TAG_SIZE = 16;

class Sha256 {
public:
    Sha256();
    void init();
    void update(const uint8_t* data, size_t len);
    void final(uint8_t out[SHA256_HASH_SIZE]);

private:
    uint32_t state_[8];
    uint64_t count_;
    uint8_t buffer_[64];
    void transform(const uint8_t data[64]);
};

class HmacSha256 {
public:
    /**
     * Returns false without hashing when a required buffer is null. A null key
     * or data pointer is permitted only for a zero-length input.
     */
    static bool calculate(const uint8_t* key, size_t key_len,
                          const uint8_t* data, size_t data_len,
                          uint8_t out[SHA256_HASH_SIZE]);

    static bool calculateTruncated(const uint8_t* key, size_t key_len,
                                   const uint8_t* data, size_t data_len,
                                   uint8_t out_tag[HMAC_TAG_SIZE]);
};

/**
 * @brief Constant-time byte array comparison to prevent timing side-channel attacks.
 * @return true if equal, false otherwise.
 */
bool constantTimeCompare(const uint8_t* a, const uint8_t* b, size_t len);
