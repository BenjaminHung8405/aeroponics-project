#pragma once

#include <cstdint>

// Test-only fixture. This header is included only by native-integration and is
// never part of an ESP32 production build.
namespace integration_test {
inline constexpr uint32_t kPskWords[4] = {
    0xA5A5A5A5U, 0x5A5A5A5AU, 0x01234567U, 0x89ABCDEFU
};
inline constexpr uint8_t kPsk[16] = {
    0xA5, 0xA5, 0xA5, 0xA5, 0x5A, 0x5A, 0x5A, 0x5A,
    0x67, 0x45, 0x23, 0x01, 0xEF, 0xCD, 0xAB, 0x89
};
}
