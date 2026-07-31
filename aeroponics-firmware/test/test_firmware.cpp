#include <unity.h>
#include <chrono>
#include <thread>
#include "config.h"
#include "schedule_manager.h"
#include "fakes/FakeRelayOutput.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeWatchdog.h"
#include "fakes/FakeProfileRepository.h"
#include "fakes/FakeTaskRunner.h"

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

    // Tick 4 times via tickOverride
    for (int i = 0; i < 4; ++i) {
        relay.tickOverride(0);
        TEST_ASSERT_TRUE(relay.isOverrideActive(0));
        TEST_ASSERT_EQUAL(RELAY_ON, relay.getRelayState(0));
    }

    // Tick 5th time -> remaining = 0, override expires
    relay.tickOverride(0);
    TEST_ASSERT_FALSE(relay.isOverrideActive(0));
    TEST_ASSERT_EQUAL_UINT32(1, relay.getOverrideExpiryTransitionCount(0));
    relay.tickOverride(0);
    TEST_ASSERT_EQUAL_UINT32(1, relay.getOverrideExpiryTransitionCount(0));
    TEST_ASSERT_TRUE(relay.setRelay(0, RELAY_OFF));
    TEST_ASSERT_EQUAL(RELAY_OFF, relay.getRelayState(0));
}

void test_override_pauses_auto_timer_spraying(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true); // DAY mode
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));

    RelayRuntimeState st_initial = mgr.getRuntimeState(0);
    TEST_ASSERT_EQUAL(PHASE_SPRAYING, st_initial.phase);
    uint32_t remaining_before = st_initial.phase_remaining_s;

    // Trigger manual override OFF for 5s
    TEST_ASSERT_TRUE(relay.startManualOverride(0, RELAY_OFF, 5));
    TEST_ASSERT_TRUE(relay.isOverrideActive(0));
    TEST_ASSERT_EQUAL(RELAY_OFF, relay.getRelayState(0));

    // Ticks 1..4: override active
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT_TRUE(mgr.stepRelayPhase(0));
        RelayRuntimeState st = mgr.getRuntimeState(0);
        TEST_ASSERT_EQUAL(PHASE_SPRAYING, st.phase);
        TEST_ASSERT_EQUAL_UINT32(remaining_before, st.phase_remaining_s);
        TEST_ASSERT_TRUE(relay.isOverrideActive(0));
        TEST_ASSERT_EQUAL(RELAY_OFF, relay.getRelayState(0));
    }

    // Tick 5: override expires inside stepRelayPhase on this tick
    TEST_ASSERT_TRUE(mgr.stepRelayPhase(0));

    // AT END OF TICK 5 (expiration tick):
    // 1. isOverrideActive() == false
    // 2. output is restored to scheduled state RELAY_ON
    // 3. phase and remaining time are UNCHANGED
    TEST_ASSERT_FALSE(relay.isOverrideActive(0));
    TEST_ASSERT_EQUAL(RELAY_ON, relay.getRelayState(0));
    RelayRuntimeState st_exp = mgr.getRuntimeState(0);
    TEST_ASSERT_EQUAL(PHASE_SPRAYING, st_exp.phase);
    TEST_ASSERT_EQUAL_UINT32(remaining_before, st_exp.phase_remaining_s);

    // Tick 6: auto-timer countdown decrements by 1, relay output remains RELAY_ON
    TEST_ASSERT_TRUE(mgr.stepRelayPhase(0));
    RelayRuntimeState st_resumed = mgr.getRuntimeState(0);
    TEST_ASSERT_EQUAL(PHASE_SPRAYING, st_resumed.phase);
    TEST_ASSERT_EQUAL_UINT32(remaining_before - 1, st_resumed.phase_remaining_s);
    TEST_ASSERT_EQUAL(RELAY_ON, relay.getRelayState(0));
}

