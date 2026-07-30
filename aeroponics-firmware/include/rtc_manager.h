#pragma once

#include <cstdint>
#include <RTClib.h>
#include "config.h"

/**
 * @brief Plain Old Data (POD) struct representing system time.
 * Stack-optimized for FreeRTOS tasks (no inheritance, no virtual methods).
 */
struct SystemTime {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    bool is_valid;
};

/**
 * @brief Adapter class wrapping DS3231 RTC hardware and ESP-IDF system time.
 * Hides time source details from callers and provides seamless NTP synchronization.
 */
class RtcManager {
public:
    RtcManager();
    ~RtcManager();

    /**
     * @brief Initialize I2C interface and communicate with DS3231 RTC hardware.
     * @return true if DS3231 RTC is detected and initialized, false otherwise.
     */
    bool begin();

    /**
     * @brief Synchronize time from NTP server and adjust DS3231 RTC hardware clock.
     * @return true if NTP sync succeeded within timeout and RTC was updated, false otherwise.
     */
    bool syncFromNtp();

    /**
     * @brief Retrieve current time with fallback hierarchy:
     * 1. DS3231 Hardware RTC (Priority 1)
     * 2. ESP-IDF System Time (Priority 2)
     * 3. Invalid status (is_valid = false) (Priority 3)
     * @return SystemTime snapshot.
     */
    SystemTime getTime();

    /**
     * @brief Check if current time is within Night Mode window (NIGHT_START_HOUR to DAY_START_HOUR).
     * Rule S1-RTC-04: If is_valid == false, MUST return false (DAY mode) as fail-safe.
     * @return true if Night Mode, false if Day Mode (or fail-safe fallback).
     */
    bool isNightMode();

private:
    enum class TimeSource {
        UNKNOWN,
        DS3231_RTC,
        SYSTEM_NTP,
        INVALID
    };

    RTC_DS3231 rtc_;
    bool rtc_initialized_;
    TimeSource last_source_;
};
