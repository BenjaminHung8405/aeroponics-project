#pragma once

#include <stdint.h>
#include "agu_legacy_codec.h"
#include "core/IRfTransport.h"
#include "config.h"

enum class AguRfCommand : uint8_t {
    PING,
    PUMP_ON,
    PUMP_OFF,
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

/** Serialized, single-owner transaction engine for the AGU legacy SCI bus. */
class AguLegacyRfHost {
public:
    explicit AguLegacyRfHost(IRfTransport* transport) : transport_(transport) {}

    AguRfTransactionResult pingNode(uint8_t node_id);
    AguRfTransactionResult setPump(uint8_t node_id, bool on);
    static bool isValidNodeId(uint8_t node_id) { return isAguLegacyNodeId(node_id); }

private:
    AguRfTransactionResult transact(uint8_t node_id, AguRfCommand command);
    static uint8_t expectedResponse(AguRfCommand command);
    static size_t encode(uint8_t node_id, AguRfCommand command, uint8_t* buffer, size_t size);
    static void guardDelay(uint32_t delay_ms);

    IRfTransport* transport_;
};

