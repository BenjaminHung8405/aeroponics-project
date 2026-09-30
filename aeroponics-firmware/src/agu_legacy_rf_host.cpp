#include "agu_legacy_rf_host.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <Arduino.h>
#include <esp_log.h>
#else
#include <chrono>
#endif

namespace {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
constexpr const char* TAG = "AGU_LEGACY_RF";
#endif

uint32_t nowMs() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    return millis();
#else
    static const auto start = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count());
#endif
}

#if defined(ESP_PLATFORM) || defined(ARDUINO)
/// Hex-dump the entire encoded frame, not just the first four bytes. AGU
/// legacy frames carry a trailing CRC16-Modbus trailer (PUMP frames are 5
/// bytes, PING is 6, READ_RAM_BURST is 8), so a fixed four-byte dump hides
/// the CRC and the payload-size difference between PUMP and PING during triage.
void hexDump(const uint8_t* data, size_t len, char* out, size_t cap) {
    if (data == nullptr || out == nullptr || cap == 0) return;
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 4 < cap; ++i) {
        pos += snprintf(out + pos, cap - pos, "%s%02X", i ? " " : "", data[i]);
    }
}
#endif
}

uint8_t AguLegacyRfHost::expectedResponse(AguRfCommand command) {
    if (command == AguRfCommand::PING) return AguLegacy::PING_DEFAULT_VAL;
    if (command == AguRfCommand::GET_PUMP_STATE) return 0x00; // Checked for 0 or 1 in transact
    return AguLegacy::ACK_BYTE;
}

size_t AguLegacyRfHost::encode(uint8_t node_id, AguRfCommand command, uint8_t* buffer, size_t size) {
    switch (command) {
        case AguRfCommand::PING:
            return AguLegacy::AguLegacyCodec::encodePing(AguLegacy::PING_DEFAULT_VAL, node_id, buffer, size);
        case AguRfCommand::PUMP_ON:
            return AguLegacy::AguLegacyCodec::encodePumpOn(node_id, buffer, size);
        case AguRfCommand::PUMP_OFF:
            return AguLegacy::AguLegacyCodec::encodePumpOff(node_id, buffer, size);
        case AguRfCommand::GET_PUMP_STATE:
            return AguLegacy::AguLegacyCodec::encodeGetPumpState(node_id, buffer, size);
        case AguRfCommand::READ_RAM_BURST:
            // READ_RAM_BURST is handled directly by readRamBurst() which builds
            // the full frame with explicit count. This case exists so the switch
            // is exhaustive; callers should use readRamBurst() rather than transact().
            return 0;
    }
    return 0;
}

void AguLegacyRfHost::guardDelay(uint32_t delay_ms) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    delay(delay_ms);
#else
    (void)delay_ms;
#endif
}

