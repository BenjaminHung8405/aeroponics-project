#pragma once

#include "core/IClock.h"

class FakeClock : public IClock {
public:
    FakeClock(uint8_t hour = 12, bool is_valid = true)
        : current_time_{hour, 0, 0, is_valid} {}

    void setTime(uint8_t hour, uint8_t min = 0, uint8_t sec = 0, bool valid = true) {
        current_time_ = SystemTime{hour, min, sec, valid};
    }

    SystemTime getTime() override {
        return current_time_;
    }

    bool isNightMode() override {
        if (!current_time_.is_valid) {
            return false; // Safe fallback DAY mode
        }
        return (current_time_.hour >= NIGHT_START_HOUR || current_time_.hour < DAY_START_HOUR);
    }

private:
    SystemTime current_time_;
};
