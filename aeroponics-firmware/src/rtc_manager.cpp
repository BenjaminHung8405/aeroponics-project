#include "rtc_manager.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)

#include <Wire.h>
#include <time.h>
#include <sys/time.h>
#include <esp_log.h>
#include <Arduino.h>

#include "core/clock_trust.h"

static const char *TAG = "RTC_MANAGER";

RtcManager::RtcManager()
    : rtc_initialized_(false),
      rtc_time_trusted_(false),
      last_source_(TimeSourceKind::UNKNOWN),
      last_sync_unix_time_utc_(0),
      backend_time_applied_ms_(0) {}

RtcManager::~RtcManager() = default;

// ---------------------------------------------------------------------------
// Clock trust and epoch helpers
// ---------------------------------------------------------------------------

bool RtcManager::isPlausibleEpoch(int64_t unix_time_utc) const {
    return unix_time_utc >= CLOCK_UNIX_TIME_MIN_VALID && unix_time_utc <= CLOCK_UNIX_TIME_MAX_VALID;
}

uint32_t RtcManager::monotonicMillis() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    return millis();
#else
    using namespace std::chrono;
    static const auto start_time = steady_clock::now();
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now() - start_time).count());
#endif
}

bool RtcManager::applyUnixToHardware(int64_t unix_time_utc, TimeSourceKind source) {
    if (!rtc_initialized_) return false;
    if (!isPlausibleEpoch(unix_time_utc)) return false;
    const time_t epoch = static_cast<time_t>(unix_time_utc);
    struct tm timeinfo;
    if (gmtime_r(&epoch, &timeinfo) == nullptr) return false;
    const DateTime dt(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    rtc_.adjust(dt);
    rtc_time_trusted_ = true;
    last_source_ = source;
    last_sync_unix_time_utc_ = unix_time_utc;
    if (source == TimeSourceKind::BACKEND) {
        backend_time_applied_ms_ = monotonicMillis();
    }
    return true;
}

ClockTrustInputs RtcManager::collectTrustInputs() const {
    ClockTrustInputs inputs{};
    inputs.hardware_trusted = rtc_initialized_ && rtc_time_trusted_;
    inputs.system_time_valid = systemTimeIsValid();
    inputs.last_applied_source = last_source_;
    inputs.last_sync_unix_time_utc = last_sync_unix_time_utc_;
    return inputs;
}

bool RtcManager::systemTimeIsValid() const {
    // The ESP-IDF system clock is only authoritative once SNTP (or a prior
    // configTime call) has advanced it past a plausible epoch floor. Before
    // that it reads from a ~1970 boot epoch and must not be treated as valid.
    const time_t now = time(nullptr);
    return now >= static_cast<time_t>(CLOCK_UNIX_TIME_MIN_VALID);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool RtcManager::begin() {
    if (!rtc_.begin()) {
        ESP_LOGE(TAG, "Couldn't find DS1307 RTC hardware module on I2C bus!");
        rtc_initialized_ = false;
        rtc_time_trusted_ = false;
        last_source_ = TimeSourceKind::UNKNOWN;
        return false;
    }

    rtc_initialized_ = true;
    if (!rtc_.isrunning()) {
        ESP_LOGW(TAG, "DS1307 oscillator stopped or coin-cell missing; time is untrusted until set.");
        rtc_time_trusted_ = false;
        last_source_ = TimeSourceKind::UNKNOWN;
    } else {
        // Battery-backed DS1307: the oscillator is running and retained
        // registers are a valid continuation of real time.
        rtc_time_trusted_ = true;
        last_source_ = TimeSourceKind::DS1307_RTC;
        ESP_LOGI(TAG, "DS1307 RTC detected, oscillator active, time is trusted.");
    }

    return true;
}

bool RtcManager::adjustTimeFromUnix(int64_t unix_time_utc, TimeSourceKind source) {
    if (!applyUnixToHardware(unix_time_utc, source)) return false;
    ESP_LOGI(TAG, "DS1307 adjusted from %s to %lld UTC.",
             timeSourceKindToString(source), static_cast<long long>(unix_time_utc));
    return true;
}

#if defined(ESP_PLATFORM) || defined(ARDUINO)
bool RtcManager::adjustTime(const DateTime& dt) {
    if (!rtc_initialized_) return false;
    rtc_.adjust(dt);
    rtc_time_trusted_ = true;
    last_source_ = TimeSourceKind::BACKEND;
    ESP_LOGI(TAG, "DS1307 manually adjusted to %04d-%02d-%02d %02d:%02d:%02d",
             dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second());
    return true;
}
#endif

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
        ESP_LOGW(TAG, "NTP synchronization timed out after %u ms.", NTP_SYNC_TIMEOUT_MS);
        return false;
    }

    ESP_LOGI(TAG, "NTP time acquired: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    if (rtc_initialized_) {
        const time_t epoch_utc = static_cast<time_t>(
            mktime(&timeinfo) - TIMEZONE_OFFSET_S);
        if (applyUnixToHardware(epoch_utc, TimeSourceKind::SYSTEM_NTP)) {
            ESP_LOGI(TAG, "DS1307 hardware RTC updated with NTP reference time.");
        }
    }

    return true;
}

