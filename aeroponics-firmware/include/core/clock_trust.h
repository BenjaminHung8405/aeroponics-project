#pragma once

#include <cstdint>

/**
 * @brief Observable time source, published verbatim in the MQTT heartbeat as
 * `time_source` so the backend can tell which reference the gateway trusted.
 */
enum class TimeSourceKind : uint8_t {
    UNKNOWN = 0,
    DS1307_RTC,
    SYSTEM_NTP,
    BACKEND,
    INVALID
};

/** Stable wire token for a time source (never localized, never reordered). */
const char* timeSourceKindToString(TimeSourceKind kind);

/** Parse a wire token back into a kind; returns UNKNOWN for anything else. */
TimeSourceKind timeSourceKindFromString(const char* token);

/**
 * @brief Snapshot of every fact the fallback hierarchy is allowed to consult.
 * RtcManager fills these in from hardware/system state; the resolver itself is
 * pure so the trust contract can be regression-tested on the host build
 * without a DS1307 on the bus.
 */
struct ClockTrustInputs {
    // True when RtcManager regards the DS1307 as a valid reference, i.e. the
    // chip answered on I2C, the oscillator is running (no lostPower), and the
    // register contents were either continued from a battery-backed run or
    // written this boot by an authoritative adjustTime().
    bool hardware_trusted = false;
    // True when the ESP-IDF/SNTP system clock holds a plausible epoch.
    bool system_time_valid = false;
    // Provenance of the most recent authoritative write (telemetry only).
    TimeSourceKind last_applied_source = TimeSourceKind::UNKNOWN;
    int64_t last_sync_unix_time_utc = 0;
};

/**
 * @brief Pure fallback hierarchy for gateway time.
 *
 * Contract (DS1307 migration plan):
 *   1. trusted DS1307 hardware clock
 *   2. ESP-IDF system time (SNTP)
 *   3. invalid -> GroupScheduler::validateRuntimeClock forces safe-OFF
 */
class ClockTrustResolver {
public:
    static bool isHardwareTrusted(const ClockTrustInputs& in);

    /**
     * @brief Resolve the active reference. Priority 1 beats Priority 2, and
     * anything else resolves to INVALID (never a guessed value).
     */
    static TimeSourceKind resolve(const ClockTrustInputs& in);

    /** True when automatic schedules may run on the resolved time. */
    static bool hasAnyValidTime(const ClockTrustInputs& in);
};

/**
 * @brief Read-only telemetry the MQTT facade needs about the active clock.
 * Kept separate from IClock so the scheduler keeps depending only on the
 * narrow getTime()/isNightMode() contract.
 */
struct TimeTelemetry {
    bool ntp_synced = false;
    bool rtc_valid = false;
    TimeSourceKind source = TimeSourceKind::UNKNOWN;
    int64_t last_sync_unix_time_utc = 0;
};

class ITimeTelemetry {
public:
    virtual ~ITimeTelemetry() = default;
    virtual TimeTelemetry getTimeTelemetry() const = 0;
};
