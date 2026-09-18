#include "agu_legacy_codec.h"
#include <cstring>

namespace AguLegacy {

size_t AguLegacyCodec::encodePumpOn(uint8_t nodeId, uint8_t* outBuf, size_t outSize) {
    if (!outBuf || outSize < 2) return 0;
    outBuf[0] = static_cast<uint8_t>(Opcode::PUMP_ON);
    outBuf[1] = nodeId;
    return 2;
}

size_t AguLegacyCodec::encodePumpOff(uint8_t nodeId, uint8_t* outBuf, size_t outSize) {
    if (!outBuf || outSize < 2) return 0;
    outBuf[0] = static_cast<uint8_t>(Opcode::PUMP_OFF);
    outBuf[1] = nodeId;
    return 2;
}

size_t AguLegacyCodec::encodePing(uint8_t value, uint8_t nodeId, uint8_t* outBuf, size_t outSize) {
    if (!outBuf || outSize < 3) return 0;
    outBuf[0] = static_cast<uint8_t>(Opcode::PING);
    outBuf[1] = value;
    outBuf[2] = nodeId;
    return 3;
}

size_t AguLegacyCodec::encodeReadEeprom(uint16_t addr, uint8_t* outBuf, size_t outSize) {
    if (!outBuf || outSize < 3) return 0;
    outBuf[0] = static_cast<uint8_t>(Opcode::READ_EEPROM);
    outBuf[1] = static_cast<uint8_t>((addr >> 8) & 0xFF); // hi(addr)
    outBuf[2] = static_cast<uint8_t>(addr & 0xFF);        // lo(addr)
    return 3;
}

size_t AguLegacyCodec::encodeWriteEeprom(uint16_t addr, uint8_t value, uint8_t* outBuf, size_t outSize) {
    if (!outBuf || outSize < 4) return 0;
    outBuf[0] = static_cast<uint8_t>(Opcode::WRITE_EEPROM);
    outBuf[1] = static_cast<uint8_t>((addr >> 8) & 0xFF); // hi(addr)
    outBuf[2] = static_cast<uint8_t>(addr & 0xFF);        // lo(addr)
    outBuf[3] = value;
    return 4;
}

size_t AguLegacyCodec::encodeReadRamBurst(uint16_t addr, uint8_t* outBuf, size_t outSize) {
    if (!outBuf || outSize < 5) return 0;
    outBuf[0] = static_cast<uint8_t>(Opcode::READ_RAM_BURST);
    outBuf[1] = static_cast<uint8_t>(addr & 0xFF);        // lo(addr)
    outBuf[2] = static_cast<uint8_t>((addr >> 8) & 0xFF); // hi(addr)
    outBuf[3] = 0x08;                                      // 8 bytes
    outBuf[4] = 0x01;
    return 5;
}

size_t AguLegacyCodec::encodeWriteRam(uint8_t addr, uint8_t value, uint8_t* outBuf, size_t outSize) {
    if (!outBuf || outSize < 5) return 0;
    outBuf[0] = static_cast<uint8_t>(Opcode::WRITE_RAM);
    outBuf[1] = addr;
    outBuf[2] = 0x00;
    outBuf[3] = value;
    outBuf[4] = 0x01;
    return 5;
}

size_t AguLegacyCodec::encodeGetId(uint8_t* outBuf, size_t outSize) {
    if (!outBuf || outSize < 2) return 0;
    outBuf[0] = static_cast<uint8_t>(Opcode::DEVICE_ID);
    outBuf[1] = 0x00;
    return 2;
}

size_t AguLegacyCodec::encodeSetId(uint8_t newId, uint8_t* outBuf, size_t outSize) {
    if (!outBuf || outSize < 3) return 0;
    outBuf[0] = static_cast<uint8_t>(Opcode::DEVICE_ID);
    outBuf[1] = 0x01;
    outBuf[2] = newId;
    return 3;
}

bool AguLegacyCodec::isAck(uint8_t byte) {
    return byte == ACK_BYTE;
}

bool AguLegacyCodec::decodeFramedId(const uint8_t* inBuf, size_t inSize, uint8_t& outId) {
    if (!inBuf || inSize < 3) return false;
    // Scan for SYNC_BYTE_1 (0xFF) followed by SYNC_BYTE_2 (0x5A)
    for (size_t i = 0; i + 2 < inSize; ++i) {
        if (inBuf[i] == SYNC_BYTE_1 && inBuf[i + 1] == SYNC_BYTE_2) {
            outId = inBuf[i + 2];
            return true;
        }
    }
    return false;
}

bool AguLegacyCodec::decodeBurstRam(const uint8_t* inBuf, size_t inSize, uint8_t* outData8) {
    if (!inBuf || inSize < (BURST_DATA_SIZE + 1) || !outData8) return false;
    // Checksum byte is at index 8
    const uint8_t chks = inBuf[BURST_DATA_SIZE];
    if (!verifyZeroSumChecksum(inBuf, BURST_DATA_SIZE, chks)) {
        return false;
    }
    std::memcpy(outData8, inBuf, BURST_DATA_SIZE);
    return true;
}

} // namespace AguLegacy
