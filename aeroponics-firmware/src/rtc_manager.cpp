#include "rtc_manager.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)

#include <Wire.h>
#include <time.h>
#include <sys/time.h>
#include <esp_log.h>
#include <Arduino.h>

static const char *TAG = "RTC_MANAGER";

RtcManager::RtcManager() 
    : rtc_initialized_(false), 
      rtc_time_trusted_(false), 
      last_source_(TimeSource::UNKNOWN) {}

RtcManager::~RtcManager() {}

bool RtcManager::begin() {
    if (!rtc_.begin()) {
        ESP_LOGE(TAG, "Couldn't find DS3231 RTC hardware module on I2C bus!");
        rtc_initialized_ = false;
        rtc_time_trusted_ = false;
        return false;
    }

    rtc_initialized_ = true;
    if (rtc_.lostPower()) {
        ESP_LOGW(TAG, "DS3231 RTC lost power! Time is untrusted until synchronized with NTP or set manually.");
        rtc_time_trusted_ = false;
    } else {
        rtc_time_trusted_ = true;
        ESP_LOGI(TAG, "DS3231 RTC module detected and time status is trusted.");
    }

    return true;
}

bool RtcManager::syncFromNtp() {
    ESP_LOGI(TAG, "Initiating NTP time synchronization (Server: %s, Offset: %d s)...", 
             NTP_SERVER_PRIMARY, TIMEZONE_OFFSET_S);

    configTime(TIMEZONE_OFFSET_S, DAYLIGHT_OFFSET_S, NTP_SERVER_PRIMARY);

    struct tm timeinfo;
    uint32_t polled_ms = 0;
    bool sync_success = false;

    while (polled_ms < NTP_SYNC_TIMEOUT_MS) {
        if (getLocalTime(&timeinfo, NTP_POLL_INTERVAL_MS)) {
            sync_success = true;
            break;
        }
        polled_ms += NTP_POLL_INTERVAL_MS;
    }

    if (!sync_success) {
        ESP_LOGW(TAG, "NTP synchronization timed out after %u ms. Could not retrieve network time.", NTP_SYNC_TIMEOUT_MS);
        return false;
    }

    ESP_LOGI(TAG, "NTP time acquired successfully: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    if (rtc_initialized_) {
        DateTime dt(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        adjustTime(dt);
        ESP_LOGI(TAG, "DS3231 hardware RTC updated with NTP reference time.");
    } else {
        ESP_LOGW(TAG, "NTP sync succeeded, but DS3231 RTC is uninitialized. Using system time only.");
    }

    last_source_ = TimeSource::SYSTEM_NTP;
    return true;
}

void RtcManager::adjustTime(const DateTime& dt) {
    if (!rtc_initialized_) {
        ESP_LOGE(TAG, "Cannot adjust RTC time: hardware RTC not initialized.");
        return;
    }

    rtc_.adjust(dt);
    rtc_time_trusted_ = true;
    ESP_LOGI(TAG, "DS3231 RTC manually adjusted to %04d-%02d-%02d %02d:%02d:%02d",
             dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second());
}

SystemTime RtcManager::getTime() {
    SystemTime st = {0, 0, 0, false};

    if (rtc_initialized_ && rtc_time_trusted_) {
        DateTime now = rtc_.now();
        st.hour = now.hour();
        st.minute = now.minute();
        st.second = now.second();
        st.is_valid = true;

        if (last_source_ != TimeSource::DS3231_RTC) {
            ESP_LOGI(TAG, "System time source: Priority 1 (DS3231 Hardware RTC)");
            last_source_ = TimeSource::DS3231_RTC;
        }
        return st;
    }

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, SYSTEM_TIME_READ_TIMEOUT_MS)) {
        st.hour = timeinfo.tm_hour;
        st.minute = timeinfo.tm_min;
        st.second = timeinfo.tm_sec;
        st.is_valid = true;

        if (last_source_ != TimeSource::SYSTEM_NTP) {
            ESP_LOGI(TAG, "System time source: Priority 2 (ESP-IDF System Clock / NTP)");
            last_source_ = TimeSource::SYSTEM_NTP;
        }
        return st;
    }

    st.hour = 0;
    st.minute = 0;
    st.second = 0;
    st.is_valid = false;

    if (last_source_ != TimeSource::INVALID) {
        ESP_LOGW(TAG, "System time source: Priority 3 (INVALID - No trusted time available)");
        last_source_ = TimeSource::INVALID;
    }
    return st;
}

bool RtcManager::isNightMode() {
    SystemTime st = getTime();

    // Rule S1-RTC-04: If is_valid == false, MUST return false (DAY mode) as safe fallback.
    if (!st.is_valid) {
        ESP_LOGW(TAG, "System time invalid. Safe-state fallback: DAY mode active");
        return false;
    }

    bool night = (st.hour >= NIGHT_START_HOUR || st.hour < DAY_START_HOUR);
    ESP_LOGD(TAG, "Current time %02d:%02d:%02d -> Mode: %s", 
             st.hour, st.minute, st.second, night ? "NIGHT" : "DAY");
    return night;
}

#endif // ESP_PLATFORM || ARDUINO