AguRfTransactionResult AguLegacyRfHost::transact(uint8_t node_id, AguRfCommand command) {
    AguRfTransactionResult result{};
    result.node_id = node_id;
    const bool is_group = isValidRfGroupAddress(node_id);
    if (!isValidTargetId(node_id)) {
        result.result = AguRfResult::INVALID_NODE_ID;
        return result;
    }
    if (is_group && command != AguRfCommand::PUMP_ON && command != AguRfCommand::PUMP_OFF) {
        result.result = AguRfResult::INVALID_NODE_ID;
        return result;
    }
    if (transport_ == nullptr) {
        result.result = AguRfResult::UART_NOT_READY;
        return result;
    }

    uint8_t frame[AguLegacy::MAX_CMD_SIZE]{};
    const size_t frame_size = encode(node_id, command, frame, sizeof(frame));
    if (frame_size == 0) {
        result.result = AguRfResult::TX_ERROR;
        return result;
    }

    const uint8_t max_attempts = is_group ? 1 : AGU_LEGACY_MAX_ATTEMPTS;
    for (uint8_t attempt = 1; attempt <= max_attempts; ++attempt) {
        result.attempts = attempt;
        transport_->flush();
        // HardwareSerial::flush() waits for TX completion; it does not clear
        // RX. Drain stale/noise bytes before each request so a leftover 0x00
        // cannot be mistaken for the node response.
        uint8_t discarded[16] = {};
        while (transport_->available() > 0) {
            (void)transport_->receive(discarded, sizeof(discarded));
        }
        const uint32_t start = nowMs();
        bool saw_unexpected = false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        char frame_hex[AguLegacy::MAX_CMD_SIZE * 3 + 1] = {};
        hexDump(frame, frame_size, frame_hex, sizeof(frame_hex));
        if (command == AguRfCommand::PUMP_ON || command == AguRfCommand::PUMP_OFF) {
            const uint16_t crc16 = (frame_size >= 2)
                ? static_cast<uint16_t>(frame[frame_size - 2] | (frame[frame_size - 1] << 8))
                : 0;
            ESP_LOGI(TAG, "================================================================================");
            ESP_LOGI(TAG, ">>> [PUMP %s] Target: %s %u | Frame (%u B): [%s] | CRC16: 0x%04X (Lo: 0x%02X, Hi: 0x%02X)",
                     command == AguRfCommand::PUMP_ON ? "ON" : "OFF",
                     is_group ? "Group" : "Node",
                     node_id,
                     static_cast<unsigned>(frame_size),
                     frame_hex,
                     crc16,
                     frame_size >= 2 ? frame[frame_size - 2] : 0,
                     frame_size >= 2 ? frame[frame_size - 1] : 0);
            ESP_LOGI(TAG, "================================================================================");
        } else {
            ESP_LOGD(TAG, "TX %s=%u command=%s attempt=%u/%u len=%u frame=%s",
                     is_group ? "group" : "node",
                     node_id,
                     command == AguRfCommand::PING ? "PING" : "GET_PUMP_STATE",
                     attempt, max_attempts,
                     static_cast<unsigned>(frame_size), frame_hex);
        }
#endif
        if (transport_->send(frame, frame_size) != frame_size) {
            result.result = AguRfResult::TX_ERROR;
            result.rtt_ms = nowMs() - start;
        } else if (is_group) {
            // Group broadcast does not mandate individual node ACK over shared RF medium.
            // Return immediately to avoid blocking the caller.
            result.result = AguRfResult::ACKED;
            result.response_byte = AguLegacy::ACK_BYTE;
            result.rtt_ms = nowMs() - start;
            return result;
        } else {
            const uint32_t wait_timeout = AGU_LEGACY_ACK_TIMEOUT_MS;
            while (nowMs() - start < wait_timeout) {
                if (transport_->available() > 0) {
                    uint8_t response = 0;
                    if (transport_->receive(&response, 1) == 1) {
                        result.response_byte = response;
                        result.rtt_ms = nowMs() - start;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
                        if (command == AguRfCommand::PUMP_ON || command == AguRfCommand::PUMP_OFF) {
                            ESP_LOGI(TAG, "<<< [PUMP %s RESP] %s %u | Resp: 0x%02X (%s) | RTT: %u ms",
                                     command == AguRfCommand::PUMP_ON ? "ON" : "OFF",
                                     is_group ? "Group" : "Node",
                                     node_id, response,
                                     (response == expectedResponse(command)) ? "ACK_OK" : "UNEXPECTED",
                                     static_cast<unsigned>(result.rtt_ms));
                        } else {
                            ESP_LOGD(TAG, "RX %s=%u command=%u response=0x%02X expected=0x%02X rtt=%u ms",
                                     is_group ? "group" : "node",
                                     node_id, static_cast<unsigned>(command), response,
                                     expectedResponse(command), static_cast<unsigned>(result.rtt_ms));
                        }
#endif
                        if (command == AguRfCommand::GET_PUMP_STATE) {
                            if (response == 0 || response == 1) {
                                result.result = AguRfResult::ACKED;
                                return result;
                            }
                        } else if (response == expectedResponse(command)) {
                            result.result = AguRfResult::ACKED;
                            return result;
                        }
                        saw_unexpected = true;
                        break;
                    }
                }
                guardDelay(1);
            }
            result.rtt_ms = nowMs() - start;
            result.result = saw_unexpected ? AguRfResult::UNEXPECTED_RESPONSE : AguRfResult::TIMEOUT;
        }

        if (attempt < max_attempts) {
            transport_->flush();
            guardDelay(AGU_LEGACY_RETRY_GUARD_MS);
        }
    }
    return result;
}

AguRfTransactionResult AguLegacyRfHost::pingNode(uint8_t node_id) {
    return transact(node_id, AguRfCommand::PING);
}

