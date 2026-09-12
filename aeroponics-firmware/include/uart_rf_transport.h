#pragma once

#include "core/IRfTransport.h"
#include "config.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <HardwareSerial.h>
#else
#include <vector>
#endif

constexpr size_t UART_RF_DEFAULT_RX_BUFFER_CAPACITY = 256;

/**
 * @brief Diagnostic and performance counters for RF UART transport.
 */
struct UartTransportStats {
    uint32_t tx_bytes = 0;
    uint32_t rx_bytes = 0;
    uint32_t tx_packets = 0;
    uint32_t rx_packets = 0;
    uint32_t dropped_bytes = 0;
    uint32_t rx_overflows = 0;
    uint32_t tx_errors = 0;
    uint32_t crc_errors = 0;
};

/**
 * @brief Concrete implementation of IRfTransport using ESP32 HardwareSerial UART.
 *
 * Dedicated RF UART interface operating on separately provisioned pins, isolated from USB debug Serial.
 * Provides non-blocking I/O, bounded RX buffer management, drop/overflow/CRC counters, and rate-limited logging.
 */
class UartRfTransport : public IRfTransport {
public:
    UartRfTransport(uint8_t uart_num, int8_t rx_pin, int8_t tx_pin, uint32_t baud_rate,
                    size_t rx_capacity = UART_RF_DEFAULT_RX_BUFFER_CAPACITY,
                    int8_t m0_pin = -1, int8_t m1_pin = -1, int8_t aux_pin = -1);
    ~UartRfTransport() override = default;

    bool begin() override;
    size_t send(const uint8_t* data, size_t length) override;
    size_t receive(uint8_t* buffer, size_t max_length) override;
    size_t available() override;
    void flush() override;

    bool isInitialized() const { return _initialized; }
    uint8_t getUartNum() const { return _uart_num; }
    int8_t getRxPin() const { return _rx_pin; }
    int8_t getTxPin() const { return _tx_pin; }
    uint32_t getBaudRate() const { return _baud_rate; }
    size_t getRxCapacity() const { return _rx_capacity; }
    int8_t getM0Pin() const { return _m0_pin; }
    int8_t getM1Pin() const { return _m1_pin; }
    int8_t getAuxPin() const { return _aux_pin; }

    bool isAuxReady() const;
    void setMode(uint8_t m0, uint8_t m1);
    void recordCrcError() { _stats.crc_errors++; }
    void recordDroppedBytes(size_t count) { _stats.dropped_bytes += count; }

    const UartTransportStats& getStats() const { return _stats; }
    void resetStats() { _stats = UartTransportStats{}; }

#if !defined(ESP_PLATFORM) && !defined(ARDUINO)
    // Host / simulation test helpers
    void injectRxBytes(const uint8_t* data, size_t length);
    const std::vector<uint8_t>& getTxHistory() const { return _host_tx_buffer; }
    void clearTxHistory() { _host_tx_buffer.clear(); }
    void setSimulateTxError(bool enable) { _simulate_tx_error = enable; }
    void setSimulateAuxBusy(bool busy) { _simulate_aux_busy = busy; }
#endif

private:
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    HardwareSerial _rf_serial;
    uint32_t _last_log_ms = 0;
#else
    std::vector<uint8_t> _host_rx_fifo;
    std::vector<uint8_t> _host_tx_buffer;
    bool _simulate_tx_error = false;
    bool _simulate_aux_busy = false;
#endif
    uint8_t _uart_num;
    int8_t _rx_pin;
    int8_t _tx_pin;
    uint32_t _baud_rate;
    size_t _rx_capacity;
    int8_t _m0_pin;
    int8_t _m1_pin;
    int8_t _aux_pin;
    bool _initialized;
    UartTransportStats _stats;
};
