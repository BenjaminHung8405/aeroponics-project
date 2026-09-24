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
    READ_EEPROM     = 0x08,
    WRITE_EEPROM    = 0x09,
    DEVICE_ID       = 0x0A,
    READ_RAM_BURST  = 0x0E,
};

// Maximum command and response buffer sizes
constexpr size_t MAX_CMD_SIZE = 16;
constexpr size_t BURST_DATA_SIZE = 8;

/**
 * @brief Compute the two's-complement zero-sum checksum byte.
 *
 * Returns the single byte that satisfies the S1-CODEC-01 invariant
 * (sum(data[0..len-1]) + checksum) & 0xFF == 0.
 *
 * @param[in] data  Pointer to the bytes to checksum; must be non-null.
 * @param[in] len   Number of bytes to include in the sum.
 * @return Checksum byte, or 0 when `data` is null or `len` is 0.
 */
inline uint8_t calculateZeroSumChecksum(const uint8_t* data, size_t len) {
    if (!data || len == 0) return 0;
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum = static_cast<uint8_t>(sum + data[i]);
    }
    return static_cast<uint8_t>((~sum + 1) & 0xFF);
}

/**
 * @brief Verify a two's-complement zero-sum checksum byte.
 *
 * Validates that (sum(data[0..len-1]) + checksum) & 0xFF == 0 (S1-CODEC-01).
 *
 * @param[in] data      Pointer to the bytes to verify; must be non-null.
 * @param[in] len       Number of bytes the checksum covers.
 * @param[in] checksum  Checksum byte to validate against the data.
 * @return true when the zero-sum invariant holds, false otherwise
 *         (including a null `data` pointer).
 */
inline bool verifyZeroSumChecksum(const uint8_t* data, size_t len, uint8_t checksum) {
    if (!data) return false;
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum = static_cast<uint8_t>(sum + data[i]);
    }
    return static_cast<uint8_t>(sum + checksum) == 0;
}

class AguLegacyCodec {
public:
    // --- Command Encoders ---

    static size_t encodePumpOn(uint8_t nodeId, uint8_t* outBuf, size_t outSize);
    static size_t encodePumpOff(uint8_t nodeId, uint8_t* outBuf, size_t outSize);
    static size_t encodePing(uint8_t value, uint8_t nodeId, uint8_t* outBuf, size_t outSize);
    static size_t encodeReadEeprom(uint16_t addr, uint8_t* outBuf, size_t outSize);
    static size_t encodeWriteEeprom(uint16_t addr, uint8_t value, uint8_t* outBuf, size_t outSize);
    /**
     * @brief Encode a READ_RAM_BURST (0x0E) command frame.
     *
     * Builds the 7-byte frame
     * [0x06, 0x0E, addr_lo, addr_hi, count, nodeId, checksum] where the
     * checksum is the two's-complement zero-sum byte of the first six bytes
     * (S1-CODEC-01: sum(frame) & 0xFF == 0) and no heap is allocated.
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
     * @return Encoded frame length (7) on success, or 0 when `count` is not
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
     * @return Encoded frame length (7) on success, or 0 on invalid arguments.
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
     * Expects a 9-byte frame [8 RAM data bytes][1 zero-sum checksum byte].
     * Validates that the two's-complement zero-sum checksum satisfies
     * S1-CODEC-01: sum(frame[0..6]) & 0xFF == 0.
     *
     * Fail-closed: returns false when the checksum does not match;
     * the caller must NOT update telemetry or actuator state from
     * the decoded buffer in this case.
     *
     * @param[in] inBuf    Pointer to the 9 received bytes.
     * @param[in] inSize   Must be at least BURST_DATA_SIZE + 1 (9).
     * @param[out] outData8 Output buffer for the 8 RAM data bytes.
     * @return true when checksum invariant passes, false otherwise
     *         (including when inBuf is null or inSize < 9).
     */
   static bool decodeBurstRam(const uint8_t* inBuf, size_t inSize, uint8_t* outData8);
};

} // namespace AguLegacy

#endif // AGU_LEGACY_CODEC_H
