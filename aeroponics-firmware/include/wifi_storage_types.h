#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "config.h"

#ifdef MAX_SSID_LEN
#undef MAX_SSID_LEN
#endif

struct WifiProfile {
    char ssid[WIFI_MAX_SSID_LEN + 1] = {};
    char password[WIFI_MAX_PASS_LEN + 1] = {};
    int8_t priority = 0;
    int8_t last_rssi = -127;
    uint32_t last_success_epoch = 0;
};

struct WifiConfigBlob {
    uint32_t magic = WIFI_BLOB_MAGIC;
    uint8_t version = WIFI_BLOB_VERSION;
    uint8_t count = 0;
    uint8_t reserved[2] = {0, 0};
    WifiProfile profiles[MAX_SAVED_WIFI] = {};
    uint32_t crc32 = 0;
};

namespace wifi_crypto {

inline uint32_t calculateCrc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

inline uint32_t computeBlobCrc(const WifiConfigBlob& blob) {
    // Compute CRC over all fields preceding crc32
    constexpr size_t checked_length = offsetof(WifiConfigBlob, crc32);
    return calculateCrc32(reinterpret_cast<const uint8_t*>(&blob), checked_length);
}

inline bool verifyBlobCrc(const WifiConfigBlob& blob) {
    if (blob.magic != WIFI_BLOB_MAGIC || blob.version != WIFI_BLOB_VERSION || blob.count > MAX_SAVED_WIFI) {
        return false;
    }
    return computeBlobCrc(blob) == blob.crc32;
}

} // namespace wifi_crypto
