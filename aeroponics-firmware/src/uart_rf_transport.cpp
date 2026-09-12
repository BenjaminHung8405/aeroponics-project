#include "uart_rf_transport.h"
#include <cstring>
#include <algorithm>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <esp_log.h>
#include <esp_timer.h>
static const char* TAG = "RF_UART";
#endif

UartRfTransport::UartRfTransport(uint8_t uart_num, int8_t rx_pin, int8_t tx_pin, uint32_t baud_rate,
                                 size_t rx_capacity, int8_t m0_pin, int8_t m1_pin, int8_t aux_pin)
    :
#if defined(ESP_PLATFORM) || defined(ARDUINO)
      _rf_serial(uart_num),
      _last_log_ms(0),
#endif
      _uart_num(uart_num),
      _rx_pin(rx_pin),
      _tx_pin(tx_pin),
      _baud_rate(baud_rate),
      _rx_capacity(rx_capacity > 0 ? rx_capacity : UART_RF_DEFAULT_RX_BUFFER_CAPACITY),
      _m0_pin(m0_pin),
      _m1_pin(m1_pin),
      _aux_pin(aux_pin),
      _initialized(false),
      _stats() {
}

bool UartRfTransport::begin() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_rx_pin < 0 || _tx_pin < 0 || _baud_rate == 0) {
        ESP_LOGE(TAG, "Invalid RF UART configuration (UART%u, RX:%d, TX:%d, Baud:%u)",
                 _uart_num, _rx_pin, _tx_pin, _baud_rate);
        return false;
    }
    if (_m0_pin >= 0) {
        pinMode(_m0_pin, OUTPUT);
        digitalWrite(_m0_pin, LOW); // Normal transmission mode
    }
    if (_m1_pin >= 0) {
        pinMode(_m1_pin, OUTPUT);
        digitalWrite(_m1_pin, LOW); // Normal transmission mode
    }
    if (_aux_pin >= 0) {
        pinMode(_aux_pin, INPUT_PULLUP);
    }
    _rf_serial.begin(_baud_rate, SERIAL_8N1, _rx_pin, _tx_pin);
    _initialized = true;
    resetStats();
    ESP_LOGI(TAG, "RF UART interface initialized on UART%u (RX:%d, TX:%d, Baud:%u, Capacity:%zu, M0:%d, M1:%d, AUX:%d)",
             _uart_num, _rx_pin, _tx_pin, _baud_rate, _rx_capacity, _m0_pin, _m1_pin, _aux_pin);
    return true;
#else
    _initialized = true;
    _host_rx_fifo.clear();
    _host_tx_buffer.clear();
    _simulate_aux_busy = false;
    resetStats();
    return true;
#endif
}

bool UartRfTransport::isAuxReady() const {
    if (!_initialized) return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_aux_pin >= 0) {
        return digitalRead(_aux_pin) == HIGH;
    }
    return true;
#else
    return !_simulate_aux_busy;
#endif
}

void UartRfTransport::setMode(uint8_t m0, uint8_t m1) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_m0_pin >= 0) {
        digitalWrite(_m0_pin, m0 ? HIGH : LOW);
    }
    if (_m1_pin >= 0) {
        digitalWrite(_m1_pin, m1 ? HIGH : LOW);
    }
#else
    (void)m0;
    (void)m1;
#endif
}

size_t UartRfTransport::send(const uint8_t* data, size_t length) {
    if (!_initialized || data == nullptr || length == 0) {
        return 0;
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    const size_t written = _rf_serial.write(data, length);
    if (written == length) {
        _stats.tx_bytes += written;
        _stats.tx_packets++;
    } else {
        _stats.tx_bytes += written;
        _stats.tx_errors++;
        const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
        if (now_ms - _last_log_ms >= 5000) {
            _last_log_ms = now_ms;
            ESP_LOGW(TAG, "RF UART write partial/error: wrote %zu/%zu bytes", written, length);
        }
    }
    return written;
#else
    if (_simulate_tx_error) {
        _stats.tx_errors++;
        return 0;
    }
    _host_tx_buffer.insert(_host_tx_buffer.end(), data, data + length);
    _stats.tx_bytes += length;
    _stats.tx_packets++;
    return length;
#endif
}

size_t UartRfTransport::receive(uint8_t* buffer, size_t max_length) {
    if (!_initialized || buffer == nullptr || max_length == 0) {
        return 0;
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    size_t count = 0;
    while (_rf_serial.available() > 0 && count < max_length) {
        int c = _rf_serial.read();
        if (c < 0) break;
        buffer[count++] = static_cast<uint8_t>(c);
    }
    if (count > 0) {
        _stats.rx_bytes += count;
        _stats.rx_packets++;
    }
    return count;
#else
    if (_host_rx_fifo.empty()) {
        return 0;
    }
    const size_t count = std::min(max_length, _host_rx_fifo.size());
    std::memcpy(buffer, _host_rx_fifo.data(), count);
    _host_rx_fifo.erase(_host_rx_fifo.begin(), _host_rx_fifo.begin() + count);
    _stats.rx_bytes += count;
    _stats.rx_packets++;
    return count;
#endif
}

size_t UartRfTransport::available() {
    if (!_initialized) {
        return 0;
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    return static_cast<size_t>(_rf_serial.available());
#else
    return _host_rx_fifo.size();
#endif
}

void UartRfTransport::flush() {
    if (!_initialized) {
        return;
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    _rf_serial.flush();
#else
    _host_rx_fifo.clear();
#endif
}

#if !defined(ESP_PLATFORM) && !defined(ARDUINO)
void UartRfTransport::injectRxBytes(const uint8_t* data, size_t length) {
    if (!_initialized || data == nullptr || length == 0) {
        return;
    }
    const size_t current_size = _host_rx_fifo.size();
    const size_t available_space = (_rx_capacity > current_size) ? (_rx_capacity - current_size) : 0;
    const size_t bytes_to_copy = std::min(length, available_space);

    if (bytes_to_copy > 0) {
        _host_rx_fifo.insert(_host_rx_fifo.end(), data, data + bytes_to_copy);
    }

    if (length > bytes_to_copy) {
        const size_t dropped = length - bytes_to_copy;
        _stats.dropped_bytes += dropped;
        _stats.rx_overflows++;
    }
}
#endif
