#include "uart_rf_transport.h"
#include <cstring>
#include <algorithm>
#include <new>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "driver/uart.h"
#if defined(ESP_PLATFORM)
#include "hal/uart_ll.h"
#endif
static const char* TAG = "RF_UART";

namespace {
constexpr uint32_t kSerial8N2 =
#if defined(SERIAL_8N2)
    SERIAL_8N2;
#else
    // Arduino-ESP32 compatibility fallback for cores that do not export the
    // symbolic constant. This is UART_DATA_8_BITS | UART_PARITY_DISABLE |
    // UART_STOP_BITS_2 encoded by the ESP32 serial API.
    0x800003c;
#endif
}
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
    if (_serial_config == 0) {
        _serial_config = kSerial8N2;
    }
    pinMode(_rx_pin, INPUT_PULLUP);
    _rf_serial.begin(_baud_rate, _serial_config, _rx_pin, _tx_pin);
#if defined(RF_UART_RING_BUFFER_ACTIVE)
    // Allocate the bounded ring buffer exactly once (compile-time constant).
    // No heap or new is allowed inside the ISR or the consumer task loop.
    _ring_size = RF_UART_RING_BUFFER_SIZE;
    _ring_buffer = new (std::nothrow) volatile uint8_t[_ring_size];
    if (!_ring_buffer) {
        ESP_LOGE(TAG, "Failed to allocate RF UART ring buffer of size %zu", _ring_size);
        return false;
    }
    _ring_head = 0;
    _ring_tail = 0;
    _dropped_bytes = 0;
    _rx_overflows = 0;

    // Create the ISR -> consumer notification queue once (bounded).
    _rx_notify_queue = xQueueCreate(1, sizeof(uint32_t));
    if (!_rx_notify_queue) {
        ESP_LOGE(TAG, "Failed to create RF UART ISR notification queue");
        delete[] _ring_buffer;
        _ring_buffer = nullptr;
        return false;
    }

    // Register the RX ISR (Core 1 affinity via uart_isr_register). The handler
    // is non-blocking: it never calls delay/malloc/printf or blocks the ISR.
    const esp_err_t isr_err =
        uart_isr_register(static_cast<int>(_uart_num), uartRxIsr, this, 0, nullptr);
    if (isr_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register RF UART RX ISR: %s (0x%x)",
                 esp_err_to_name(isr_err), isr_err);
        vQueueDelete(_rx_notify_queue);
        _rx_notify_queue = nullptr;
        delete[] _ring_buffer;
        _ring_buffer = nullptr;
        return false;
    }
#endif
    _initialized = true;
    resetStats();
    ESP_LOGI(TAG, "RF UART interface initialized on UART%u (RX:%d, TX:%d, Baud:%u, Config:0x%X, Capacity:%zu, M0:%d, M1:%d, AUX:%d)",
             _uart_num, _rx_pin, _tx_pin, _baud_rate, _serial_config, _rx_capacity, _m0_pin, _m1_pin, _aux_pin);
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

void UartRfTransport::flushRx() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_initialized) {
        while (_rf_serial.available() > 0) {
            _rf_serial.read();
        }
    }
#else
    _host_rx_fifo.clear();
#endif
}

void UartRfTransport::setBaudRate(uint32_t baud_rate, uint32_t serial_config) {
    _baud_rate = baud_rate;
    if (serial_config != 0) {
        _serial_config = serial_config;
    }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_initialized) {
        _rf_serial.begin(_baud_rate, _serial_config, _rx_pin, _tx_pin);
        ESP_LOGI(TAG, "RF UART reconfigured: Baud=%u, Config=0x%X", _baud_rate, _serial_config);
    }
#endif
}

bool UartRfTransport::setPins(int8_t rx_pin, int8_t tx_pin) {
    if (rx_pin < 0 || tx_pin < 0 || rx_pin == tx_pin) return false;
    _rx_pin = rx_pin;
    _tx_pin = tx_pin;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_initialized) {
        pinMode(_rx_pin, INPUT_PULLUP);
        _rf_serial.begin(_baud_rate, _serial_config, _rx_pin, _tx_pin);
        ESP_LOGI(TAG, "RF UART pins reconfigured: RX=%d, TX=%d", _rx_pin, _tx_pin);
    }
