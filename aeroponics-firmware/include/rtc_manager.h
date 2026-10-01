#pragma once

#include <cstdint>
#include "config.h"
#include "core/IClock.h"
#include "core/clock_trust.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <RTClib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#endif

#ifdef UNIT_TEST_HOST
#include <functional>
#include <utility>
#endif

/**
 * @brief Adapter class wrapping the MKE-M09 DS1307 hardware RTC and ESP-IDF
 * system time with cached POSIX clock and periodic drift compensation.
 *
 * Clock architecture:
 *   - Runtime reads use cached POSIX system time (time(nullptr) / gettimeofday)
 *   - DS1307 seeds POSIX once during begin() when oscillator is running
 *   - Six-hour background task compensates drift by comparing DS1307 to POSIX
 *   - NTP/backend updates write both POSIX and DS1307 when available
 *
 * Trust hierarchy (see ClockTrustResolver):
 *   1. Trusted DS1307 hardware clock (seeds POSIX at startup)
 *   2. ESP-IDF system time (SNTP or backend)
 *   3. invalid -> GroupScheduler forces safe-OFF
 *
 * getTime() and getUtcEpochSeconds() perform zero I2C operations after begin().
 */
class RtcManager : public IClock, public ITimeTelemetry {
public:
    RtcManager();
#ifdef UNIT_TEST_HOST
    explicit RtcManager(std::function<bool()> fake_begin,
                       std::function<bool()> fake_isrunning,
                       std::function<int64_t()> fake_now,
                       std::function<void(int64_t)> fake_adjust);
#endif
    ~RtcManager() override;

    /**
     * @brief Initialize DS1307, perform one hardware time read, seed POSIX
     * system clock, and start the drift compensation task.
     *
     * Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN) must already have run on ESP32.
     *
     * @return true if DS1307 responds and is initialized; POSIX seeding and
     *         task start failures are logged but do not fail this call.
     */
    bool begin();

    /**
     * @brief Synchronize time from NTP and correct DS1307 drift.
     *
     * Updates POSIX system clock via configTime/getLocalTime, then writes
     * DS1307 hardware when available. Authority metadata prevents the six-hour
     * task from immediately overwriting this reference.
     *
     * @return true if NTP sync succeeded and the RTC was updated.
     */
    bool syncFromNtp();

    /**
     * @brief Apply an authoritative wall-clock value and mark time trusted.
     *
     * Updates POSIX system clock first, then DS1307 hardware when available.
     *
     * @param unix_time_utc UTC epoch seconds to set the DS1307 to.
     * @param source Provenance of the value; BACKEND suppresses NTP override.
     * @return true if the hardware clock accepted the write.
     */
    bool adjustTimeFromUnix(int64_t unix_time_utc,
                            TimeSourceKind source = TimeSourceKind::BACKEND);

    /**
     * @brief Apply an authoritative UTC epoch from the backend and synchronize
     *        both the POSIX system clock and the DS1307 hardware RTC when
     *        available.
     *
     * The POSIX system clock is always updated first so that schedule
     * calculations remain correct even when the DS1307 is missing or faulty.
     *
     * @param unix_time_utc UTC epoch seconds to apply.
     * @param tz_offset_s   Timezone offset reported by the backend.
     * @return true when at least the POSIX clock was updated successfully.
     */
    bool applyUtcClockFromBackend(int64_t unix_time_utc, int32_t tz_offset_s);

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    /**
     * @brief Manually adjust POSIX system clock and DS1307, mark RTC trusted.
     *
     * @param dt DateTime object to set the RTC clock to.
     * @return true if the hardware clock accepted the write.
     */
    bool adjustTime(const DateTime& dt);
#endif

    /**
     * @brief Retrieve current time from cached POSIX system clock only.
     *
     * Performs zero I2C operations. Reads time(nullptr) / gettimeofday and
     * converts to local SystemTime. Returns invalid when POSIX clock is not
     * seeded or plausible.
     *
     * @return SystemTime snapshot.
     */
    SystemTime getTime() override;

    /**
     * @brief Retrieve current UTC epoch seconds from cached POSIX system clock.
     *
     * Performs zero I2C operations. Returns POSIX time(nullptr) directly when
     * the system clock is valid and plausible.
     *
     * @return UTC epoch seconds, or 0 when invalid/unseeded.
     */
    int64_t getUtcEpochSeconds() const;

