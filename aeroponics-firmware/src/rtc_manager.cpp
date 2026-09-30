#include "rtc_manager.h"

#include <chrono>
#include <ctime>
#include <cstdlib>
#include <sys/time.h>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <Wire.h>
#include <time.h>
#include <sys/time.h>
#include <cerrno>
#include <esp_log.h>
#include <Arduino.h>

static const char *TAG = "RTC_MANAGER";
#endif

RtcManager::RtcManager()
    :
#if defined(ESP_PLATFORM) || defined(ARDUINO)
      drift_task_handle_(nullptr),
      hardware_mutex_(nullptr),
      drift_task_started_(false),
#endif
      rtc_initialized_(false),
      rtc_time_trusted_(false),
      ntp_synced_(false),
      last_source_(TimeSourceKind::UNKNOWN),
      last_sync_unix_time_utc_(0),
      backend_time_applied_ms_(0),
      last_ntp_or_backend_applied_utc_(0),
      last_authoritative_update_ms_(0),
#ifdef UNIT_TEST_HOST
      hardware_read_count_(0),
      fake_posix_epoch_(0),
#endif
      system_clock_updated_from_backend_(false),
      last_backend_applied_utc_(0) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    hardware_mutex_ = xSemaphoreCreateMutex();
#endif
}

#ifdef UNIT_TEST_HOST
RtcManager::RtcManager(std::function<bool()> fake_begin,
                       std::function<bool()> fake_isrunning,
                       std::function<int64_t()> fake_now,
                       std::function<void(int64_t)> fake_adjust)
    : RtcManager() {
    fake_begin_ = std::move(fake_begin);
    fake_isrunning_ = std::move(fake_isrunning);
    fake_now_ = std::move(fake_now);
    fake_adjust_ = std::move(fake_adjust);
}
#endif

RtcManager::~RtcManager() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    stopDriftCompensationTask();
    if (hardware_mutex_ != nullptr) {
        vSemaphoreDelete(hardware_mutex_);
        hardware_mutex_ = nullptr;
    }
#endif
}

bool RtcManager::isPlausibleEpoch(int64_t unix_time_utc) const {
    return unix_time_utc >= CLOCK_UNIX_TIME_MIN_VALID &&
           unix_time_utc <= CLOCK_UNIX_TIME_MAX_VALID;
}

uint32_t RtcManager::monotonicMillis() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    return millis();
#else
    using namespace std::chrono;
    static const auto start_time = steady_clock::now();
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - start_time).count());
#endif
}

bool RtcManager::systemTimeIsValid() const {
    const time_t now = time(nullptr);
    return now >= static_cast<time_t>(CLOCK_UNIX_TIME_MIN_VALID) &&
           now <= static_cast<time_t>(CLOCK_UNIX_TIME_MAX_VALID);
}

bool RtcManager::setPosixSystemClockFromUtc(int64_t unix_time_utc) {
    if (!isPlausibleEpoch(unix_time_utc)) return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    struct timeval tv{};
    tv.tv_sec = static_cast<time_t>(unix_time_utc);
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) != 0) {
        ESP_LOGE(TAG, "Failed to set POSIX system clock to %lld UTC (errno=%d).",
                 static_cast<long long>(unix_time_utc), errno);
        return false;
    }
#else
#ifdef UNIT_TEST_HOST
    fake_posix_epoch_ = unix_time_utc;
#else
    struct timeval tv{};
    tv.tv_sec = static_cast<time_t>(unix_time_utc);
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) != 0) return false;
#endif
#endif
#ifdef UNIT_TEST_HOST
    if (fake_posix_epoch_ != 0) return true;
#endif
    const int64_t applied = static_cast<int64_t>(time(nullptr));
    return applied >= CLOCK_UNIX_TIME_MIN_VALID && applied - unix_time_utc >= -1 &&
           applied - unix_time_utc <= 1;
}

bool RtcManager::readHardwareUtcEpochLocked(int64_t& out_epoch_utc) {
    if (!rtc_initialized_) return false;
#ifdef UNIT_TEST_HOST
    if (fake_now_) {
        ++hardware_read_count_;
        out_epoch_utc = fake_now_();
        return isPlausibleEpoch(out_epoch_utc);
    }
#endif
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    const DateTime now = rtc_.now();
    if (now.year() < 2020 || now.year() > 2099) return false;
    out_epoch_utc = static_cast<int64_t>(now.unixtime());
    return isPlausibleEpoch(out_epoch_utc);
#else
    (void)out_epoch_utc;
    return false;
#endif
}

