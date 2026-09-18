#pragma once

#include <cstdint>

enum class LedState : uint8_t {
    OFF = 0,
    CONNECTED_GREEN,
    PORTAL_YELLOW_BLINK,
    OFFLINE_RED
};

class HardwareButton {
public:
    explicit HardwareButton(int8_t button_pin = 0, int16_t led_pin = -1);
    ~HardwareButton() = default;

    void begin();
    void update(uint32_t now_ms);

    bool isLongPressDetected();
    void resetLongPress();

    void setLedState(LedState state);
    LedState getLedState() const { return current_led_state_; }

private:
    int8_t button_pin_;
    int16_t led_pin_;
    bool button_configured_ = false;
    bool led_configured_ = false;

    bool last_raw_reading_ = true;
    bool stable_state_ = true;
    uint32_t last_debounce_ms_ = 0;
    uint32_t press_start_ms_ = 0;
    bool long_press_fired_ = false;

    LedState current_led_state_ = LedState::OFF;
    uint32_t last_blink_ms_ = 0;
    bool blink_phase_ = false;
};