AguRfTransactionResult AguLegacyRfHost::setPump(uint8_t node_id, bool on) {
    return transact(node_id, on ? AguRfCommand::PUMP_ON : AguRfCommand::PUMP_OFF);
}

AguRfTransactionResult AguLegacyRfHost::setGroupPump(uint8_t group_rf_id, bool on) {
    return transact(group_rf_id, on ? AguRfCommand::PUMP_ON : AguRfCommand::PUMP_OFF);
}

AguRfTransactionResult AguLegacyRfHost::getPumpState(uint8_t node_id) {
    return transact(node_id, AguRfCommand::GET_PUMP_STATE);
}

AguRfTransactionResult AguLegacyRfHost::readRamBurst(
    uint8_t node_id, uint16_t addr, uint8_t* out_data8) {
    AguRfTransactionResult result{};
    result.node_id = node_id;
    if (!isValidNodeId(node_id)) {
        result.result = AguRfResult::INVALID_NODE_ID;
        return result;
    }
    if (out_data8 == nullptr) {
        result.result = AguRfResult::TX_ERROR;
        return result;
    }
    if (transport_ == nullptr) {
        result.result = AguRfResult::UART_NOT_READY;
        return result;
    }

    // Encode the READ_RAM_BURST frame with explicit count=8.
    uint8_t frame[AguLegacy::MAX_CMD_SIZE]{};
    const size_t frame_size = AguLegacy::AguLegacyCodec::encodeReadRamBurst(
        node_id, addr, static_cast<uint8_t>(AguLegacy::BURST_DATA_SIZE),
        frame, sizeof(frame));
    if (frame_size == 0) {
        result.result = AguRfResult::TX_ERROR;
        return result;
    }

    for (uint8_t attempt = 1; attempt <= AGU_LEGACY_MAX_ATTEMPTS; ++attempt) {
        result.attempts = attempt;
        transport_->flush();
        // Drain stale/noise RX bytes before each request so a leftover byte
        // cannot be mistaken for the response.
        uint8_t discarded[16] = {};
        while (transport_->available() > 0) {
            (void)transport_->receive(discarded, sizeof(discarded));
        }
        const uint32_t start = nowMs();
        bool saw_unexpected = false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        char burst_hex[AguLegacy::MAX_CMD_SIZE * 3 + 1] = {};
        hexDump(frame, frame_size, burst_hex, sizeof(burst_hex));
        ESP_LOGD(TAG, "TX node=%u READ_RAM_BURST attempt=%u/%u len=%u frame=%s",
                 node_id, attempt, AGU_LEGACY_MAX_ATTEMPTS,
                 static_cast<unsigned>(frame_size), burst_hex);
#endif
        if (transport_->send(frame, frame_size) != frame_size) {
            result.result = AguRfResult::TX_ERROR;
            result.rtt_ms = nowMs() - start;
        } else {
            // Wait for [length][8 RAM data][CRC16-LE].
            uint8_t resp[AGU_LEGACY_BURST_RESPONSE_SIZE]{};
            size_t resp_len = 0;
            while (nowMs() - start < AGU_LEGACY_ACK_TIMEOUT_MS) {
                if (resp_len >= sizeof(resp)) break;
                if (transport_->available() > 0) {
                    resp_len += transport_->receive(resp + resp_len,
                                                    sizeof(resp) - resp_len);
                } else {
                    guardDelay(1);
                }
            }
            result.rtt_ms = nowMs() - start;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
            ESP_LOGD(TAG, "RX node=%u READ_RAM_BURST resp_len=%u rtt=%u ms",
                     node_id, static_cast<unsigned>(resp_len),
                     static_cast<unsigned>(result.rtt_ms));
#endif
            if (resp_len < sizeof(resp)) {
                result.response_byte = resp_len > 0 ? resp[0] : 0;
                result.result = AguRfResult::TIMEOUT;
            } else if (AguLegacy::AguLegacyCodec::decodeBurstRam(resp, sizeof(resp), out_data8)) {
                result.result = AguRfResult::ACKED;
                return result;
            } else {
                result.response_byte = resp[0];
                result.result = AguRfResult::UNEXPECTED_RESPONSE;
            }
        }

        if (attempt < AGU_LEGACY_MAX_ATTEMPTS) {
            transport_->flush();
            guardDelay(AGU_LEGACY_RETRY_GUARD_MS);
        }
    }
    return result;
}
