#include "rtc_manager.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>
#include <Arduino.h>

static const char *TAG = "RTC_MANAGER";

RtcManager::RtcManager() : rtc_initialized_(false), rtc_time_trusted_(false), last_source_(TimeSource::UNKNOWN) {}

RtcManager::~RtcManager() {}

bool RtcManager::begin() {
    if (rtc_.begin()) {
        rtc_initialized_ = true;
        if (rtc_.lostPower()) {
            rtc_time_trusted_ = false;
            ESP_LOGW(TAG, "DS3231 lost power! Time untrusted until NTP sync or manual adjust.");
        } else {
            rtc_time_trusted_ = true;
            ESP_LOGI(TAG, "DS3231 RTC hardware initialized and trusted.");
        }
        return true;
    } else {
        rtc_initialized_ = false;
        rtc_time_trusted_ = false;
        ESP_LOGW(TAG, "Failed to initialize DS3231 RTC hardware (I2C communication error or device absent).");
        return false;
    }
}

void RtcManager::adjustTime(const DateTime& dt) {
    if (rtc_initialized_) {
        rtc_.adjust(dt);
        rtc_time_trusted_ = true;
        ESP_LOGI(TAG, "DS3231 RTC time adjusted manually and marked trusted.");
    }
}

bool RtcManager::syncFromNtp() {
    ESP_LOGI(TAG, "Initiating NTP time sync (server: %s, offset: %d s)...", NTP_SERVER_PRIMARY, (int)TIMEZONE_OFFSET_S);
    configTime(TIMEZONE_OFFSET_S, DAYLIGHT_OFFSET_S, NTP_SERVER_PRIMARY);

    struct tm timeinfo;
    uint32_t start_ms = millis();
    constexpr uint32_t NTP_TIMEOUT_MS = 10000;
    bool synced = false;

    while (millis() - start_ms < NTP_TIMEOUT_MS) {
        if (getLocalTime(&timeinfo, 500)) {
            synced = true;
            break;
        }
    }

    if (synced) {
        ESP_LOGI(TAG, "NTP time sync successful: %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        if (rtc_initialized_) {
            DateTime dt(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                         timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            rtc_.adjust(dt);
            rtc_time_trusted_ = true;
            ESP_LOGI(TAG, "DS3231 RTC hardware updated with NTP time and marked trusted.");
        } else {
            ESP_LOGW(TAG, "DS3231 hardware RTC not available; NTP system time will be used.");
        }
        return true;
    } else {
        ESP_LOGW(TAG, "NTP sync timed out after 10000ms. Could not obtain valid NTP time.");
        return false;
    }
}

SystemTime RtcManager::getTime() {
    // Priority 1: DS3231 Hardware RTC (only if initialized AND trusted)
    if (rtc_initialized_ && rtc_time_trusted_) {
        DateTime now = rtc_.now();
        if (now.isValid() && now.year() >= 2020) {
            if (last_source_ != TimeSource::DS3231_RTC) {
                ESP_LOGI(TAG, "Time source active: DS3231 Hardware RTC (%02d:%02d:%02d)",
                         now.hour(), now.minute(), now.second());
                last_source_ = TimeSource::DS3231_RTC;
            }
            return SystemTime{ (uint8_t)now.hour(), (uint8_t)now.minute(), (uint8_t)now.second(), true };
        }
    }

    // Priority 2: ESP-IDF System Time
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
        if (timeinfo.tm_year >= (2020 - 1900)) {
            if (last_source_ != TimeSource::SYSTEM_NTP) {
                ESP_LOGI(TAG, "Time source active: ESP-IDF System Time (%02d:%02d:%02d)",
                         timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
                last_source_ = TimeSource::SYSTEM_NTP;
            }
            return SystemTime{ (uint8_t)timeinfo.tm_hour, (uint8_t)timeinfo.tm_min, (uint8_t)timeinfo.tm_sec, true };
        }
    }

    // Priority 3: Invalid status (is_valid = false)
    if (last_source_ != TimeSource::INVALID) {
        ESP_LOGW(TAG, "Time source active: INVALID (No trusted time source available!)");
        last_source_ = TimeSource::INVALID;
    }
    return SystemTime{ 0, 0, 0, false };
}

bool RtcManager::isNightMode() {
    SystemTime st = getTime();
    if (!st.is_valid) {
        ESP_LOGW(TAG, "isNightMode(): Time is invalid! Rule S1-RTC-04 fail-safe active -> returning DAY mode (false).");
        return false;
    }

    bool is_night = (st.hour >= NIGHT_START_HOUR || st.hour < DAY_START_HOUR);
    return is_night;
}

