#pragma once

#include "core/IRfTransport.h"
#include "config.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <HardwareSerial.h>
#endif

/**
 * @brief Concrete implementation of IRfTransport using ESP32 HardwareSerial UART.
 *
 * Dedicated RF UART interface operating on separate GPIO pins (UART2) from USB debug Serial (UART0).
 */
class UartRfTransport : public IRfTransport {
public:
    UartRfTransport(uint8_t uart_num = RF_UART_NUM,
                    int8_t rx_pin = RF_UART_RX_PIN,
                    int8_t tx_pin = RF_UART_TX_PIN,
                    uint32_t baud_rate = RF_UART_BAUD_RATE);
    ~UartRfTransport() override = default;

    bool begin() override;
    size_t send(const uint8_t* data, size_t length) override;
    size_t receive(uint8_t* buffer, size_t max_length) override;
    size_t available() override;
    void flush() override;

    bool isInitialized() const { return _initialized; }

private:
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    HardwareSerial _rf_serial;
#endif
    uint8_t _uart_num;
    int8_t _rx_pin;
    int8_t _tx_pin;
    uint32_t _baud_rate;
    bool _initialized;
};
