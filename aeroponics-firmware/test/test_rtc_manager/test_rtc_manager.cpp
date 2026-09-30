#include <unity.h>
#include <map>
#include <string>
#include <vector>

#include "config.h"
#include "nvs_storage.h"
#include "fakes/FakeNvsBackend.h"
#include "core/clock_trust.h"
#include "fakes/FakeTimeTelemetry.h"
#include "rtc_manager.h"

class FakeRtcScript {
public:
    bool begin() const { return begin_result; }
    bool isrunning() const { return running; }
    int64_t now() {
        ++now_calls;
        return now_epoch;
    }
    void adjust(int64_t epoch) {
        adjusted_epochs.push_back(epoch);
        now_epoch = epoch;
        running = true;
    }

    bool begin_result = true;
    bool running = true;
    int64_t now_epoch = 1700000000LL;
    uint32_t now_calls = 0;
    std::vector<int64_t> adjusted_epochs;
};

void test_rtc_manager_begin_seeds_cached_posix_and_reads_hardware_once(void) {
    FakeRtcScript fake;
    RtcManager manager(
        [&fake]() { return fake.begin(); },
        [&fake]() { return fake.isrunning(); },
        [&fake]() { return fake.now(); },
        [&fake](int64_t epoch) { fake.adjust(epoch); });

    TEST_ASSERT_TRUE(manager.begin());
    TEST_ASSERT_EQUAL_UINT32(1, fake.now_calls);
    TEST_ASSERT_EQUAL_INT64(fake.now_epoch, manager.getUtcEpochSeconds());
    TEST_ASSERT_TRUE(manager.getTime().is_valid);
    TEST_ASSERT_TRUE(manager.getTime().is_valid);
    TEST_ASSERT_EQUAL_UINT32(1, fake.now_calls);
}

void test_rtc_manager_invalid_rtc_does_not_seed_or_read_on_cached_queries(void) {
    FakeRtcScript fake;
    fake.now_epoch = 0;
    RtcManager manager(
        [&fake]() { return fake.begin(); },
        [&fake]() { return fake.isrunning(); },
        [&fake]() { return fake.now(); },
        [&fake](int64_t epoch) { fake.adjust(epoch); });

    TEST_ASSERT_TRUE(manager.begin());
    TEST_ASSERT_EQUAL_UINT32(1, fake.now_calls);
    TEST_ASSERT_EQUAL_INT64(0, manager.getUtcEpochSeconds());
    TEST_ASSERT_FALSE(manager.getTime().is_valid);
    TEST_ASSERT_EQUAL_UINT32(1, fake.now_calls);
}

void test_rtc_manager_backend_updates_cached_clock_and_hardware(void) {
    FakeRtcScript fake;
    RtcManager manager(
        [&fake]() { return fake.begin(); },
        [&fake]() { return fake.isrunning(); },
        [&fake]() { return fake.now(); },
        [&fake](int64_t epoch) { fake.adjust(epoch); });

    TEST_ASSERT_TRUE(manager.begin());
    const uint32_t reads_before = fake.now_calls;
    TEST_ASSERT_TRUE(manager.applyUtcClockFromBackend(1710000000LL, 0));
    TEST_ASSERT_EQUAL_INT64(1710000000LL, manager.getUtcEpochSeconds());
    TEST_ASSERT_EQUAL_UINT32(reads_before, fake.now_calls);
    TEST_ASSERT_FALSE(fake.adjusted_epochs.empty());
    TEST_ASSERT_EQUAL_INT64(1710000000LL, fake.adjusted_epochs.back());
}

void test_rtc_manager_drift_compensation_reads_once_and_corrects_cached_clock(void) {
    FakeRtcScript fake;
    RtcManager manager(
        [&fake]() { return fake.begin(); },
        [&fake]() { return fake.isrunning(); },
        [&fake]() { return fake.now(); },
        [&fake](int64_t epoch) { fake.adjust(epoch); });

    TEST_ASSERT_TRUE(manager.begin());
    fake.now_epoch = 1700000100LL;
    const uint32_t reads_before = fake.now_calls;
    manager.triggerDriftCompensationCycle();
    TEST_ASSERT_EQUAL_UINT32(reads_before + 1, fake.now_calls);
    TEST_ASSERT_EQUAL_INT64(1700000100LL, manager.getUtcEpochSeconds());
}

class ScriptedNvsBackend final : public INvsBackend {
public:
    explicit ScriptedNvsBackend(bool ready = true) : ready_(ready) {}
    static constexpr Result OK = 0;
    static constexpr Result NOT_FOUND = 1;

    Result open(const char*, bool, Handle&) override { return ready_ ? OK : NOT_FOUND; }
    Result getU32(Handle, const char* key, uint32_t& value) override {
        auto it = store_.find(std::string(key ? key : ""));
        if (it == store_.end()) return NOT_FOUND;
        value = it->second;
        return OK;
    }
    Result setU32(Handle, const char* key, uint32_t value) override {
        store_[std::string(key ? key : "")] = value;
        return OK;
    }
    Result getBlob(Handle, const char*, void*, size_t*) override { return NOT_FOUND; }
    Result setBlob(Handle, const char*, const void*, size_t) override { return OK; }
    Result commit(Handle) override { return OK; }
    Result eraseAll(Handle) override { return OK; }
    void close(Handle) override {}
    Result flashInit() override { return ready_ ? OK : NOT_FOUND; }
    Result flashErase() override { return OK; }
    bool isOk(Result r) const override { return r == OK; }
    bool isNotFound(Result r) const override { return r == NOT_FOUND; }
    bool requiresFlashErase(Result) const override { return false; }
    const char* errorName(Result) const override { return "TEST_NVS_ERR"; }

