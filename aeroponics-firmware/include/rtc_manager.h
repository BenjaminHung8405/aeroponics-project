#pragma once

#include <cstdint>
#include "config.h"
#include "core/IClock.h"
#include "core/clock_trust.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <RTClib.h>
#endif

/**
 * @brief Adapter class wrapping the MKE-M09 DS1307 hardware RTC and ESP-IDF
 * system time. Implements the IClock core interface.
 *
 * Trust hierarchy is strictly (see ClockTrustResolver):
 *   1. trusted DS1307 hardware clock
 *   2. ESP-IDF system time (SNTP)
 *   3. invalid -> GroupScheduler forces safe-OFF
 *
 * Time never becomes trusted merely by being read: only an explicit
 * adjustTime() (backend GATEWAY_CLOCK or NTP correction) makes it a reference.
 */
class RtcManager : public IClock, public ITimeTelemetry {
public:
    RtcManager();
    ~RtcManager() override;

    /**
     * @brief Initialize the I2C interface and communicate with the DS1307 RTC.
     * Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN) must already have run.
     * @return true if the DS1307 responds on the bus, false otherwise.
     */
    bool begin();

    /**
     * @brief Synchronize time from NTP and correct DS1307 drift.
     *
     * Unlike a first-boot-only sync this rewrites the hardware clock on every
     * successful call, so it doubles as the bounded drift-compensation path.
     * @return true if NTP sync succeeded and the RTC was updated.
     */
    bool syncFromNtp();

    /**
     * @brief Apply an authoritative wall-clock value and mark time trusted.
     * @param unix_time_utc UTC epoch seconds to set the DS1307 to.
     * @param source Provenance of the value; BACKEND suppresses NTP override.
     * @return true if the hardware clock accepted the write.
     */
    bool adjustTimeFromUnix(int64_t unix_time_utc,
                            TimeSourceKind source = TimeSourceKind::BACKEND);

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    /**
     * @brief Manually adjust time on the DS1307 and mark RTC time as trusted.
     * @param dt DateTime object to set the RTC clock to.
     * @return true if the hardware clock accepted the write.
     */
    bool adjustTime(const DateTime& dt);
#endif

    /**
     * @brief Retrieve current time with the fallback hierarchy documented on
     * the class. Does not mutate trust state.
     * @return SystemTime snapshot.
     */
    SystemTime getTime() override;

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

    /** Last successful sync epoch in UTC seconds, or 0 if never synced. */
    int64_t lastSyncUnixTimeUtc() const { return last_sync_unix_time_utc_; }

private:
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    RTC_DS1307 rtc_;
#endif
    bool rtc_initialized_;
    bool rtc_time_trusted_;
    bool ntp_synced_;
    TimeSourceKind last_source_;
    int64_t last_sync_unix_time_utc_;
    uint32_t backend_time_applied_ms_;

    bool isPlausibleEpoch(int64_t unix_time_utc) const;
    bool applyUnixToHardware(int64_t unix_time_utc, TimeSourceKind source);
    ClockTrustInputs collectTrustInputs() const;
    bool systemTimeIsValid() const;
    static uint32_t monotonicMillis();
};
