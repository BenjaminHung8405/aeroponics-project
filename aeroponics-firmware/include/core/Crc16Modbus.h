#pragma once

#include <cstddef>
#include <cstdint>

/**
 * CRC-16/Modbus: init 0xFFFF, reflected polynomial 0xA001, no XOR-out.
 * Matches Delphi TSCI.CalCRC16 / CheckCRC16 and the legacy AVR assembly.
 */

/** @brief Standard initial register value. */
constexpr uint16_t kCrc16ModbusInitialValue = 0xFFFF;

/** @brief Reflected polynomial (CRC-16/MODBUS). */
constexpr uint16_t kCrc16ModbusPolynomial = 0xA001;

/** @brief Trailing CRC field size in bytes. */
constexpr size_t kCrc16ModbusSize = 2;

/**
 * @brief Calculate the CRC-16/Modbus checksum of a data buffer.
 * @param data Buffer to inspect; may be null only when @p len is 0.
 * @param len  Number of bytes in @p data.
 * @return The 16-bit checksum, or 0 when @p data is null while @p len != 0
 *         (fail-closed, mirroring RfFrameCodec::calculateCrc16).
 *
 * An empty buffer (@p len == 0) returns kCrc16ModbusInitialValue (0xFFFF).
 * Pure function, zero state, no heap allocation.
 */
uint16_t calculateCrc16Modbus(const uint8_t* data, size_t len);

/**
 * @brief Append the CRC-16/Modbus checksum to a frame in little-endian order.
 * @param[out] frame  Buffer holding @p data_len payload bytes.
 * @param[in]  data_len   Payload length already written to @p frame.
 * @param[in]  capacity   Usable capacity of @p frame in bytes.
 * @return Total frame length (@p data_len + kCrc16ModbusSize) on success,
 *         or 0 when @p frame is null or @p capacity < @p data_len + 2
 *         (fail-closed; nothing is written past the buffer).
 *
 * CRC_Lo is written at frame[data_len] and CRC_Hi at frame[data_len + 1],
 * matching the legacy SendComCRC16 "Chr(Lo)" then "Chr(Hi)" order.
 */
size_t appendCrc16Modbus(uint8_t* frame, size_t data_len, size_t capacity);

/**
 * @brief Verify a frame whose trailing two bytes carry the CRC-16/Modbus.
 * @param frame Buffer to inspect; may be null only when @p len is 0.
 * @param len   Total length including the two CRC bytes.
 * @return true when the frame is intact, false otherwise.
 *
 * The check exploits the Modbus remainder property: running the algorithm
 * over the whole [data][crc_lo][crc_hi] packet yields 0x0000 exactly when the
 * data matches the trailing checksum, matching Delphi TSCI.CheckCRC16.
 * A null frame always returns false (fail-closed).
 */
bool verifyCrc16Modbus(const uint8_t* frame, size_t len);
