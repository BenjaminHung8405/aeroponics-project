#include <unity.h>

#include <cstddef>
#include <cstdint>

#include "core/Crc16Modbus.h"

/* ----------------------------------------------------------------
 * CRC-16/Modbus unit tests (Sprint 1 — S1-T3)
 *
 * Strategy: assert exact independent values from the Big Plan. A test is
 * never "fixed" to match the implementation; the implementation is the
 * side that must change (S1-HARD-01).
 * ----------------------------------------------------------------
 */

void setUp(void) {}
void tearDown(void) {}

/** Standard check vector: ASCII "123456789" -> 0x4B37 (S1-HARD-01). */
extern "C" void test_crc16_modbus_standard_vector_4b37(void) {
    const uint8_t input[] = "123456789";
    TEST_ASSERT_EQUAL_HEX16(0x4B37, calculateCrc16Modbus(input, 9));
}

/** Big Plan pump-on vector: {0x04,0x06,0x09} -> 0xA7F3 (lo 0xF3, hi 0xA7). */
extern "C" void test_crc16_modbus_bigplan_pump_on_vector(void) {
    const uint8_t data[] = {0x04, 0x06, 0x09};
    TEST_ASSERT_EQUAL_HEX16(0xA7F3, calculateCrc16Modbus(data, sizeof(data)));

    uint8_t frame[5] = {0x04, 0x06, 0x09, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT(5, appendCrc16Modbus(frame, 3, sizeof(frame)));
    const uint8_t expected[5] = {0x04, 0x06, 0x09, 0xF3, 0xA7};
    TEST_ASSERT_EQUAL_MEMORY(expected, frame, sizeof(expected));
    TEST_ASSERT_TRUE(verifyCrc16Modbus(frame, sizeof(frame)));
}

/** Big Plan pump-off vector: {0x04,0x07,0x09} -> 0x37F2 (lo 0xF2, hi 0x37). */
extern "C" void test_crc16_modbus_bigplan_pump_off_vector(void) {
    const uint8_t data[] = {0x04, 0x07, 0x09};
    TEST_ASSERT_EQUAL_HEX16(0x37F2, calculateCrc16Modbus(data, sizeof(data)));

    uint8_t frame[5] = {0x04, 0x07, 0x09, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT(5, appendCrc16Modbus(frame, 3, sizeof(frame)));
    const uint8_t expected[5] = {0x04, 0x07, 0x09, 0xF2, 0x37};
    TEST_ASSERT_EQUAL_MEMORY(expected, frame, sizeof(expected));
    TEST_ASSERT_TRUE(verifyCrc16Modbus(frame, sizeof(frame)));
}

/** appendCrc16Modbus writes CRC_Lo then CRC_Hi and returns data_len + 2. */
extern "C" void test_crc16_modbus_append_layout(void) {
    uint8_t frame[5] = {0x04, 0x06, 0x09, 0x00, 0x00};
    const size_t total = appendCrc16Modbus(frame, 3, sizeof(frame));
    TEST_ASSERT_EQUAL_UINT(5, total);
    TEST_ASSERT_EQUAL_HEX8(0xF3, frame[3]);
    TEST_ASSERT_EQUAL_HEX8(0xA7, frame[4]);
    TEST_ASSERT_EQUAL_HEX16(0xA7F3,
                            static_cast<uint16_t>(frame[3] | (frame[4] << 8)));
}

/**
 * Null / zero-length convention (S1-HARD-02).
 * len == 0 keeps the initial register value 0xFFFF (convention shared with
 * RfFrameCodec::calculateCrc16; the empty-input policy is fixed for Sprint 2).
 * A null pointer with a non-zero length is rejected fail-closed.
 */
extern "C" void test_crc16_modbus_null_and_zero(void) {
    TEST_ASSERT_EQUAL_HEX16(kCrc16ModbusInitialValue, calculateCrc16Modbus(nullptr, 0));
    TEST_ASSERT_EQUAL_HEX16(0, calculateCrc16Modbus(nullptr, 10));
    TEST_ASSERT_FALSE(verifyCrc16Modbus(nullptr, 8));
}

/** appendCrc16Modbus fails closed and never writes past the capacity. */
extern "C" void test_crc16_modbus_append_rejects_insufficient_capacity(void) {
    uint8_t frame[4] = {0x04, 0x06, 0x09, 0xAA};

    // capacity 4 < data_len 3 + 2 -> reject, guard bytes untouched.
    TEST_ASSERT_EQUAL_UINT(0, appendCrc16Modbus(frame, 3, 4));
    TEST_ASSERT_EQUAL_HEX8(0xAA, frame[3]);

    // Null frame is rejected as well.
    TEST_ASSERT_EQUAL_UINT(0, appendCrc16Modbus(nullptr, 3, 5));

    // data_len beyond capacity must not underflow the capacity check.
    TEST_ASSERT_EQUAL_UINT(0, appendCrc16Modbus(frame, 9, 4));
}

/** verifyCrc16Modbus rejects frames that cannot carry a CRC. */
extern "C" void test_crc16_modbus_verify_rejects_short_frames(void) {
    uint8_t frame[2] = {0x04, 0x06};
    TEST_ASSERT_FALSE(verifyCrc16Modbus(frame, 0));
    TEST_ASSERT_FALSE(verifyCrc16Modbus(frame, 1));
    TEST_ASSERT_FALSE(verifyCrc16Modbus(frame, kCrc16ModbusSize));
}

/** Anti-tamper: a single-bit flip anywhere in the frame must be detected. */
extern "C" void test_crc16_modbus_tamper_detection(void) {
    uint8_t pristine[5] = {0x04, 0x06, 0x09, 0xF3, 0xA7};
    TEST_ASSERT_TRUE(verifyCrc16Modbus(pristine, sizeof(pristine)));

    for (size_t index = 0; index < sizeof(pristine); ++index) {
        for (int bit = 0; bit < 8; ++bit) {
            uint8_t frame[5];
            for (size_t i = 0; i < sizeof(pristine); ++i) {
                frame[i] = pristine[i];
            }
            frame[index] = static_cast<uint8_t>(frame[index] ^ (1u << bit));
            TEST_ASSERT_FALSE_MESSAGE(verifyCrc16Modbus(frame, sizeof(frame)),
                                      "single-bit flip must invalidate the frame");
        }
    }
}

/** Frame envelope limit: 253 payload bytes + 2 CRC bytes = 255 bytes. */
extern "C" void test_crc16_modbus_max_length_255(void) {
    uint8_t frame[255] = {};
    for (size_t i = 0; i < 253; ++i) {
        frame[i] = static_cast<uint8_t>(i);
    }

    const size_t total = appendCrc16Modbus(frame, 253, sizeof(frame));
    TEST_ASSERT_EQUAL_UINT(255, total);
    TEST_ASSERT_TRUE(verifyCrc16Modbus(frame, total));
    TEST_ASSERT_EQUAL_HEX16(calculateCrc16Modbus(frame, 253),
                            static_cast<uint16_t>(frame[253] | (frame[253 + 1] << 8)));

    // Empty payload still round-trips through the append/verify pair.
    uint8_t crc_only[2] = {};
    TEST_ASSERT_EQUAL_UINT(2, appendCrc16Modbus(crc_only, 0, sizeof(crc_only)));
    TEST_ASSERT_EQUAL_HEX8(0xFF, crc_only[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, crc_only[1]);
    TEST_ASSERT_TRUE(verifyCrc16Modbus(crc_only, sizeof(crc_only)));
}
