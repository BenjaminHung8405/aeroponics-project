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
constexpr char RF_NVS_UART_M0_PIN_KEY[] = "uart_m0_pin";
constexpr char RF_NVS_UART_M1_PIN_KEY[] = "uart_m1_pin";
constexpr char RF_NVS_UART_AUX_PIN_KEY[] = "uart_aux_pin";

// Default approved production hardware constants (MKE-K01 / ESP32-S3 Header J1: Pins 18-22)
constexpr uint8_t RF_DEFAULT_UART_NUM = 1;
constexpr int8_t RF_DEFAULT_TX_PIN = 12; // J1-18 (GPIO12) -> RF RXD
constexpr int8_t RF_DEFAULT_RX_PIN = 13; // J1-19 (GPIO13) <- RF TXD
constexpr int8_t RF_DEFAULT_M0_PIN = -1; // Unconnected on 4-wire jack
constexpr int8_t RF_DEFAULT_M1_PIN = -1; // Unconnected on 4-wire jack
constexpr int8_t RF_DEFAULT_AUX_PIN = -1; // Unconnected on 4-wire jack
constexpr uint32_t RF_DEFAULT_BAUD_RATE = 38400;

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
