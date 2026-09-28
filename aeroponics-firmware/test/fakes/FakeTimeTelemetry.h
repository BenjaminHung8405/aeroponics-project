#pragma once

#include "core/IClock.h"
#include "core/clock_trust.h"

/**
 * @brief Host-test ITimeTelemetry stand-in. RtcManager is hardware-only, so
 * tests drive the heartbeat's time_source / last_sync telemetry through this
 * fake to assert the MQTT contract without an I2C bus.
 */
class FakeTimeTelemetry : public ITimeTelemetry {
public:
    TimeTelemetry telemetry{};

    FakeTimeTelemetry() = default;
    explicit FakeTimeTelemetry(const TimeTelemetry& initial) : telemetry(initial) {}

    void setSource(TimeSourceKind source) { telemetry.source = source; }
    void setRtcValid(bool valid) { telemetry.rtc_valid = valid; }
    void setNtpSynced(bool synced) { telemetry.ntp_synced = synced; }
    void setLastSyncUnixTimeUtc(int64_t value) { telemetry.last_sync_unix_time_utc = value; }

    TimeTelemetry getTimeTelemetry() const override { return telemetry; }
};
