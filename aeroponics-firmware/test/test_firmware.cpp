#include <unity.h>
#include "config.h"
#include "schedule_manager.h"
#include "fakes/FakeRelayOutput.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeWatchdog.h"
#include "fakes/FakeProfileRepository.h"

void setUp(void) {}
void tearDown(void) {}

void test_fake_relay_override(void) {
    FakeRelayOutput relay;
    relay.initPins();

    TEST_ASSERT_EQUAL(RELAY_OFF, relay.getRelayState(0));
    TEST_ASSERT_FALSE(relay.isOverrideActive(0));

    bool started = relay.startManualOverride(0, RELAY_ON, 5);
    TEST_ASSERT_TRUE(started);
    TEST_ASSERT_TRUE(relay.isOverrideActive(0));
    TEST_ASSERT_EQUAL(RELAY_ON, relay.getRelayState(0));

    // Tick 4 times -> remaining = 1, still active
    for (int i = 0; i < 4; ++i) {
        relay.applyScheduledStateUnlessOverride(0, RELAY_OFF);
    }
    TEST_ASSERT_TRUE(relay.isOverrideActive(0));
    TEST_ASSERT_EQUAL(RELAY_ON, relay.getRelayState(0));

    // Tick 1 more time -> remaining = 0, override expires, restores scheduled state OFF
    relay.applyScheduledStateUnlessOverride(0, RELAY_OFF);
    TEST_ASSERT_FALSE(relay.isOverrideActive(0));
    TEST_ASSERT_EQUAL(RELAY_OFF, relay.getRelayState(0));
}

void test_fake_clock_night_mode(void) {
    FakeClock clock(12, true); // 12:00 -> DAY
    TEST_ASSERT_FALSE(clock.isNightMode());

    clock.setTime(20, 0, 0, true); // 20:00 -> NIGHT
    TEST_ASSERT_TRUE(clock.isNightMode());

    clock.setTime(3, 0, 0, true); // 03:00 -> NIGHT
    TEST_ASSERT_TRUE(clock.isNightMode());

    // Rule S1-RTC-04: invalid time MUST return false (DAY mode fail-safe)
    clock.setTime(22, 0, 0, false);
    TEST_ASSERT_FALSE(clock.isNightMode());
}

void test_profile_repository_validation(void) {
    FakeProfileRepository repo;
    RelayProfile valid_p{10, 100, 15, 200};
    TEST_ASSERT_TRUE(repo.saveProfile(0, valid_p));

    RelayProfile loaded;
    TEST_ASSERT_TRUE(repo.loadProfile(0, loaded));
    TEST_ASSERT_EQUAL_UINT32(10, loaded.spray_day_s);
    TEST_ASSERT_EQUAL_UINT32(100, loaded.cooldown_day_s);

    // Rule S1-NVS-03: Reject spray < 5
    RelayProfile invalid_spray{2, 100, 15, 200};
    TEST_ASSERT_FALSE(repo.saveProfile(0, invalid_spray));

    // Rule S1-NVS-03: Reject cooldown > 7200
    RelayProfile invalid_cooldown{10, 8000, 15, 200};
    TEST_ASSERT_FALSE(repo.saveProfile(0, invalid_cooldown));
}

void test_schedule_manager_di_and_step(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true); // DAY mode
    FakeRelayOutput relay;
    FakeWatchdog wdt;

    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt));

    RelayRuntimeState st = mgr.getRuntimeState(0);
    TEST_ASSERT_EQUAL(PHASE_SPRAYING, st.phase);
    TEST_ASSERT_EQUAL_UINT32(DEFAULT_SPRAY_DAY_S, st.phase_remaining_s);

    // Step through spray phase duration
    uint32_t spray_dur = st.phase_remaining_s;
    for (uint32_t i = 0; i < spray_dur; ++i) {
        mgr.stepRelayPhase(0);
    }

    RelayRuntimeState st_after_spray = mgr.getRuntimeState(0);
    TEST_ASSERT_EQUAL(PHASE_COOLING_DOWN, st_after_spray.phase);
    TEST_ASSERT_EQUAL_UINT32(DEFAULT_COOLDOWN_DAY_S, st_after_spray.phase_remaining_s);
}

void test_emergency_fault_latching(void) {
    FakeRelayOutput relay;
    relay.initPins();

    TEST_ASSERT_FALSE(relay.isFaultLatched(0));
    TEST_ASSERT_TRUE(relay.setRelay(0, RELAY_ON));
    TEST_ASSERT_EQUAL(RELAY_ON, relay.getRelayState(0));

    // Trigger emergency fault latch
    TEST_ASSERT_TRUE(relay.forceRelayOffEmergency(0));
    TEST_ASSERT_TRUE(relay.isFaultLatched(0));
    TEST_ASSERT_EQUAL(RELAY_OFF, relay.getRelayState(0));

    // Block write HIGH while latched
    TEST_ASSERT_FALSE(relay.setRelay(0, RELAY_ON));
    TEST_ASSERT_EQUAL(RELAY_OFF, relay.getRelayState(0));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_fake_relay_override);
    RUN_TEST(test_fake_clock_night_mode);
    RUN_TEST(test_profile_repository_validation);
    RUN_TEST(test_schedule_manager_di_and_step);
    RUN_TEST(test_emergency_fault_latching);
    return UNITY_END();
}
