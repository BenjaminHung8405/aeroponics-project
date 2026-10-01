#pragma once

#include "core/IClock.h"
#include "config.h"

class FakeClock : public IClock {
public:
    FakeClock(uint8_t hour = 12, bool is_valid = true)
        : current_time_{hour, 0, 0, is_valid}, unix_time_(0) {}

    void setTime(uint8_t hour, uint8_t min = 0, uint8_t sec = 0, bool valid = true) {
        current_time_ = SystemTime{hour, min, sec, valid};
    }

    void setUnixTime(int64_t unix_time) {
        unix_time_ = unix_time;
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

    bool isDayMode() const {
        if (!current_time_.is_valid) return true;
        return (current_time_.hour >= DAY_START_HOUR && current_time_.hour < NIGHT_START_HOUR);
    }

    void setMonotonicTimeUs(int64_t us) {
        monotonic_time_us_ = us;
    }

    void advanceMonotonicTimeUs(int64_t delta_us) {
        monotonic_time_us_ += delta_us;
    }

    int64_t getMonotonicTimeUs() const {
        return monotonic_time_us_;
    }

private:
    SystemTime current_time_;
    int64_t unix_time_;
    int64_t monotonic_time_us_ = 0;
};
