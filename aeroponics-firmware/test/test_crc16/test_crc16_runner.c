#include <unity.h>

/* ----------------------------------------------------------------
 * Unity test runner for the CRC-16/Modbus suite (Sprint 1 — S1-T4).
 *
 * The test bodies live in test_crc16.cpp with extern "C" linkage so the
 * native C/C++ linker can resolve them from this C translation unit.
 * Native host only — no hardware dependency.
 * ----------------------------------------------------------------
 */

void setUp(void);
void tearDown(void);

void test_crc16_modbus_standard_vector_4b37(void);
void test_crc16_modbus_bigplan_pump_on_vector(void);
void test_crc16_modbus_bigplan_pump_off_vector(void);
void test_crc16_modbus_append_layout(void);
void test_crc16_modbus_null_and_zero(void);
void test_crc16_modbus_append_rejects_insufficient_capacity(void);
void test_crc16_modbus_verify_rejects_short_frames(void);
void test_crc16_modbus_tamper_detection(void);
void test_crc16_modbus_max_length_255(void);

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_crc16_modbus_standard_vector_4b37);
    RUN_TEST(test_crc16_modbus_bigplan_pump_on_vector);
    RUN_TEST(test_crc16_modbus_bigplan_pump_off_vector);
    RUN_TEST(test_crc16_modbus_append_layout);
    RUN_TEST(test_crc16_modbus_null_and_zero);
    RUN_TEST(test_crc16_modbus_append_rejects_insufficient_capacity);
    RUN_TEST(test_crc16_modbus_verify_rejects_short_frames);
    RUN_TEST(test_crc16_modbus_tamper_detection);
    RUN_TEST(test_crc16_modbus_max_length_255);

    return UNITY_END();
}