    bool ready_ = true;
    std::map<std::string, uint32_t> store_;
};

void test_clock_trust_hardware_priority_beats_system_time(void) {
    ClockTrustInputs in{};
    in.hardware_trusted = true;
    in.system_time_valid = true;
    TEST_ASSERT_EQUAL(TimeSourceKind::DS1307_RTC, ClockTrustResolver::resolve(in));
}

void test_clock_trust_untrusted_hardware_falls_through_to_system_time(void) {
    ClockTrustInputs in{};
    in.hardware_trusted = false;
    in.system_time_valid = true;
    TEST_ASSERT_EQUAL(TimeSourceKind::SYSTEM_NTP, ClockTrustResolver::resolve(in));
}

void test_clock_trust_no_valid_source_returns_invalid(void) {
    ClockTrustInputs in{};
    TEST_ASSERT_EQUAL(TimeSourceKind::INVALID, ClockTrustResolver::resolve(in));
}

void test_time_source_string_round_trip(void) {
    TEST_ASSERT_EQUAL_STRING("DS1307_RTC", timeSourceKindToString(TimeSourceKind::DS1307_RTC));
    TEST_ASSERT_EQUAL(TimeSourceKind::SYSTEM_NTP, timeSourceKindFromString("SYSTEM_NTP"));
    TEST_ASSERT_EQUAL(TimeSourceKind::UNKNOWN, timeSourceKindFromString("GHOST"));
}

void test_fake_time_telemetry_can_be_set_and_read_back(void) {
    FakeTimeTelemetry fake;
    fake.setSource(TimeSourceKind::BACKEND);
    fake.setRtcValid(true);
    fake.setNtpSynced(true);
    fake.setLastSyncUnixTimeUtc(1700000000LL);

    TimeTelemetry t = fake.getTimeTelemetry();
    TEST_ASSERT_EQUAL(TimeSourceKind::BACKEND, t.source);
    TEST_ASSERT_TRUE(t.rtc_valid);
    TEST_ASSERT_TRUE(t.ntp_synced);
    TEST_ASSERT_EQUAL_INT64(1700000000LL, t.last_sync_unix_time_utc);
}

void test_backend_clock_persistence_survives_nvs_round_trip(void) {
    ScriptedNvsBackend backend;
    NvsStorage store(&backend, CLOCK_NVS_NAMESPACE);
    TEST_ASSERT_TRUE(store.begin());

    TEST_ASSERT_TRUE(store.setU32(NVS_KEY_CLOCK_MAGIC, CLOCK_NVS_RECORD_VERSION));
    TEST_ASSERT_TRUE(store.setU32(NVS_KEY_CLOCK_UNIX, 1700000001U));
    TEST_ASSERT_TRUE(store.setU32(NVS_KEY_CLOCK_TZ_OFFSET, 25200U));

    uint32_t magic = 0;
    uint32_t unix_time = 0;
    uint32_t tz_offset = 0;
    TEST_ASSERT_TRUE(store.getU32(NVS_KEY_CLOCK_MAGIC, magic));
    TEST_ASSERT_TRUE(store.getU32(NVS_KEY_CLOCK_UNIX, unix_time));
    TEST_ASSERT_TRUE(store.getU32(NVS_KEY_CLOCK_TZ_OFFSET, tz_offset));
    TEST_ASSERT_EQUAL_UINT32(CLOCK_NVS_RECORD_VERSION, magic);
    TEST_ASSERT_EQUAL_UINT32(1700000001U, unix_time);
    TEST_ASSERT_EQUAL_UINT32(25200U, tz_offset);
}

void test_backend_clock_persistence_rejects_stale_record(void) {
    ScriptedNvsBackend backend;
    NvsStorage store(&backend, CLOCK_NVS_NAMESPACE);
    TEST_ASSERT_TRUE(store.begin());

    TEST_ASSERT_TRUE(store.setU32(NVS_KEY_CLOCK_MAGIC, CLOCK_NVS_RECORD_VERSION + 1U));
    uint32_t magic = 0;
    TEST_ASSERT_TRUE(store.getU32(NVS_KEY_CLOCK_MAGIC, magic));
    TEST_ASSERT_EQUAL_UINT32(CLOCK_NVS_RECORD_VERSION + 1U, magic);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_clock_trust_hardware_priority_beats_system_time);
    RUN_TEST(test_clock_trust_untrusted_hardware_falls_through_to_system_time);
    RUN_TEST(test_clock_trust_no_valid_source_returns_invalid);
    RUN_TEST(test_time_source_string_round_trip);
    RUN_TEST(test_fake_time_telemetry_can_be_set_and_read_back);
    RUN_TEST(test_backend_clock_persistence_survives_nvs_round_trip);
    RUN_TEST(test_backend_clock_persistence_rejects_stale_record);
    RUN_TEST(test_rtc_manager_begin_seeds_cached_posix_and_reads_hardware_once);
    RUN_TEST(test_rtc_manager_invalid_rtc_does_not_seed_or_read_on_cached_queries);
    RUN_TEST(test_rtc_manager_backend_updates_cached_clock_and_hardware);
    RUN_TEST(test_rtc_manager_drift_compensation_reads_once_and_corrects_cached_clock);
    return UNITY_END();
}