SystemTime RtcManager::getTime() {
    SystemTime st = {0, 0, 0, false};
    const ClockTrustInputs inputs = collectTrustInputs();
    const TimeSourceKind source = ClockTrustResolver::resolve(inputs);

    if (source == TimeSourceKind::DS1307_RTC) {
        DateTime now = rtc_.now();
        st.hour = now.hour();
        st.minute = now.minute();
        st.second = now.second();
        st.is_valid = true;
        last_source_ = TimeSourceKind::DS1307_RTC;
        ESP_LOGD(TAG, "System time source: Priority 1 (DS1307 Hardware RTC)");
        return st;
    }

    if (source == TimeSourceKind::SYSTEM_NTP) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, SYSTEM_TIME_READ_TIMEOUT_MS)) {
            st.hour = timeinfo.tm_hour;
            st.minute = timeinfo.tm_min;
            st.second = timeinfo.tm_sec;
            st.is_valid = true;
            last_source_ = TimeSourceKind::SYSTEM_NTP;
            ESP_LOGD(TAG, "System time source: Priority 2 (ESP-IDF System Clock / NTP)");
            return st;
        }
    }

    st.is_valid = false;
    last_source_ = TimeSourceKind::INVALID;
    ESP_LOGW(TAG, "System time source: Priority 3 (INVALID)");
    return st;
}

bool RtcManager::isNightMode() {
    SystemTime st = getTime();
    if (!st.is_valid) {
        ESP_LOGW(TAG, "System time invalid. Safe-state fallback: DAY mode active");
        return false;
    }
    bool night = (st.hour >= NIGHT_START_HOUR || st.hour < DAY_START_HOUR);
    ESP_LOGD(TAG, "Current time %02d:%02d:%02d -> Mode: %s",
             st.hour, st.minute, st.second, night ? "NIGHT" : "DAY");
    return night;
}

bool RtcManager::hasPowerLoss() const {
    if (!rtc_initialized_) return true;
    return !const_cast<RTC_DS1307&>(rtc_).isrunning();
}

bool RtcManager::isBackendTimeAuthoritative(uint32_t now_ms) const {
    if (backend_time_applied_ms_ == 0) return false;
    if (now_ms < backend_time_applied_ms_) return false;
    return (now_ms - backend_time_applied_ms_) < CLOCK_BACKEND_SUPPRESS_NTP_MS;
}

TimeTelemetry RtcManager::getTimeTelemetry() const {
    TimeTelemetry t{};
    t.rtc_valid = rtc_initialized_ && rtc_time_trusted_;
    t.last_sync_unix_time_utc = last_sync_unix_time_utc_;
    t.source = last_source_;
    t.ntp_synced = (last_source_ == TimeSourceKind::SYSTEM_NTP);
    return t;
}

#endif // ESP_PLATFORM || ARDUINO
