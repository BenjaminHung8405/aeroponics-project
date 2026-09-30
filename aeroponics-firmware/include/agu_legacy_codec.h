#ifndef AGU_LEGACY_CODEC_H
#define AGU_LEGACY_CODEC_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Industrial codec for AGU-Aeroponics legacy SCI serial protocol.
 *
 * Implements byte-level frame serialization and deserialization matching
 * the original Delphi diagnostic utility AGU-Aeroponics/TestSCI.dpr.
 */
namespace AguLegacy {

// Frame sync bytes and ACKs
constexpr uint8_t SYNC_BYTE_1 = 0xFF;
constexpr uint8_t SYNC_BYTE_2 = 0x5A;
constexpr uint8_t ACK_BYTE    = 0x5A; // ASCII 'Z'
constexpr uint8_t PING_DEFAULT_VAL = 0xA5;

// Command Opcodes
enum class Opcode : uint8_t {
    READ_MEM_WORD   = 0x01,
    WRITE_RAM       = 0x04,
    PING            = 0x05,
    PUMP_ON         = 0x06,
    PUMP_OFF        = 0x07,
    GET_PUMP_STATE  = 0x08,
    READ_EEPROM     = 0x08,
    WRITE_EEPROM    = 0x09,
    DEVICE_ID       = 0x0A,
    READ_RAM_BURST  = 0x0E,
};

// Maximum command and response buffer sizes
constexpr size_t MAX_CMD_SIZE = 16;
constexpr size_t BURST_DATA_SIZE = 8;

// SendComCRC16 envelope sizes. The length byte counts payload plus CRC bytes;
// the CRC itself covers the length byte and payload.
constexpr size_t BURST_RESPONSE_SIZE = 1 + BURST_DATA_SIZE + 2;
constexpr uint8_t BURST_RESPONSE_LENGTH = static_cast<uint8_t>(BURST_DATA_SIZE + 2);

class AguLegacyCodec {
public:
    // --- Command Encoders ---

    static size_t encodePumpOn(uint8_t nodeId, uint8_t* outBuf, size_t outSize);
    static size_t encodePumpOff(uint8_t nodeId, uint8_t* outBuf, size_t outSize);
    static size_t encodeGetPumpState(uint8_t nodeId, uint8_t* outBuf, size_t outSize);
    static size_t encodePing(uint8_t value, uint8_t nodeId, uint8_t* outBuf, size_t outSize);
    static size_t encodeReadEeprom(uint16_t addr, uint8_t* outBuf, size_t outSize);
    static size_t encodeWriteEeprom(uint16_t addr, uint8_t value, uint8_t* outBuf, size_t outSize);
    /**
     * @brief Encode a READ_RAM_BURST (0x0E) command frame.
     *
     * Builds the SendComCRC16 frame
     * [length, 0x0E, addr_lo, addr_hi, count, nodeId, crc_lo, crc_hi].
     * The length is payloadLen + 2 and the CRC covers length plus payload.
     *
     * The RAM address is transmitted little-endian (`addr_lo` then `addr_hi`),
     * unlike the big-endian `READ_EEPROM`/`WRITE_EEPROM` encoders.
     *
     * @param[in] nodeId   Legacy node address (1..4 in production). Must be
     *                     supplied explicitly by the caller; never hard-coded
     *                     as 0x01 in production call sites (S1-CODEC-02).
     * @param[in] addr     Little-endian RAM base address.
     * @param[in] count    Number of bytes to read. Must equal BURST_DATA_SIZE
     *                     (the deployed decoder is fixed at one 8-byte block).
     * @param[out] outBuf  Destination buffer for the encoded frame.
     * @param[in] outSize  Capacity of `outBuf` in bytes.
     * @return Encoded frame length (8) on success, or 0 when `count` is not
     *         BURST_DATA_SIZE, `outBuf` is null, or `outSize` is too small.
     */
    static size_t encodeReadRamBurst(uint8_t nodeId, uint16_t addr, uint8_t count,
                                     uint8_t* outBuf, size_t outSize);
    /**
     * @brief Encode a READ_RAM_BURST (0x0E) command frame with BURST_DATA_SIZE.
     *
     * Transition shim (Strangler Fig) that forwards to the full overload with
     * `count = BURST_DATA_SIZE`. Retained temporarily while production callers
     * migrate to the explicit-count signature, then removed.
     *
     * @param[in] nodeId   Legacy node address; never hard-coded 0x01 in
     *                     production call sites (S1-CODEC-02).
     * @param[in] addr     Little-endian RAM base address.
     * @param[out] outBuf  Destination buffer for the encoded frame.
     * @param[in] outSize  Capacity of `outBuf` in bytes.
     * @return Encoded frame length (8) on success, or 0 on invalid arguments.
     */
    static size_t encodeReadRamBurst(uint8_t nodeId, uint16_t addr,
                                     uint8_t* outBuf, size_t outSize);
    static size_t encodeWriteRam(uint8_t addr, uint8_t value, uint8_t* outBuf, size_t outSize);
    static size_t encodeGetId(uint8_t* outBuf, size_t outSize);
    static size_t encodeSetId(uint8_t newId, uint8_t* outBuf, size_t outSize);

    // --- Response / Inbound Decoders ---

   static bool isAck(uint8_t byte);
   static bool decodeFramedId(const uint8_t* inBuf, size_t inSize, uint8_t& outId);

    /**
     * @brief Decode an AGU legacy burst RAM response.
     *
     * Expects an 11-byte SendComCRC16 frame
     * [length=0x0A][8 RAM data bytes][crc_lo][crc_hi]. The CRC covers the
     * length byte and all eight data bytes.
     *
     * Fail-closed: returns false when the CRC does not match;
     * the caller must NOT update telemetry or actuator state from
     * the decoded buffer in this case.
     *
     * @param[in] inBuf    Pointer to the received frame.
     * @param[in] inSize   Must be at least BURST_RESPONSE_SIZE (11).
     * @param[out] outData8 Output buffer for the 8 RAM data bytes.
     * @return true when the length and CRC validate, false otherwise
     *         (including when inBuf is null or inSize < BURST_RESPONSE_SIZE).
     */
   static bool decodeBurstRam(const uint8_t* inBuf, size_t inSize, uint8_t* outData8);
};

} // namespace AguLegacy

#endif // AGU_LEGACY_CODEC_H
