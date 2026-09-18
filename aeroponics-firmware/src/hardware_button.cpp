#include "hardware_button.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <Arduino.h>
#endif

namespace {
constexpr uint32_t DEBOUNCE_DELAY_MS = 50;
constexpr uint32_t LONG_PRESS_DURATION_MS = 3000;
constexpr uint32_t BLINK_INTERVAL_MS = 500;
}

HardwareButton::HardwareButton(int8_t button_pin, int8_t led_pin)
    : button_pin_(button_pin),
      led_pin_(led_pin),
      button_configured_(false),
      led_configured_(false),
      last_raw_reading_(true),
      stable_state_(true),
      last_debounce_ms_(0),
      press_start_ms_(0),
      long_press_fired_(false),
      current_led_state_(LedState::OFF),
      last_blink_ms_(0),
      blink_phase_(false) {
}

void HardwareButton::begin() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (button_pin_ >= 0) {
        pinMode(button_pin_, INPUT_PULLUP);
        button_configured_ = true;
    }
    if (led_pin_ >= 0) {
        pinMode(led_pin_, OUTPUT);
        digitalWrite(led_pin_, LOW);
        led_configured_ = true;
    }
#else
    button_configured_ = (button_pin_ >= 0);
    led_configured_ = (led_pin_ >= 0);
#endif
}

void HardwareButton::update(uint32_t now_ms) {
    if (button_configured_) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        const bool raw = (digitalRead(button_pin_) == HIGH);
#else
        const bool raw = last_raw_reading_;
#endif
        if (raw != last_raw_reading_) {
            last_debounce_ms_ = now_ms;
            last_raw_reading_ = raw;
        }

        if ((now_ms - last_debounce_ms_) > DEBOUNCE_DELAY_MS) {
            if (raw != stable_state_) {
                stable_state_ = raw;
                if (!stable_state_) {
                    // Button pressed (Active LOW)
                    press_start_ms_ = now_ms;
                    long_press_fired_ = false;
                } else {
                    // Button released
                    press_start_ms_ = 0;
                }
            }
        }

        if (!stable_state_ && !long_press_fired_ && press_start_ms_ > 0) {
            if (now_ms - press_start_ms_ >= LONG_PRESS_DURATION_MS) {
                long_press_fired_ = true;
            }
        }
    }

    // LED State Service
    if (led_configured_) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        switch (current_led_state_) {
            case LedState::CONNECTED_GREEN:
                digitalWrite(led_pin_, HIGH);
                break;
            case LedState::OFFLINE_RED:
            case LedState::OFF:
                digitalWrite(led_pin_, LOW);
                break;
            case LedState::PORTAL_YELLOW_BLINK:
                if (now_ms - last_blink_ms_ >= BLINK_INTERVAL_MS) {
                    last_blink_ms_ = now_ms;
                    blink_phase_ = !blink_phase_;
                    digitalWrite(led_pin_, blink_phase_ ? HIGH : LOW);
                }
                break;
        }
#endif
    }
}

bool HardwareButton::isLongPressDetected() {
    return long_press_fired_;
}

void HardwareButton::resetLongPress() {
    long_press_fired_ = false;
}

void HardwareButton::setLedState(LedState state) {
    current_led_state_ = state;
}
