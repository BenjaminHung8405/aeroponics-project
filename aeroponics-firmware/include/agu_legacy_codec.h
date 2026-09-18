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
constexpr size_t MAX_CMD_SIZE = 8;
constexpr size_t BURST_DATA_SIZE = 8;

/**
 * @brief Calculate two's complement zero-sum checksum byte.
 * 
 * Ensures (sum(data[0..len-1]) + checksum) & 0xFF == 0.
 */
inline uint8_t calculateZeroSumChecksum(const uint8_t* data, size_t len) {
    if (!data || len == 0) return 0;
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum = static_cast<uint8_t>(sum + data[i]);
    }
    return static_cast<uint8_t>((0x100 - sum) & 0xFF);
}

/**
 * @brief Verify two's complement zero-sum checksum.
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
    static size_t encodeReadRamBurst(uint16_t addr, uint8_t* outBuf, size_t outSize);
    static size_t encodeWriteRam(uint8_t addr, uint8_t value, uint8_t* outBuf, size_t outSize);
    static size_t encodeGetId(uint8_t* outBuf, size_t outSize);
    static size_t encodeSetId(uint8_t newId, uint8_t* outBuf, size_t outSize);

    // --- Response / Inbound Decoders ---

    static bool isAck(uint8_t byte);
    static bool decodeFramedId(const uint8_t* inBuf, size_t inSize, uint8_t& outId);
    static bool decodeBurstRam(const uint8_t* inBuf, size_t inSize, uint8_t* outData8);
};

} // namespace AguLegacy

#endif // AGU_LEGACY_CODEC_H
