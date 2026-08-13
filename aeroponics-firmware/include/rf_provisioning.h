#pragma once

#include <cstdint>

/** Canonical RF provisioning keys; NVS encryption is not asserted by this interface. */
constexpr char RF_NVS_NAMESPACE[] = "rf_config";
constexpr char RF_NVS_PSK_WORD_KEYS[][11] = {"psk_word_0", "psk_word_1", "psk_word_2", "psk_word_3"};
constexpr char RF_NVS_BOOT_SESSION_KEY[] = "boot_session";
constexpr char RF_NVS_UART_NUM_KEY[] = "uart_num";
constexpr char RF_NVS_UART_TX_PIN_KEY[] = "uart_tx_pin";
constexpr char RF_NVS_UART_RX_PIN_KEY[] = "uart_rx_pin";
constexpr char RF_NVS_UART_BAUD_KEY[] = "uart_baud";

/** Non-secret release gate; independent security evidence is required for production RF. */
#if defined(RF_PROVISIONING_INDEPENDENT_SIGNOFF) && RF_PROVISIONING_INDEPENDENT_SIGNOFF == 1
constexpr bool RF_PROVISIONING_INDEPENDENT_SIGNOFF_PRESENT = true;
#else
constexpr bool RF_PROVISIONING_INDEPENDENT_SIGNOFF_PRESENT = false;
#endif

struct RfHardwareConfig {
    uint8_t uart_num = 0;
    int8_t tx_pin = -1;
    int8_t rx_pin = -1;
    uint32_t baud_rate = 0;

    bool isValid() const {
        return uart_num <= 2 && tx_pin >= 0 && rx_pin >= 0 && tx_pin != rx_pin &&
               baud_rate >= 1200 && baud_rate <= 115200;
    }
};