    /**
     * @brief Check if current time is within the Night Mode window.
     * Rule S1-RTC-04: invalid time MUST return false (DAY mode) as fail-safe.
     * @return true if Night Mode, false if Day Mode (or fail-safe fallback).
     */
    bool isNightMode() override;

    /**
     * @brief True while the DS1307 oscillator-stop bit is set, meaning the
     * retained register contents are not a valid continuation of real time.
     */
    bool hasPowerLoss() const;

    /**
     * @brief True while the persisted backend reference is newer than the
     * current monotonic boot reference, so SNTP must not override it yet.
     */
    bool isBackendTimeAuthoritative(uint32_t now_ms) const;

    /**
     * @brief Read-only view of clock provenance for MQTT telemetry.
     */
    TimeTelemetry getTimeTelemetry() const override;

    /** True when the DS1307 was detected on I2C during begin(). */
    bool isHardwarePresent() const { return rtc_initialized_; }

    /** True when the last backend clock apply successfully updated the POSIX system clock. */
    bool isSystemClockUpdatedFromBackend() const { return system_clock_updated_from_backend_; }

    /** Last successful sync epoch in UTC seconds, or 0 if never synced. */
    int64_t lastSyncUnixTimeUtc() const { return last_sync_unix_time_utc_; }

#ifdef UNIT_TEST_HOST
    /** Test-only: retrieve the count of hardware now() calls made. */
    uint32_t getHardwareReadCount() const { return hardware_read_count_; }

    /** Test-only: trigger one drift compensation cycle immediately. */
    void triggerDriftCompensationCycle();
#endif

private:
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    // Drift compensation task constants
    static constexpr uint32_t DRIFT_COMPENSATION_PERIOD_MS = 6UL * 60UL * 60UL * 1000UL; // 6 hours
    static constexpr TickType_t DRIFT_COMPENSATION_TICKS = 
        (static_cast<uint64_t>(DRIFT_COMPENSATION_PERIOD_MS) * configTICK_RATE_HZ) / 1000ULL;
    static constexpr uint32_t DRIFT_COMPENSATION_STACK_SIZE = 3072;
    static constexpr UBaseType_t DRIFT_COMPENSATION_PRIORITY = 1;
    static constexpr BaseType_t DRIFT_COMPENSATION_CORE = 0;
    static constexpr TickType_t HARDWARE_LOCK_TIMEOUT_MS = 1000;
#endif

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    RTC_DS1307 rtc_;
    TaskHandle_t drift_task_handle_;
    SemaphoreHandle_t hardware_mutex_;
    bool drift_task_started_;
#endif

    bool rtc_initialized_;
    bool rtc_time_trusted_;
    bool ntp_synced_;
    TimeSourceKind last_source_;
    int64_t last_sync_unix_time_utc_;
    uint32_t backend_time_applied_ms_;
    int64_t last_ntp_or_backend_applied_utc_;
    uint32_t last_authoritative_update_ms_;

    bool isPlausibleEpoch(int64_t unix_time_utc) const;
    bool applyUnixToHardware(int64_t unix_time_utc, TimeSourceKind source);
    ClockTrustInputs collectTrustInputs() const;
    bool systemTimeIsValid() const;
    static uint32_t monotonicMillis();

    bool setPosixSystemClockFromUtc(int64_t unix_time_utc);
    bool epochToLocalSystemTime(int64_t epoch_utc, SystemTime& out) const;

    bool seedPosixClockFromHardware();
    bool readHardwareUtcEpochLocked(int64_t& out_epoch_utc);
    void startDriftCompensationTask();
    void stopDriftCompensationTask();
    void driftCompensationLoop();
    bool shouldApplyDriftCorrection(int64_t hardware_utc, int64_t posix_utc) const;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    static void driftCompensationTaskTrampoline(void* param);
#endif

#ifdef UNIT_TEST_HOST
    std::function<bool()> fake_begin_;
    std::function<bool()> fake_isrunning_;
    std::function<int64_t()> fake_now_;
    std::function<void(int64_t)> fake_adjust_;
    mutable uint32_t hardware_read_count_;
    mutable int64_t fake_posix_epoch_;
#endif

    bool system_clock_updated_from_backend_ = false;
    int64_t last_backend_applied_utc_ = 0;
};
