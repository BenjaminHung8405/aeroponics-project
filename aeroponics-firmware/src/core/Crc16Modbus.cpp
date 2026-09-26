#include "core/Crc16Modbus.h"

// ---------------------------------------------------------------------------
// CRC-16/Modbus — pure function, zero state, no heap allocation.
//
// The reflected (LSB-first) bit-by-bit algorithm matches the Delphi
// TSCI.CalCRC16 / CheckCRC16 reference and legacy AVR assembly CheckCRC16.
// Whole-frame verification gives exactly 0x0000 on data integrity, the
// Modbus self-cancelling property.
//
// On ESP32 and native host this compiles to a tight loop.  On ATmega8 the
// absence of a 256-byte LUT saves precious SRAM (quality gate S1-HARD-05).
// ---------------------------------------------------------------------------

uint16_t calculateCrc16Modbus(const uint8_t* data, size_t len) {
    // Fail-closed: null pointer with non-zero length returns 0.
    if (data == nullptr && len != 0) {
        return 0;
    }

    uint16_t crc = kCrc16ModbusInitialValue;

    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1) ? ((crc >> 1) ^ kCrc16ModbusPolynomial) : (crc >> 1);
        }
    }

    return crc;
}

size_t appendCrc16Modbus(uint8_t* frame, size_t data_len, size_t capacity) {
    // Fail-closed: null frame or insufficient capacity → return 0.
    if (frame == nullptr || data_len > capacity ||
        capacity - data_len < kCrc16ModbusSize) {
        return 0;
    }

    const uint16_t crc = calculateCrc16Modbus(frame, data_len);

    // Little-endian byte order: CRC_Lo at frame[data_len], CRC_Hi at frame[data_len+1].
    frame[data_len]     = static_cast<uint8_t>(crc & 0xFF);
    frame[data_len + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);

    return data_len + kCrc16ModbusSize;
}

bool verifyCrc16Modbus(const uint8_t* frame, size_t len) {
    if (frame == nullptr || len < kCrc16ModbusSize) {
        return false;
    }

    // Whole-frame remainder property: crc over [data][crc_lo][crc_hi] is 0
    // exactly when data matches the trailing checksum, matching
    // Delphi TSCI.CheckCRC16.
    return calculateCrc16Modbus(frame, len) == 0;
}
