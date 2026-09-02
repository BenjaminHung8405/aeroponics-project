#if defined(__AVR__)
#include "atmega8_eeprom_schedule_storage.h"
#include <Arduino.h>

#if defined(__AVR__)
#include <avr/eeprom.h>
#endif

namespace {
class Atmega8PumpDriver final : public IPumpActuatorDriver {
public:
    void begin() {
        pinMode(kPumpPin, OUTPUT);
        digitalWrite(kPumpPin, LOW);
        output_ = false;
    }
    void setPumpOutput(bool level) override {
        output_ = level;
        digitalWrite(kPumpPin, level ? HIGH : LOW);
    }
    bool readDriverSense() override { return output_; }
    bool getOutputLevel() const override { return output_; }

private:
    static constexpr uint8_t kPumpPin = 4;
    bool output_ = false;
};

class Atmega8RfTransport final : public IRfTransport {
public:
    bool begin() override {
        // Minimal UART setup: do not pull Arduino HardwareSerial/Print into
        // the 8 KiB node image. UBRR=103 is 9600 baud at 16 MHz, normal mode.
        UBRRH = 0;
        UBRRL = 103;
        UCSRB = _BV(RXEN) | _BV(TXEN);
        UCSRC = _BV(URSEL) | _BV(UCSZ1) | _BV(UCSZ0);
        return true;
    }
    size_t send(const uint8_t* data, size_t length) override {
        if (data == nullptr) return 0;
        for (size_t i = 0; i < length; ++i) {
            while ((UCSRA & _BV(UDRE)) == 0) {}
            UDR = data[i];
        }
        return length;
    }
    size_t receive(uint8_t* buffer, size_t max_length) override {
        if (buffer == nullptr || max_length == 0) return 0;
        size_t count = 0;
        while ((UCSRA & _BV(RXC)) != 0 && count < max_length) {
            buffer[count++] = UDR;
        }
        return count;
    }
    size_t available() override { return (UCSRA & _BV(RXC)) ? 1U : 0U; }
    void flush() override { while ((UCSRA & _BV(UDRE)) == 0) {} }
};

constexpr uint8_t kNodeId = 1;
constexpr uint16_t kPskEepromAddress = 256;
Atmega8EepromScheduleStorage g_schedule_storage;
Atmega8PumpDriver g_actuator;
Atmega8RfTransport g_rf_transport;
NodeCommandProcessor g_processor;
uint8_t g_psk[16] = {};
uint8_t g_rx_buffer[RF_MAX_RX_BUFFER_SIZE] = {};
}

void setup() {
#if defined(__AVR__)
    eeprom_read_block(g_psk, reinterpret_cast<const void*>(kPskEepromAddress), sizeof(g_psk));
#endif
    g_actuator.begin();
    g_rf_transport.begin();
    g_processor.begin(kNodeId, &g_rf_transport, &g_actuator, g_psk, sizeof(g_psk), 1,
                     &g_schedule_storage);
}

void loop() {
    const uint32_t now_ms = millis();
    const size_t received = g_rf_transport.receive(g_rx_buffer, sizeof(g_rx_buffer));
    if (received > 0) {
        g_processor.processIncomingFrame(g_rx_buffer, received, now_ms);
    }
    g_processor.service(now_ms);
}
#endif
