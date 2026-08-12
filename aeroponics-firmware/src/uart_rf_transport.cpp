#include "uart_rf_transport.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <esp_log.h>
static const char* TAG = "RF_UART";
#endif

UartRfTransport::UartRfTransport(uint8_t uart_num, int8_t rx_pin, int8_t tx_pin, uint32_t baud_rate)
    :
#if defined(ESP_PLATFORM) || defined(ARDUINO)
      _rf_serial(uart_num),
#endif
      _uart_num(uart_num),
      _rx_pin(rx_pin),
      _tx_pin(tx_pin),
      _baud_rate(baud_rate),
      _initialized(false) {
}

bool UartRfTransport::begin() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    _rf_serial.begin(_baud_rate, SERIAL_8N1, _rx_pin, _tx_pin);
    _initialized = true;
    ESP_LOGI(TAG, "RF UART interface initialized on UART%u (RX:%d, TX:%d, Baud:%u)",
             _uart_num, _rx_pin, _tx_pin, _baud_rate);
    return true;
#else
    _initialized = true;
    return true;
#endif
}

size_t UartRfTransport::send(const uint8_t* data, size_t length) {
    if (!_initialized || !data || length == 0) return 0;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    return _rf_serial.write(data, length);
#else
    return length;
#endif
}

size_t UartRfTransport::receive(uint8_t* buffer, size_t max_length) {
    if (!_initialized || !buffer || max_length == 0) return 0;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    size_t count = 0;
    while (_rf_serial.available() > 0 && count < max_length) {
        int c = _rf_serial.read();
        if (c < 0) break;
        buffer[count++] = static_cast<uint8_t>(c);
    }
    return count;
#else
    return 0;
#endif
}

size_t UartRfTransport::available() {
    if (!_initialized) return 0;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    return static_cast<size_t>(_rf_serial.available());
#else
    return 0;
#endif
}

void UartRfTransport::flush() {
    if (!_initialized) return;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    _rf_serial.flush();
#endif
}
