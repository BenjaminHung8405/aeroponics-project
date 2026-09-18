#include "agu_legacy_codec.h"
#include <cstring>

namespace AguLegacy {

namespace {

/**
 * @brief Encapsulate raw command bytes in Delphi TSCI.SendCom envelope:
 * [ (payloadLen + 1) ] [ payload[0] ... payload[N-1] ] [ checksum ]
 * Checksum ensures (sum(all_bytes) & 0xFF) == 0.
 */
size_t formatSendComPacket(const uint8_t* payload, size_t payloadLen, uint8_t* outBuf, size_t outSize) {
    if (!payload || payloadLen == 0 || !outBuf || outSize < (payloadLen + 2)) {
        return 0;
    }
    const uint8_t frameLen = static_cast<uint8_t>(payloadLen + 1);
    outBuf[0] = frameLen;
    uint8_t sum = frameLen;
    for (size_t i = 0; i < payloadLen; ++i) {
        outBuf[1 + i] = payload[i];
        sum = static_cast<uint8_t>(sum + payload[i]);
    }
    const uint8_t checksum = static_cast<uint8_t>((0x100 - sum) & 0xFF);
    outBuf[1 + payloadLen] = checksum;
    return payloadLen + 2;
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

size_t AguLegacyCodec::encodeReadRamBurst(uint16_t addr, uint8_t* outBuf, size_t outSize) {
    const uint8_t payload[5] = {
        static_cast<uint8_t>(Opcode::READ_RAM_BURST),
        static_cast<uint8_t>(addr & 0xFF),
        static_cast<uint8_t>((addr >> 8) & 0xFF),
        0x08,
        0x01
    };
    return formatSendComPacket(payload, sizeof(payload), outBuf, outSize);
}

size_t AguLegacyCodec::encodeWriteRam(uint8_t addr, uint8_t value, uint8_t* outBuf, size_t outSize) {
    const uint8_t payload[5] = {
        static_cast<uint8_t>(Opcode::WRITE_RAM),
        addr,
        0x00,
        value,
        0x01
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
    if (!inBuf || inSize < (BURST_DATA_SIZE + 1) || !outData8) return false;
    const uint8_t chks = inBuf[BURST_DATA_SIZE];
    if (!verifyZeroSumChecksum(inBuf, BURST_DATA_SIZE, chks)) {
        return false;
    }
    std::memcpy(outData8, inBuf, BURST_DATA_SIZE);
    return true;
}

} // namespace AguLegacy
