#pragma once

#include <stdint.h>
#include <stddef.h>
#include "agu_legacy_codec.h"
#include "core/IRfTransport.h"
#include "config.h"

#include <atomic>
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#else
#include <mutex>
#include <chrono>
#endif

enum class AguRfCommand : uint8_t {
    PING,
    PUMP_ON,
    PUMP_OFF,
    GET_PUMP_STATE,
    READ_RAM_BURST,
};

enum class RfTrafficClass : uint8_t {
    PUMP_CRITICAL,
    PUMP_NORMAL,
    PING,
    TELEMETRY,
    DIAGNOSTIC,
};

enum class AguRfResult : uint8_t {
    ACKED,
    TIMEOUT,
    UNEXPECTED_RESPONSE,
    TX_ERROR,
    INVALID_NODE_ID,
    UART_NOT_READY,
    BUS_BUSY,
    SILENCE_DEFERRED,
};

struct AguRfTransactionResult {
    AguRfResult result = AguRfResult::UART_NOT_READY;
    uint8_t node_id = 0;
    uint8_t response_byte = 0;
    uint8_t attempts = 0;
    uint8_t burst_frames_sent = 0;
    uint32_t rtt_ms = 0;
};

constexpr size_t AGU_LEGACY_BURST_RESPONSE_SIZE = AguLegacy::BURST_RESPONSE_SIZE;

class AguLegacyRfHost;

/** RAII Bus Guard for maintenance, diagnostics, and external transactions */
class RfBusGuard {
public:
    explicit RfBusGuard(AguLegacyRfHost* host, RfTrafficClass traffic = RfTrafficClass::DIAGNOSTIC);
    ~RfBusGuard();
    bool isLocked() const { return locked_; }
    void unlock();

    RfBusGuard(const RfBusGuard&) = delete;
    RfBusGuard& operator=(const RfBusGuard&) = delete;
    RfBusGuard(RfBusGuard&& other) noexcept;
    RfBusGuard& operator=(RfBusGuard&& other) noexcept;

private:
    AguLegacyRfHost* host_ = nullptr;
    bool locked_ = false;
};

/** Serialized, single-owner transaction engine for the AGU legacy SCI bus. */
class AguLegacyRfHost {
public:
    explicit AguLegacyRfHost(IRfTransport* transport);
    ~AguLegacyRfHost();

    AguRfTransactionResult pingNode(uint8_t node_id);
    AguRfTransactionResult setPump(uint8_t node_id, bool on);
    AguRfTransactionResult setGroupPump(uint8_t group_rf_id, bool on);
    AguRfTransactionResult getPumpState(uint8_t node_id);

    /**
     * @brief Staggered fan-out to multiple unicast nodes with 300 ms inter-node spacing.
     */
    void fanoutPump(const uint8_t* node_ids, size_t count, bool turn_on,
                    AguRfTransactionResult* results_out = nullptr);

    /**
     * @brief Read an 8-byte RAM burst block from a node.
     *
     * Performs a READ_RAM_BURST (0x0E) transaction with validation,
     * encoding, RX flush, send, and 11-byte response reception. Decodes
     * the response through decodeBurstRam (fail-closed). Retries up to
     * AGU_LEGACY_MAX_ATTEMPTS with the identical frame (S1-CODEC-02).
     *
     * @param[in] node_id   Legacy node address (1..15).
     * @param[in] addr      Little-endian RAM base address.
     * @param[out] out_data8 Destination buffer for 8 RAM bytes (must not be null).
     * @return AguRfTransactionResult with result = ACKED / TIMEOUT /
     *         UNEXPECTED_RESPONSE / TX_ERROR / INVALID_NODE_ID / UART_NOT_READY.
     */
    AguRfTransactionResult readRamBurst(uint8_t node_id, uint16_t addr, uint8_t* out_data8);

    void setRadioSilenceWindow(uint32_t start_ms, uint32_t end_ms);
    bool isRadioSilenceActive(uint32_t now_ms) const;
    bool pumpPending() const;

    bool acquire(RfTrafficClass traffic);
    void release();

    using NowMsFn = uint32_t (*)();
    using DelayMsFn = void (*)(uint32_t);

    void setTimeProvider(NowMsFn now_fn, DelayMsFn delay_fn);
    void resetTimeProvider();

    static bool isValidNodeId(uint8_t node_id) { return isAguLegacyNodeId(node_id); }
    static bool isValidTargetId(uint8_t id) {
        return isAguLegacyNodeId(id) || isValidRfGroupAddress(id);
    }

private:
    AguRfTransactionResult transact(uint8_t node_id, AguRfCommand command);
    static uint8_t expectedResponse(AguRfCommand command);
    static size_t encode(uint8_t node_id, AguRfCommand command, uint8_t* buffer, size_t size);
    void guardDelay(uint32_t delay_ms) const;
    AguRfTransactionResult transactLocked(uint8_t node_id, AguRfCommand command,
                                          RfTrafficClass traffic);
    bool silenceBlocks(RfTrafficClass traffic, uint32_t now_ms) const;
    static bool timeInWindow(uint32_t now_ms, uint32_t start_ms, uint32_t end_ms);
    uint32_t nowMs() const;

    IRfTransport* transport_;
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
    void* mutex_;
#else
    mutable std::timed_mutex mutex_;
#endif
    std::atomic<bool> pump_pending_{false};
    volatile uint32_t silence_start_ms_ = 0;
    volatile uint32_t silence_end_ms_ = 0;
    NowMsFn now_fn_ = nullptr;
    DelayMsFn delay_fn_ = nullptr;
};
