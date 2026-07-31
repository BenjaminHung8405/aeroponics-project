#pragma once

#include <cstdint>
#include "config.h"
#include "core/IClock.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <RTClib.h>
#endif

/**
 * @brief Adapter class wrapping DS3231 RTC hardware and ESP-IDF system time.
 * Implements IClock core interface.
 */
class RtcManager : public IClock {
public:
    RtcManager();
    ~RtcManager() override;

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

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    /**
     * @brief Manually adjust time on DS3231 RTC hardware and mark RTC time as trusted.
     * @param dt DateTime object to set RTC clock to.
     */
    void adjustTime(const DateTime& dt);
#endif

    /**
     * @brief Retrieve current time with fallback hierarchy:
     * 1. DS3231 Hardware RTC (Priority 1 - trusted RTC only)
     * 2. ESP-IDF System Time (Priority 2)
     * 3. Invalid status (is_valid = false) (Priority 3)
     * @return SystemTime snapshot.
     */
    SystemTime getTime() override;

    /**
     * @brief Check if current time is within Night Mode window (NIGHT_START_HOUR to DAY_START_HOUR).
     * Rule S1-RTC-04: If is_valid == false, MUST return false (DAY mode) as fail-safe.
     * @return true if Night Mode, false if Day Mode (or fail-safe fallback).
     */
    bool isNightMode() override;

private:
    enum class TimeSource {
        UNKNOWN,
        DS3231_RTC,
        SYSTEM_NTP,
        INVALID
    };

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    RTC_DS3231 rtc_;
#endif
    bool rtc_initialized_;
    bool rtc_time_trusted_;
    TimeSource last_source_;
};
