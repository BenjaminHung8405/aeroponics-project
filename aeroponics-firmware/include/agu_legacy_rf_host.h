#pragma once

#include <stdint.h>
#include "agu_legacy_codec.h"
#include "core/IRfTransport.h"
#include "config.h"

enum class AguRfCommand : uint8_t {
    PING,
    PUMP_ON,
    PUMP_OFF,
    GET_PUMP_STATE,
    READ_RAM_BURST,
};

enum class AguRfResult : uint8_t {
    ACKED,
    TIMEOUT,
    UNEXPECTED_RESPONSE,
    TX_ERROR,
    INVALID_NODE_ID,
    UART_NOT_READY,
};

struct AguRfTransactionResult {
    AguRfResult result = AguRfResult::UART_NOT_READY;
    uint8_t node_id = 0;
    uint8_t response_byte = 0;
    uint8_t attempts = 0;
    uint32_t rtt_ms = 0;
};

constexpr size_t AGU_LEGACY_BURST_RESPONSE_SIZE = AguLegacy::BURST_RESPONSE_SIZE;

/** Serialized, single-owner transaction engine for the AGU legacy SCI bus. */
class AguLegacyRfHost {
public:
    explicit AguLegacyRfHost(IRfTransport* transport) : transport_(transport) {}

    AguRfTransactionResult pingNode(uint8_t node_id);
    AguRfTransactionResult setPump(uint8_t node_id, bool on);
    AguRfTransactionResult setGroupPump(uint8_t group_rf_id, bool on);
    AguRfTransactionResult getPumpState(uint8_t node_id);

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

    static bool isValidNodeId(uint8_t node_id) { return isAguLegacyNodeId(node_id); }
    static bool isValidTargetId(uint8_t id) {
        return isAguLegacyNodeId(id) || isValidRfGroupAddress(id);
    }

private:
    AguRfTransactionResult transact(uint8_t node_id, AguRfCommand command);
    static uint8_t expectedResponse(AguRfCommand command);
    static size_t encode(uint8_t node_id, AguRfCommand command, uint8_t* buffer, size_t size);
    static void guardDelay(uint32_t delay_ms);

    IRfTransport* transport_;
};
