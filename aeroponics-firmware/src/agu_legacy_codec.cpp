#include "agu_legacy_codec.h"
#include "core/Crc16Modbus.h"
#include <cstring>

namespace AguLegacy {

namespace {

/**
 * @brief Encapsulate raw command bytes in Delphi TSCI.SendComCRC16 envelope:
 * [ (payloadLen + 2) ] [ payload[0] ... payload[N-1] ] [ crc_lo ] [ crc_hi ].
 * CRC covers the length byte and payload.
 */
/// Legacy WRITE_RAM payload bytes — per RF wire contract §5.2 these are
/// protocol-fixed fields; named constants prevent magic-number drift.
constexpr uint8_t WRITE_RAM_DUMMY_HI = 0x00;
constexpr uint8_t WRITE_RAM_ENABLE_FLAG = 0x01;

size_t formatSendComPacket(const uint8_t* payload, size_t payloadLen, uint8_t* outBuf, size_t outSize) {
    if (!payload || payloadLen == 0 || payloadLen > UINT8_MAX - 2 ||
        !outBuf || outSize < (payloadLen + 3)) {
        return 0;
    }
    outBuf[0] = static_cast<uint8_t>(payloadLen + 2);
    for (size_t i = 0; i < payloadLen; ++i) {
        outBuf[1 + i] = payload[i];
    }
    return appendCrc16Modbus(outBuf, payloadLen + 1, outSize);
}

} // anonymous namespace

size_t AguLegacyCodec::encodePumpOn(uint8_t nodeId, uint8_t* outBuf, size_t outSize) {
    const uint8_t payload[2] = {static_cast<uint8_t>(Opcode::PUMP_ON), nodeId};
    return formatSendComPacket(payload, sizeof(payload), outBuf, outSize);
}

size_t AguLegacyCodec::encodePumpOff(uint8_t nodeId, uint8_t* outBuf, size_t outSize) {
    const uint8_t payload[2] = {static_cast<uint8_t>(Opcode::PUMP_OFF), nodeId};
    return formatSendComPacket(payload, sizeof(payload), outBuf, outSize);
}

size_t AguLegacyCodec::encodeGetPumpState(uint8_t nodeId, uint8_t* outBuf, size_t outSize) {
    const uint8_t payload[2] = {static_cast<uint8_t>(Opcode::GET_PUMP_STATE), nodeId};
    return formatSendComPacket(payload, sizeof(payload), outBuf, outSize);
}

size_t AguLegacyCodec::encodePing(uint8_t value, uint8_t nodeId, uint8_t* outBuf, size_t outSize) {
    const uint8_t payload[3] = {static_cast<uint8_t>(Opcode::PING), value, nodeId};
    return formatSendComPacket(payload, sizeof(payload), outBuf, outSize);
}

size_t AguLegacyCodec::encodeReadEeprom(uint16_t addr, uint8_t* outBuf, size_t outSize) {
    const uint8_t payload[3] = {
        static_cast<uint8_t>(Opcode::READ_EEPROM),
        static_cast<uint8_t>((addr >> 8) & 0xFF),
        static_cast<uint8_t>(addr & 0xFF)
    };
    return formatSendComPacket(payload, sizeof(payload), outBuf, outSize);
}

size_t AguLegacyCodec::encodeWriteEeprom(uint16_t addr, uint8_t value, uint8_t* outBuf, size_t outSize) {
    const uint8_t payload[4] = {
        static_cast<uint8_t>(Opcode::WRITE_EEPROM),
        static_cast<uint8_t>((addr >> 8) & 0xFF),
        static_cast<uint8_t>(addr & 0xFF),
        value
    };
    return formatSendComPacket(payload, sizeof(payload), outBuf, outSize);
}

size_t AguLegacyCodec::encodeReadRamBurst(uint8_t nodeId, uint16_t addr, uint8_t count,
                                          uint8_t* outBuf, size_t outSize) {
    // The deployed response decoder is fixed at one 8-byte RAM block.
    if (count != BURST_DATA_SIZE) return 0;
    const uint8_t payload[5] = {
        static_cast<uint8_t>(Opcode::READ_RAM_BURST),
        static_cast<uint8_t>(addr & 0xFF),
        static_cast<uint8_t>((addr >> 8) & 0xFF),
        count,
        nodeId
    };
    return formatSendComPacket(payload, sizeof(payload), outBuf, outSize);
}

size_t AguLegacyCodec::encodeReadRamBurst(uint8_t nodeId, uint16_t addr,
                                          uint8_t* outBuf, size_t outSize) {
    return encodeReadRamBurst(nodeId, addr, BURST_DATA_SIZE, outBuf, outSize);
}

size_t AguLegacyCodec::encodeWriteRam(uint8_t addr, uint8_t value, uint8_t* outBuf, size_t outSize) {
    const uint8_t payload[5] = {
        static_cast<uint8_t>(Opcode::WRITE_RAM),
        addr,
        WRITE_RAM_DUMMY_HI,
        value,
        WRITE_RAM_ENABLE_FLAG
    };
    return formatSendComPacket(payload, sizeof(payload), outBuf, outSize);
}

size_t AguLegacyCodec::encodeGetId(uint8_t* outBuf, size_t outSize) {
    const uint8_t payload[2] = {static_cast<uint8_t>(Opcode::DEVICE_ID), 0x00};
    return formatSendComPacket(payload, sizeof(payload), outBuf, outSize);
}

size_t AguLegacyCodec::encodeSetId(uint8_t newId, uint8_t* outBuf, size_t outSize) {
    const uint8_t payload[3] = {static_cast<uint8_t>(Opcode::DEVICE_ID), 0x01, newId};
    return formatSendComPacket(payload, sizeof(payload), outBuf, outSize);
}

bool AguLegacyCodec::isAck(uint8_t byte) {
    return byte == ACK_BYTE;
}

bool AguLegacyCodec::decodeFramedId(const uint8_t* inBuf, size_t inSize, uint8_t& outId) {
    if (!inBuf || inSize < 3) return false;
    for (size_t i = 0; i + 2 < inSize; ++i) {
        if (inBuf[i] == SYNC_BYTE_1 && inBuf[i + 1] == SYNC_BYTE_2) {
            outId = inBuf[i + 2];
            return true;
        }
    }
    return false;
}

bool AguLegacyCodec::decodeBurstRam(const uint8_t* inBuf, size_t inSize, uint8_t* outData8) {
    if (!inBuf || inSize < BURST_RESPONSE_SIZE || !outData8) return false;
    if (inBuf[0] != BURST_RESPONSE_LENGTH ||
        !verifyCrc16Modbus(inBuf, BURST_RESPONSE_SIZE)) return false;
    std::memcpy(outData8, inBuf + 1, BURST_DATA_SIZE);
    return true;
}

} // namespace AguLegacy
