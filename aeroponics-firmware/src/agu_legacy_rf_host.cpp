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
}

uint8_t AguLegacyRfHost::expectedResponse(AguRfCommand command) {
    return command == AguRfCommand::PING ? AguLegacy::PING_DEFAULT_VAL : AguLegacy::ACK_BYTE;
}

size_t AguLegacyRfHost::encode(uint8_t node_id, AguRfCommand command, uint8_t* buffer, size_t size) {
    switch (command) {
        case AguRfCommand::PING:
            return AguLegacy::AguLegacyCodec::encodePing(AguLegacy::PING_DEFAULT_VAL, node_id, buffer, size);
        case AguRfCommand::PUMP_ON:
            return AguLegacy::AguLegacyCodec::encodePumpOn(node_id, buffer, size);
        case AguRfCommand::PUMP_OFF:
            return AguLegacy::AguLegacyCodec::encodePumpOff(node_id, buffer, size);
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
    if (!isValidNodeId(node_id)) {
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

    for (uint8_t attempt = 1; attempt <= AGU_LEGACY_MAX_ATTEMPTS; ++attempt) {
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
        ESP_LOGI(TAG, "TX node=%u command=%s attempt=%u/%u frame=%02X %02X %02X %02X",
                 node_id,
                 command == AguRfCommand::PING ? "PING" :
                 command == AguRfCommand::PUMP_ON ? "PUMP_ON" : "PUMP_OFF",
                 attempt, AGU_LEGACY_MAX_ATTEMPTS,
                 frame_size > 0 ? frame[0] : 0,
                 frame_size > 1 ? frame[1] : 0,
                 frame_size > 2 ? frame[2] : 0,
                 frame_size > 3 ? frame[3] : 0);
#endif
        if (transport_->send(frame, frame_size) != frame_size) {
            result.result = AguRfResult::TX_ERROR;
            result.rtt_ms = nowMs() - start;
        } else {
            while (nowMs() - start < AGU_LEGACY_ACK_TIMEOUT_MS) {
                if (transport_->available() > 0) {
                    uint8_t response = 0;
                    if (transport_->receive(&response, 1) == 1) {
                        result.response_byte = response;
                        result.rtt_ms = nowMs() - start;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
                        ESP_LOGI(TAG, "RX node=%u command=%u response=0x%02X expected=0x%02X rtt=%u ms",
                                 node_id, static_cast<unsigned>(command), response,
                                 expectedResponse(command), static_cast<unsigned>(result.rtt_ms));
#endif
                        if (response == expectedResponse(command)) {
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

        if (attempt < AGU_LEGACY_MAX_ATTEMPTS) {
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
        ESP_LOGI(TAG, "TX node=%u READ_RAM_BURST attempt=%u/%u frame=%02X %02X %02X %02X",
                 node_id, attempt, AGU_LEGACY_MAX_ATTEMPTS,
                 frame_size > 0 ? frame[0] : 0,
                 frame_size > 1 ? frame[1] : 0,
                 frame_size > 2 ? frame[2] : 0,
                 frame_size > 3 ? frame[3] : 0);
#endif
        if (transport_->send(frame, frame_size) != frame_size) {
            result.result = AguRfResult::TX_ERROR;
            result.rtt_ms = nowMs() - start;
        } else {
            // Wait for the 9-byte response (8 RAM data + 1 zero-sum checksum).
            uint8_t resp[AguLegacy::BURST_DATA_SIZE + 1]{};
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
            ESP_LOGI(TAG, "RX node=%u READ_RAM_BURST resp_len=%u rtt=%u ms",
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