bool RtcManager::seedPosixClockFromHardware() {
    int64_t hardware_epoch_utc = 0;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (hardware_mutex_ == nullptr ||
        xSemaphoreTake(hardware_mutex_, pdMS_TO_TICKS(HARDWARE_LOCK_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Unable to lock DS1307 for startup read.");
        return false;
    }
    const bool valid = readHardwareUtcEpochLocked(hardware_epoch_utc);
    xSemaphoreGive(hardware_mutex_);
#else
    const bool valid = readHardwareUtcEpochLocked(hardware_epoch_utc);
#endif
    if (!valid || !setPosixSystemClockFromUtc(hardware_epoch_utc)) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        ESP_LOGW(TAG, "DS1307 startup time invalid or POSIX seed failed.");
#endif
        return false;
    }
    rtc_time_trusted_ = true;
    last_source_ = TimeSourceKind::DS1307_RTC;
    last_sync_unix_time_utc_ = hardware_epoch_utc;
    return true;
}

bool RtcManager::applyUnixToHardware(int64_t unix_time_utc, TimeSourceKind source) {
    if (!rtc_initialized_ || !isPlausibleEpoch(unix_time_utc)) return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    const time_t epoch = static_cast<time_t>(unix_time_utc);
    struct tm timeinfo{};
    if (gmtime_r(&epoch, &timeinfo) == nullptr) return false;
    const DateTime dt(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    if (hardware_mutex_ == nullptr ||
        xSemaphoreTake(hardware_mutex_, pdMS_TO_TICKS(HARDWARE_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }
    rtc_.adjust(dt);
    xSemaphoreGive(hardware_mutex_);
#elif defined(UNIT_TEST_HOST)
    if (fake_adjust_) fake_adjust_(unix_time_utc);
#else
    return false;
#endif
    rtc_time_trusted_ = true;
    last_source_ = source;
    last_sync_unix_time_utc_ = unix_time_utc;
    if (source == TimeSourceKind::BACKEND) backend_time_applied_ms_ = monotonicMillis();
    return true;
}

bool RtcManager::epochToLocalSystemTime(int64_t epoch_utc, SystemTime& out) const {
    if (!isPlausibleEpoch(epoch_utc)) return false;
    const time_t epoch = static_cast<time_t>(epoch_utc);
    struct tm local_info{};
    if (localtime_r(&epoch, &local_info) == nullptr) return false;
    out.hour = static_cast<uint8_t>(local_info.tm_hour);
    out.minute = static_cast<uint8_t>(local_info.tm_min);
    out.second = static_cast<uint8_t>(local_info.tm_sec);
    out.is_valid = true;
    return true;
}

int64_t RtcManager::getUtcEpochSeconds() const {
#ifdef UNIT_TEST_HOST
    if (fake_begin_) return isPlausibleEpoch(fake_posix_epoch_) ? fake_posix_epoch_ : 0;
#endif
    const int64_t now = static_cast<int64_t>(time(nullptr));
    return isPlausibleEpoch(now) ? now : 0;
}

ClockTrustInputs RtcManager::collectTrustInputs() const {
    ClockTrustInputs inputs{};
    inputs.hardware_trusted = rtc_initialized_ && rtc_time_trusted_;
    inputs.system_time_valid = systemTimeIsValid();
    inputs.last_applied_source = last_source_;
    inputs.last_sync_unix_time_utc = last_sync_unix_time_utc_;
    return inputs;
}

bool RtcManager::begin() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (hardware_mutex_ == nullptr ||
        xSemaphoreTake(hardware_mutex_, pdMS_TO_TICKS(HARDWARE_LOCK_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Unable to lock DS1307 during initialization.");
        return false;
    }
    const bool detected = rtc_.begin();
    const bool running = detected && rtc_.isrunning();
    xSemaphoreGive(hardware_mutex_);
#elif defined(UNIT_TEST_HOST)
    const bool detected = fake_begin_ ? fake_begin_() : false;
    const bool running = detected && (!fake_isrunning_ || fake_isrunning_());
#else
    const bool detected = false;
    const bool running = false;
#endif
    if (!detected) {
        rtc_initialized_ = false;
        rtc_time_trusted_ = false;
        last_source_ = TimeSourceKind::UNKNOWN;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        ESP_LOGE(TAG, "Couldn't find DS1307 RTC hardware module on I2C bus!");
#endif
        return false;
    }
    rtc_initialized_ = true;
    if (!running) {
        rtc_time_trusted_ = false;
        last_source_ = TimeSourceKind::UNKNOWN;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        ESP_LOGW(TAG, "DS1307 oscillator stopped or coin-cell missing; time is untrusted until set.");
#endif
        return true;
    }

    const bool seeded = seedPosixClockFromHardware();
    if (!seeded) {
        rtc_time_trusted_ = false;
        last_source_ = TimeSourceKind::UNKNOWN;
    }
    startDriftCompensationTask();
    return true;
}

bool RtcManager::adjustTimeFromUnix(int64_t unix_time_utc, TimeSourceKind source) {
    if (!setPosixSystemClockFromUtc(unix_time_utc)) return false;
    last_authoritative_update_ms_ = monotonicMillis();
    last_ntp_or_backend_applied_utc_ = unix_time_utc;
    system_clock_updated_from_backend_ = source == TimeSourceKind::BACKEND;
    if (!applyUnixToHardware(unix_time_utc, source)) return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    ESP_LOGI(TAG, "DS1307 adjusted from %s to %lld UTC.",
             timeSourceKindToString(source), static_cast<long long>(unix_time_utc));
#endif
    return true;
}

bool RtcManager::applyUtcClockFromBackend(int64_t unix_time_utc, int32_t tz_offset_s) {
    if (!isPlausibleEpoch(unix_time_utc)) return false;
    setenv("TZ", "ICT-7", 1);
    tzset();
    if (!setPosixSystemClockFromUtc(unix_time_utc)) return false;
    system_clock_updated_from_backend_ = true;
    last_backend_applied_utc_ = unix_time_utc;
    last_source_ = TimeSourceKind::BACKEND;
    last_sync_unix_time_utc_ = unix_time_utc;
    backend_time_applied_ms_ = monotonicMillis();
    last_authoritative_update_ms_ = backend_time_applied_ms_;
    last_ntp_or_backend_applied_utc_ = unix_time_utc;
    const bool hardware_updated = applyUnixToHardware(unix_time_utc, TimeSourceKind::BACKEND);
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    ESP_LOGI(TAG, "Backend clock applied: epoch=%lld UTC, tz=%d, DS1307=%s.",
             static_cast<long long>(unix_time_utc), static_cast<int>(tz_offset_s),
             hardware_updated ? "updated" : "unavailable");
#else
    (void)tz_offset_s;
#endif
    return true;
}

#if defined(ESP_PLATFORM) || defined(ARDUINO)
bool RtcManager::adjustTime(const DateTime& dt) {
    const int64_t epoch = static_cast<int64_t>(dt.unixtime());
    if (!setPosixSystemClockFromUtc(epoch) || !applyUnixToHardware(epoch, TimeSourceKind::BACKEND)) return false;
    last_authoritative_update_ms_ = monotonicMillis();
    last_ntp_or_backend_applied_utc_ = epoch;
    ESP_LOGI(TAG, "DS1307 manually adjusted to %lld UTC", static_cast<long long>(epoch));
    return true;
}
#endif

bool RtcManager::syncFromNtp() {
#if !defined(ESP_PLATFORM) && !defined(ARDUINO)
    return false;
#else
    ESP_LOGI(TAG, "Initiating NTP time synchronization (Server: %s, Offset: %d s)...",
             NTP_SERVER_PRIMARY, TIMEZONE_OFFSET_S);
    configTime(TIMEZONE_OFFSET_S, DAYLIGHT_OFFSET_S, NTP_SERVER_PRIMARY);
    struct tm timeinfo{};
    uint32_t polled_ms = 0;
    while (polled_ms < NTP_SYNC_TIMEOUT_MS) {
        if (getLocalTime(&timeinfo, NTP_POLL_INTERVAL_MS)) {
            const time_t epoch_utc = static_cast<time_t>(mktime(&timeinfo) - TIMEZONE_OFFSET_S);
            const int64_t epoch = static_cast<int64_t>(epoch_utc);
            if (!setPosixSystemClockFromUtc(epoch)) return false;
            ntp_synced_ = true;
            last_authoritative_update_ms_ = monotonicMillis();
            last_ntp_or_backend_applied_utc_ = epoch;
            last_source_ = TimeSourceKind::SYSTEM_NTP;
            last_sync_unix_time_utc_ = epoch;
            applyUnixToHardware(epoch, TimeSourceKind::SYSTEM_NTP);
            return true;
        }
        polled_ms += NTP_POLL_INTERVAL_MS;
    }
    ESP_LOGW(TAG, "NTP synchronization timed out after %u ms.", NTP_SYNC_TIMEOUT_MS);
    return false;
#endif
}

SystemTime RtcManager::getTime() {
    SystemTime st{0, 0, 0, false};
    const int64_t now_utc = getUtcEpochSeconds();
    if (!epochToLocalSystemTime(now_utc, st)) {
        last_source_ = TimeSourceKind::INVALID;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        ESP_LOGW(TAG, "System time source: INVALID");
#endif
        return st;
    }
    if (last_source_ == TimeSourceKind::UNKNOWN || last_source_ == TimeSourceKind::INVALID) {
        last_source_ = rtc_time_trusted_ ? TimeSourceKind::DS1307_RTC : TimeSourceKind::SYSTEM_NTP;
    }
    return st;
}

bool RtcManager::isNightMode() {
    const SystemTime st = getTime();
    if (!st.is_valid) return false;
    return st.hour >= NIGHT_START_HOUR || st.hour < DAY_START_HOUR;
}

bool RtcManager::hasPowerLoss() const {
    if (!rtc_initialized_) return true;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (hardware_mutex_ == nullptr ||
        xSemaphoreTake(hardware_mutex_, pdMS_TO_TICKS(HARDWARE_LOCK_TIMEOUT_MS)) != pdTRUE) return true;
    const bool running = const_cast<RTC_DS1307&>(rtc_).isrunning();
    xSemaphoreGive(hardware_mutex_);
    return !running;
#elif defined(UNIT_TEST_HOST)
    return fake_isrunning_ ? !fake_isrunning_() : true;
#else
    return true;
#endif
}

bool RtcManager::isBackendTimeAuthoritative(uint32_t now_ms) const {
    if (backend_time_applied_ms_ == 0 || now_ms < backend_time_applied_ms_) return false;
    return (now_ms - backend_time_applied_ms_) < CLOCK_BACKEND_SUPPRESS_NTP_MS;
}

TimeTelemetry RtcManager::getTimeTelemetry() const {
    TimeTelemetry t{};
    t.rtc_valid = rtc_initialized_ && rtc_time_trusted_;
    t.last_sync_unix_time_utc = last_sync_unix_time_utc_;
    t.source = last_source_;
    t.ntp_synced = ntp_synced_;
    return t;
}

bool RtcManager::shouldApplyDriftCorrection(int64_t hardware_utc, int64_t posix_utc) const {
    if (!isPlausibleEpoch(hardware_utc) || !isPlausibleEpoch(posix_utc)) return false;
    if (last_authoritative_update_ms_ == 0) return true;
    const uint32_t elapsed = monotonicMillis() - last_authoritative_update_ms_;
    return elapsed >= CLOCK_BACKEND_SUPPRESS_NTP_MS;
}

void RtcManager::driftCompensationLoop() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(DRIFT_COMPENSATION_PERIOD_MS));
        if (!drift_task_started_) break;
        int64_t hardware_utc = 0;
        if (hardware_mutex_ == nullptr ||
            xSemaphoreTake(hardware_mutex_, pdMS_TO_TICKS(HARDWARE_LOCK_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGW(TAG, "Drift compensation could not lock DS1307.");
            continue;
        }
        const bool valid = readHardwareUtcEpochLocked(hardware_utc);
        xSemaphoreGive(hardware_mutex_);
        if (!valid) {
            ESP_LOGW(TAG, "Drift compensation received invalid DS1307 time.");
            continue;
        }
        const int64_t posix_utc = getUtcEpochSeconds();
        if (!shouldApplyDriftCorrection(hardware_utc, posix_utc)) continue;
        if (setPosixSystemClockFromUtc(hardware_utc)) {
            ESP_LOGI(TAG, "DS1307 drift correction applied: delta=%lld seconds.",
                     static_cast<long long>(hardware_utc - posix_utc));
        }
    }
#endif
}

void RtcManager::startDriftCompensationTask() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (drift_task_started_ || !rtc_initialized_ || !rtc_time_trusted_) return;
    drift_task_started_ = xTaskCreatePinnedToCore(
        driftCompensationTaskTrampoline, "rtc_drift", DRIFT_COMPENSATION_STACK_SIZE,
        this, DRIFT_COMPENSATION_PRIORITY, &drift_task_handle_, DRIFT_COMPENSATION_CORE) == pdPASS;
    if (!drift_task_started_) {
        ESP_LOGW(TAG, "Failed to start RTC drift compensation task; cached clock remains active.");
        drift_task_handle_ = nullptr;
    }
#endif
}

void RtcManager::stopDriftCompensationTask() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (!drift_task_started_) return;
    drift_task_started_ = false;
    if (drift_task_handle_ != nullptr) {
        vTaskDelete(drift_task_handle_);
        drift_task_handle_ = nullptr;
    }
#endif
}

#if defined(ESP_PLATFORM) || defined(ARDUINO)
void RtcManager::driftCompensationTaskTrampoline(void* param) {
    static_cast<RtcManager*>(param)->driftCompensationLoop();
    vTaskDelete(nullptr);
}
#endif

#ifdef UNIT_TEST_HOST
void RtcManager::triggerDriftCompensationCycle() {
    int64_t hardware_utc = 0;
    if (!readHardwareUtcEpochLocked(hardware_utc)) return;
    const int64_t posix_utc = getUtcEpochSeconds();
    if (shouldApplyDriftCorrection(hardware_utc, posix_utc)) setPosixSystemClockFromUtc(hardware_utc);
}
#endif