void test_override_pauses_auto_timer_cooldown(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true); // DAY mode
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));

    // Fast-forward spray phase to enter COOLING_DOWN
    uint32_t spray_dur = mgr.getRuntimeState(0).phase_remaining_s;
    for (uint32_t i = 0; i < spray_dur; ++i) {
        mgr.stepRelayPhase(0);
    }

    RelayRuntimeState st_cd = mgr.getRuntimeState(0);
    TEST_ASSERT_EQUAL(PHASE_COOLING_DOWN, st_cd.phase);
    uint32_t cd_remaining_before = st_cd.phase_remaining_s;

    // Trigger manual override ON for 5s while in cooldown
    TEST_ASSERT_TRUE(relay.startManualOverride(0, RELAY_ON, 5));
    TEST_ASSERT_TRUE(relay.isOverrideActive(0));
    TEST_ASSERT_EQUAL(RELAY_ON, relay.getRelayState(0));

    // Ticks 1..4: override active
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT_TRUE(mgr.stepRelayPhase(0));
        RelayRuntimeState st = mgr.getRuntimeState(0);
        TEST_ASSERT_EQUAL(PHASE_COOLING_DOWN, st.phase);
        TEST_ASSERT_EQUAL_UINT32(cd_remaining_before, st.phase_remaining_s);
        TEST_ASSERT_TRUE(relay.isOverrideActive(0));
        TEST_ASSERT_EQUAL(RELAY_ON, relay.getRelayState(0));
    }

    // Tick 5: override expires
    TEST_ASSERT_TRUE(mgr.stepRelayPhase(0));

    // AT END OF TICK 5 (expiration tick):
    // 1. isOverrideActive() == false
    // 2. output restored to scheduled RELAY_OFF
    // 3. phase and remaining time UNCHANGED
    TEST_ASSERT_FALSE(relay.isOverrideActive(0));
    TEST_ASSERT_EQUAL(RELAY_OFF, relay.getRelayState(0));
    RelayRuntimeState st_exp = mgr.getRuntimeState(0);
    TEST_ASSERT_EQUAL(PHASE_COOLING_DOWN, st_exp.phase);
    TEST_ASSERT_EQUAL_UINT32(cd_remaining_before, st_exp.phase_remaining_s);

    // Tick 6: auto-timer resumes countdown, relay output remains RELAY_OFF
    TEST_ASSERT_TRUE(mgr.stepRelayPhase(0));
    RelayRuntimeState st_resumed = mgr.getRuntimeState(0);
    TEST_ASSERT_EQUAL(PHASE_COOLING_DOWN, st_resumed.phase);
    TEST_ASSERT_EQUAL_UINT32(cd_remaining_before - 1, st_resumed.phase_remaining_s);
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
    FakeTaskRunner runner;

    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));

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

void test_schedule_manager_step_failure_propagation(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));

    // Normal step should succeed
    TEST_ASSERT_TRUE(mgr.stepRelayPhase(0));

    // Inject failure on relay output apply
    relay.setFailScheduledApply(0, true);

    // Step should now return false (propagating output error and latching emergency off)
    TEST_ASSERT_FALSE(mgr.stepRelayPhase(0));
    TEST_ASSERT_TRUE(relay.isFaultLatched(0));
}

void test_schedule_manager_update_profile_rejection(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));

    RelayProfile valid_p{20, 200, 20, 400};
    TEST_ASSERT_TRUE(mgr.updateProfile(0, valid_p));

    RelayProfile invalid_p{1, 200, 20, 400}; // spray < MIN_SPRAY_DURATION_S (5)
    TEST_ASSERT_FALSE(mgr.updateProfile(0, invalid_p));
}

void test_profile_save_failure_keeps_ram_and_repository_consistent(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;
    ScheduleManager mgr;

    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));
    const RelayProfile original = mgr.getRuntimeState(0).current_profile;
    const RelayProfile replacement{20, 200, 20, 400};
    repo.setSaveFailure(0, true);

    TEST_ASSERT_FALSE(mgr.updateProfile(0, replacement));

    RelayProfile persisted{};
    TEST_ASSERT_TRUE(repo.loadProfile(0, persisted));
    TEST_ASSERT_EQUAL_UINT32(original.spray_day_s, persisted.spray_day_s);
    TEST_ASSERT_EQUAL_UINT32(original.cooldown_day_s, persisted.cooldown_day_s);

    TEST_ASSERT_TRUE(mgr.stepRelayPhase(0));
    const RelayProfile runtime = mgr.getRuntimeState(0).current_profile;
    TEST_ASSERT_EQUAL_UINT32(original.spray_day_s, runtime.spray_day_s);
    TEST_ASSERT_EQUAL_UINT32(original.cooldown_day_s, runtime.cooldown_day_s);
}

