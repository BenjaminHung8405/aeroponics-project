#pragma once

#include <cstdint>
#include "config.h"

/** Non-secret release gate; independent security evidence is required for production RF. */
#if defined(RF_PROVISIONING_INDEPENDENT_SIGNOFF) && RF_PROVISIONING_INDEPENDENT_SIGNOFF == 1
constexpr bool RF_PROVISIONING_INDEPENDENT_SIGNOFF_PRESENT = true;
#else
constexpr bool RF_PROVISIONING_INDEPENDENT_SIGNOFF_PRESENT = false;
#endif

struct RfHardwareConfig {
    uint8_t uart_num = RF_DEFAULT_UART_NUM;
    int8_t tx_pin = RF_DEFAULT_TX_PIN;
    int8_t rx_pin = RF_DEFAULT_RX_PIN;
    uint32_t baud_rate = RF_DEFAULT_BAUD_RATE;
    int8_t m0_pin = RF_DEFAULT_M0_PIN;
    int8_t m1_pin = RF_DEFAULT_M1_PIN;
    int8_t aux_pin = RF_DEFAULT_AUX_PIN;

    bool isValid() const {
        return uart_num <= 2 && tx_pin >= 0 && rx_pin >= 0 && tx_pin != rx_pin &&
               baud_rate >= 1200 && baud_rate <= 115200;
    }
};