#endif
    return true;
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
    _rf_serial.flush();
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
#if defined(RF_UART_RING_BUFFER_ACTIVE)
    // Read from the bounded ring buffer filled by the Core 1 RX ISR. This path
    // replaces the direct HardwareSerial read while the ring buffer is active;
    // the legacy direct read is preserved below for backward compatibility.
    size_t count = 0;
    while (_ring_head != _ring_tail && count < max_length) {
        buffer[count++] = static_cast<uint8_t>(_ring_buffer[_ring_tail]);
        _ring_tail = (_ring_tail + 1) % _ring_size;
    }
    if (count > 0) {
        _stats.rx_bytes += count;
        _stats.rx_packets++;
    }
    return count;
#else
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
#endif
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
#if defined(RF_UART_RING_BUFFER_ACTIVE)
    // Bytes currently waiting in the bounded ring buffer (bounded by size - 1).
    if (_ring_size == 0) return 0;
    const size_t used = (_ring_head + _ring_size - _ring_tail) % _ring_size;
    return used;
#else
    return static_cast<size_t>(_rf_serial.available());
#endif
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

bool UartRfTransport::startRxTask() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    return xTaskCreatePinnedToCore(
        rxTaskFunction, RF_UART_RX_TASK_NAME,
        RF_UART_RX_TASK_STACK_SIZE, this,
        RF_UART_RX_TASK_PRIORITY, &_rx_task_handle,
        RF_UART_RX_TASK_CORE) == pdPASS;
#else
    return true;
#endif
}

void UartRfTransport::stopRxTask() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (_rx_task_handle) {
        vTaskDelete(_rx_task_handle);
        _rx_task_handle = nullptr;
    }
#endif
}

size_t UartRfTransport::getDroppedBytes() const {
    return _dropped_bytes;
}

size_t UartRfTransport::getRxOverflows() const {
    return _rx_overflows;
}

#if defined(ESP_PLATFORM) || defined(ARDUINO)
void UartRfTransport::rxTaskFunction(void* param) {
    UartRfTransport* self = static_cast<UartRfTransport*>(param);
    (void)self;
    uint32_t notify;
    for (;;) {
        // Block until ISR signals new data via notification queue
        if (xTaskNotifyWait(0, UINT32_MAX, &notify, portMAX_DELAY) == pdTRUE) {
            // Consume all bytes currently in the ring buffer
            while (self->_ring_head != self->_ring_tail) {
                uint8_t byte = self->_ring_buffer[self->_ring_tail];
                self->_ring_tail = (self->_ring_tail + 1) % self->_ring_size;
                // Feed byte into any downstream accumulator; for Sprint 1 the
                // synchronous receive() path is the primary consumer, so we loop
                // without double-draining.
            }
        }
    }
}

void UartRfTransport::rxTaskLoop() {
    // Entry point kept for compatibility with the documentation reference;
    // the task created by startRxTask() dispatches to rxTaskFunction above.
    for (;;) {
        if (xTaskNotifyWait(0, UINT32_MAX, nullptr, portMAX_DELAY) == pdTRUE) {
            // Notified by ISR; ring buffer is drained by receive() — this task
            // acts as the architectural hook for future async frame parsing.
        }
    }
}

void UartRfTransport::uartRxIsr(void* arg) {
    UartRfTransport* self = static_cast<UartRfTransport*>(arg);
    uart_dev_t* uart_hw = UART_LL_GET_HW(self->_uart_num);
    // Read all available bytes from UART FIFO (non-blocking).
    // The ISR must NOT block (no delay/malloc/printf). It drops bytes if the
    // ring buffer is full and increments overflow/drop counters.
    while (uart_ll_get_rxfifo_len(uart_hw) > 0) {
        uint8_t byte;
        uart_ll_read_rxfifo(uart_hw, &byte, 1);
        size_t next_head = (self->_ring_head + 1) % self->_ring_size;
        if (next_head == self->_ring_tail) {
            // BUFFER FULL — drop byte, increment counters, do NOT block ISR
            self->_dropped_bytes++;
            self->_rx_overflows++;
            return; // Return immediately to avoid blocking the ISR
        }
        self->_ring_buffer[self->_ring_head] = byte;
        self->_ring_head = next_head;
    }
    // Signal the consumer task that new data is available.
    BaseType_t hp_woken = pdFALSE;
    uint32_t notify_value = 1;
    xQueueSendFromISR(self->_rx_notify_queue, &notify_value, &hp_woken);
    portYIELD_FROM_ISR(hp_woken);
}
#endif

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