void test_profile_save_contention_does_not_latch_relay_or_starve_wdt(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;
    ScheduleManager mgr;
    const RelayProfile replacement{20, 200, 20, 400};

    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));
    TEST_ASSERT_TRUE(wdt.registerWatchdog(0));
    repo.blockSaves();

    bool update_result = false;
    std::thread update_thread([&] { update_result = mgr.updateProfile(0, replacement); });
    TEST_ASSERT_TRUE(repo.waitForSaveToStart(500));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // The save is deliberately blocked for more than the former 100 ms lock
    // timeout. Relay scheduling and WDT feeds must continue normally.
    TEST_ASSERT_TRUE(mgr.stepRelayPhase(0));
    TEST_ASSERT_FALSE(relay.isFaultLatched(0));
    TEST_ASSERT_TRUE(wdt.resetWatchdog(0));
    TEST_ASSERT_TRUE(wdt.resetWatchdog(0));
    TEST_ASSERT_EQUAL_UINT32(2, wdt.getResetCount(0));

    repo.releaseSaves();
    update_thread.join();
    TEST_ASSERT_TRUE(update_result);

    RelayProfile persisted{};
    TEST_ASSERT_TRUE(repo.loadProfile(0, persisted));
    TEST_ASSERT_EQUAL_UINT32(replacement.spray_day_s, persisted.spray_day_s);
    TEST_ASSERT_TRUE(mgr.stepRelayPhase(0));
    TEST_ASSERT_EQUAL_UINT32(replacement.spray_day_s, mgr.getRuntimeState(0).current_profile.spray_day_s);
    TEST_ASSERT_FALSE(relay.isFaultLatched(0));
}

void test_task_creation_fault_injection_and_rollback(void) {
    for (uint8_t fail_idx = 0; fail_idx < TOTAL_RELAYS; ++fail_idx) {
        FakeProfileRepository repo;
        FakeClock clock(10, true);
        FakeRelayOutput relay;
        FakeWatchdog wdt;
        FakeTaskRunner runner;

        ScheduleManager mgr;
        TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));

        runner.setFailAtRelay(fail_idx, true);

        // startAllTasks should fail at fail_idx
        TEST_ASSERT_FALSE(mgr.startAllTasks());

        // Assert lifecycle state is FAULTED
        TEST_ASSERT_EQUAL(ScheduleLifecycleState::FAULTED, mgr.getLifecycleState());

        // Assert NO relay task is registered with WDT
        for (uint8_t j = 0; j < TOTAL_RELAYS; ++j) {
            TEST_ASSERT_FALSE(wdt.isRegistered(j));
            TEST_ASSERT_FALSE(mgr.isTaskWdtRegistered(j));
        }

        // Assert ALL 4 relays are latched OFF and forced LOW in safe-state
        for (uint8_t j = 0; j < TOTAL_RELAYS; ++j) {
            TEST_ASSERT_TRUE(relay.isFaultLatched(j));
            TEST_ASSERT_EQUAL(RELAY_OFF, relay.getRelayState(j));
        }
    }
}

void test_callback_exit_during_task_creation_rolls_back_safely(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;
    ScheduleManager mgr;

    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));
    runner.setExitImmediatelyDuringStart(0, true);

    TEST_ASSERT_FALSE(mgr.startAllTasks());
    TEST_ASSERT_EQUAL(ScheduleLifecycleState::FAULTED, mgr.getLifecycleState());
    for (uint8_t relay_id = 0; relay_id < TOTAL_RELAYS; ++relay_id) {
        TEST_ASSERT_FALSE(mgr.isManagerCallbackActive(relay_id));
        TEST_ASSERT_TRUE(relay.isFaultLatched(relay_id));
    }
}

void test_rollback_timeout_keeps_manager_lifetime_pending_and_latches_relays(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;
    ScheduleManager mgr;

    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));
    runner.setHoldCallbackOnStop(0, true);
    runner.setFailAtRelay(1, true);

    // Relay 0 refuses the cooperative stop while relay 1 fails to start.
    // Rollback must retain callback-owned manager state rather than claiming
    // teardown succeeded and freeing synchronization primitives.
    TEST_ASSERT_FALSE(mgr.startAllTasks());
    TEST_ASSERT_TRUE(mgr.isTeardownPending());
    TEST_ASSERT_TRUE(mgr.isManagerCallbackActive(0));
    for (uint8_t relay_id = 0; relay_id < TOTAL_RELAYS; ++relay_id) {
        TEST_ASSERT_TRUE(relay.isFaultLatched(relay_id));
        TEST_ASSERT_EQUAL(RELAY_OFF, relay.getRelayState(relay_id));
    }

    // Release the fake callback so normal test object destruction is safe.
    runner.setHoldCallbackOnStop(0, false);
    TEST_ASSERT_TRUE(runner.requestStop(0));
    TEST_ASSERT_FALSE(mgr.isManagerCallbackActive(0));
}

void test_wdt_registration_failure_rolls_back_without_affecting_main_wdt(void) {
    for (uint8_t fail_idx = 0; fail_idx < TOTAL_RELAYS; ++fail_idx) {
        FakeProfileRepository repo;
        FakeClock clock(10, true);
        FakeRelayOutput relay;
        FakeWatchdog wdt;
        FakeTaskRunner runner;
        ScheduleManager mgr;

        TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));
        TEST_ASSERT_TRUE(wdt.resetMainTaskWatchdog());
        const uint32_t main_resets_before = wdt.getMainTaskResetCount();
        wdt.setRegistrationFailure(fail_idx, true);

        TEST_ASSERT_FALSE(mgr.startAllTasks());
        TEST_ASSERT_EQUAL(ScheduleLifecycleState::FAULTED, mgr.getLifecycleState());
        TEST_ASSERT_TRUE(wdt.isMainTaskRegistered());
        TEST_ASSERT_TRUE(wdt.resetMainTaskWatchdog());
        TEST_ASSERT_EQUAL_UINT32(main_resets_before + 1, wdt.getMainTaskResetCount());

        for (uint8_t relay_id = 0; relay_id < TOTAL_RELAYS; ++relay_id) {
            TEST_ASSERT_FALSE(mgr.isManagerCallbackActive(relay_id));
            TEST_ASSERT_FALSE(mgr.isTaskWdtRegistered(relay_id));
            TEST_ASSERT_FALSE(wdt.isRegistered(relay_id));
            TEST_ASSERT_TRUE(relay.isFaultLatched(relay_id));
            if (relay_id < fail_idx) {
                TEST_ASSERT_TRUE(runner.wasStopRequested(relay_id));
            }
        }
    }
}

void test_duplicate_start_all_tasks_rejection(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));

    TEST_ASSERT_EQUAL(ScheduleLifecycleState::NOT_STARTED, mgr.getLifecycleState());

    // First call succeeds
    TEST_ASSERT_TRUE(mgr.startAllTasks());
    TEST_ASSERT_EQUAL(ScheduleLifecycleState::RUNNING, mgr.getLifecycleState());

    // Second call while RUNNING MUST be rejected
    TEST_ASSERT_FALSE(mgr.startAllTasks());
    TEST_ASSERT_EQUAL(ScheduleLifecycleState::RUNNING, mgr.getLifecycleState());
}

void test_get_runtime_state_safely_validation(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));

    RelayRuntimeState st;
    TEST_ASSERT_TRUE(mgr.getRuntimeStateSafely(0, st));
    TEST_ASSERT_EQUAL(PHASE_SPRAYING, st.phase);

    // Invalid relay index MUST fail and return false
    TEST_ASSERT_FALSE(mgr.getRuntimeStateSafely(TOTAL_RELAYS, st));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_fake_relay_override);
    RUN_TEST(test_override_pauses_auto_timer_spraying);
    RUN_TEST(test_override_pauses_auto_timer_cooldown);
    RUN_TEST(test_fake_clock_night_mode);
    RUN_TEST(test_profile_repository_validation);
    RUN_TEST(test_schedule_manager_di_and_step);
    RUN_TEST(test_emergency_fault_latching);
    RUN_TEST(test_schedule_manager_step_failure_propagation);
    RUN_TEST(test_schedule_manager_update_profile_rejection);
    RUN_TEST(test_profile_save_failure_keeps_ram_and_repository_consistent);
    RUN_TEST(test_profile_save_contention_does_not_latch_relay_or_starve_wdt);
    RUN_TEST(test_task_creation_fault_injection_and_rollback);
    RUN_TEST(test_callback_exit_during_task_creation_rolls_back_safely);
    RUN_TEST(test_rollback_timeout_keeps_manager_lifetime_pending_and_latches_relays);
    RUN_TEST(test_wdt_registration_failure_rolls_back_without_affecting_main_wdt);
    RUN_TEST(test_duplicate_start_all_tasks_rejection);
    RUN_TEST(test_get_runtime_state_safely_validation);
    return UNITY_END();
}
