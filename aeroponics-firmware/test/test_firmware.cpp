#include <unity.h>
#include <chrono>
#include <thread>
#include "config.h"
#include "nvs_storage.h"
#include "schedule_manager.h"
#include "fakes/FakeRelayOutput.h"
#include "fakes/FakeClock.h"
#include "mqtt_task_policy.h"
#include "mqtt_lifecycle.h"
#include "fakes/FakeWatchdog.h"
#include "fakes/FakeProfileRepository.h"
#include "fakes/FakeNvsBackend.h"
#include "fakes/FakeTaskRunner.h"
#include "mqtt_client.h"
#include "mqtt_config_provider.h"

void setUp(void) {}
void tearDown(void) {}

class OrderedWatchdog final : public FakeWatchdog {
public:
    bool resetWatchdog(uint8_t relay_id) override {
        reset_observed_ = true;
        return FakeWatchdog::resetWatchdog(relay_id);
    }

    bool resetObserved() const { return reset_observed_; }

private:
    bool reset_observed_ = false;
};

class OrderedStopRunner final : public FakeTaskRunner {
public:
    explicit OrderedStopRunner(const OrderedWatchdog& watchdog) : watchdog_(watchdog) {}

    bool consumeStopRequest(uint8_t relay_id, uint32_t generation) override {
        (void)relay_id;
        (void)generation;
        stop_checked_after_wdt_ = watchdog_.resetObserved();
        ++stop_check_count_;
        return stop_requested_;
    }

    void requestStopOnNextCheck() { stop_requested_ = true; }
    bool stopCheckedAfterWdt() const { return stop_checked_after_wdt_; }
    uint32_t stopCheckCount() const { return stop_check_count_; }

private:
    const OrderedWatchdog& watchdog_;
    bool stop_requested_ = false;
    bool stop_checked_after_wdt_ = false;
    uint32_t stop_check_count_ = 0;
};

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

void assertSafeDefaultProfile(const RelayProfile& profile) {
    TEST_ASSERT_EQUAL_UINT32(DEFAULT_SPRAY_DAY_S, profile.spray_day_s);
    TEST_ASSERT_EQUAL_UINT32(DEFAULT_COOLDOWN_DAY_S, profile.cooldown_day_s);
    TEST_ASSERT_EQUAL_UINT32(DEFAULT_SPRAY_NIGHT_S, profile.spray_night_s);
    TEST_ASSERT_EQUAL_UINT32(DEFAULT_COOLDOWN_NIGHT_S, profile.cooldown_night_s);
}

void test_nvs_storage_not_found_uses_safe_defaults_successfully(void) {
    FakeNvsBackend backend;
    backend.setOpenResult(FakeNvsBackend::NOT_FOUND);
    NvsStorage storage(&backend);
    RelayProfile profile{};

    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_TRUE(storage.loadProfile(0, profile));
    TEST_ASSERT_EQUAL_UINT32(1, backend.openCalls());
    TEST_ASSERT_EQUAL_UINT32(0, backend.getCalls());
    assertSafeDefaultProfile(profile);
}

void test_nvs_storage_open_error_returns_false_and_safe_defaults(void) {
    FakeNvsBackend backend;
    backend.setOpenResult(FakeNvsBackend::IO_ERROR);
    NvsStorage storage(&backend);
    RelayProfile profile{};

    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_FALSE(storage.loadProfile(0, profile));
    TEST_ASSERT_EQUAL_UINT32(1, backend.openCalls());
    TEST_ASSERT_EQUAL_UINT32(0, backend.getCalls());
    assertSafeDefaultProfile(profile);
}

void test_nvs_storage_each_get_error_returns_false_and_safe_defaults(void) {
    for (uint8_t field = 0; field < FakeNvsBackend::FIELD_COUNT; ++field) {
        FakeNvsBackend backend;
        backend.setGetResult(field, FakeNvsBackend::IO_ERROR);
        NvsStorage storage(&backend);
        RelayProfile profile{};

        TEST_ASSERT_TRUE(storage.begin());
        TEST_ASSERT_FALSE(storage.loadProfile(0, profile));
        TEST_ASSERT_EQUAL_UINT32(1, backend.openCalls());
        TEST_ASSERT_EQUAL_UINT32(FakeNvsBackend::FIELD_COUNT, backend.getCalls());
        assertSafeDefaultProfile(profile);
    }
}

void test_nvs_storage_load_all_reports_any_production_read_error(void) {
    FakeNvsBackend backend;
    backend.setGetResult(FakeNvsBackend::SPRAY_NIGHT, FakeNvsBackend::IO_ERROR);
    NvsStorage storage(&backend);
    RelayProfile profiles[TOTAL_RELAYS] = {};

    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_FALSE(storage.loadAllProfiles(profiles));
    for (uint8_t relay_id = 0; relay_id < TOTAL_RELAYS; ++relay_id) {
        assertSafeDefaultProfile(profiles[relay_id]);
    }
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

void test_schedule_manager_uses_composition_root_boot_snapshot(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;
    RelayProfile boot_profiles[TOTAL_RELAYS];
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        boot_profiles[i] = RelayProfile{20, 200, 25, 300};
    }

    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner, boot_profiles));
    const RelayRuntimeState state = mgr.getRuntimeState(0);
    TEST_ASSERT_EQUAL_UINT32(20, state.current_profile.spray_day_s);
    TEST_ASSERT_EQUAL_UINT32(20, state.phase_remaining_s);
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

void test_relay_iteration_feeds_wdt_before_stop_check_and_stops_within_one_tick(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    OrderedWatchdog wdt;
    OrderedStopRunner runner(wdt);
    ScheduleManager mgr;

    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));
    TEST_ASSERT_TRUE(mgr.initializeRelayTask(0));
    TEST_ASSERT_TRUE(relay.setRelay(0, RELAY_ON));
    runner.requestStopOnNextCheck();

    // One tick must feed WDT first, consume the stop request, then exit.
    TEST_ASSERT_FALSE(mgr.runRelayTaskIteration(0, 1));
    TEST_ASSERT_TRUE(runner.stopCheckedAfterWdt());
    TEST_ASSERT_EQUAL_UINT32(1, runner.stopCheckCount());
    TEST_ASSERT_EQUAL_UINT32(1, wdt.getResetCount(0));
    TEST_ASSERT_FALSE(mgr.isTaskWdtRegistered(0));
    TEST_ASSERT_TRUE(relay.isFaultLatched(0));
    TEST_ASSERT_EQUAL(RELAY_OFF, relay.getRelayState(0));
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

        // FAULTED is terminal: retrying must neither recreate tasks nor
        // publish a false RUNNING lifecycle while relays remain latched OFF.
        uint32_t attempts_before_retry[TOTAL_RELAYS];
        for (uint8_t j = 0; j < TOTAL_RELAYS; ++j) {
            attempts_before_retry[j] = runner.getStartAttemptCount(j);
        }
        TEST_ASSERT_FALSE(mgr.startAllTasks());
        TEST_ASSERT_EQUAL(ScheduleLifecycleState::FAULTED, mgr.getLifecycleState());
        for (uint8_t j = 0; j < TOTAL_RELAYS; ++j) {
            TEST_ASSERT_EQUAL_UINT32(attempts_before_retry[j], runner.getStartAttemptCount(j));
        }
    }
}

void test_task_startup_uses_configured_timeout(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;
    ScheduleManager mgr;

    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));
    TEST_ASSERT_TRUE(mgr.startAllTasks());
    for (uint8_t relay_id = 0; relay_id < TOTAL_RELAYS; ++relay_id) {
        TEST_ASSERT_EQUAL_UINT32(RELAY_TASK_STARTUP_TIMEOUT_MS, runner.getStartupTimeout(relay_id));
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

void test_mqtt_client_connect_and_lwt(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    ScheduleManager sm;
    TEST_ASSERT_TRUE(sm.begin(&repo, &clock, &relay, &wdt, &runner));
    IRelayOutput* rc = &relay;

    MqttClient client;
    MqttConfig config {
        "127.0.0.1",
        1883,
        "test_user",
        "test_pass",
        "esp32s3-test"
    };

    // Uninitialized client must fail connect()
    TEST_ASSERT_FALSE(client.connect());

    // Begin validation
    TEST_ASSERT_TRUE(client.begin(config, &sm, rc));

    // Connect & LWT validation
    TEST_ASSERT_TRUE(client.connect());
    TEST_ASSERT_TRUE(client.isConnected());
}

void test_mqtt_client_publish_heartbeat(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    ScheduleManager sm;
    TEST_ASSERT_TRUE(sm.begin(&repo, &clock, &relay, &wdt, &runner));
    IRelayOutput* rc = &relay;

    MqttClient client;
    MqttConfig config {
        "127.0.0.1",
        1883,
        "test_user",
        "test_pass",
        "esp32s3-test"
    };

    // Unconnected client publishHeartbeat must fail
    TEST_ASSERT_FALSE(client.publishHeartbeat());

    TEST_ASSERT_TRUE(client.begin(config, &sm, rc, &clock));
    TEST_ASSERT_TRUE(client.connect());

    // Connected client publishHeartbeat must succeed
    TEST_ASSERT_TRUE(client.publishHeartbeat());

    // Host clock is deliberately NTP-unsynced while the RTC remains valid.
    // timestamp_utc must stay JSON null rather than a time-only string.
    StaticJsonDocument<MQTT_HEARTBEAT_DOC_SIZE> doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, client.mockLastPublishedPayload()));
    TEST_ASSERT_TRUE(doc["rtc_valid"].as<bool>());
    TEST_ASSERT_FALSE(doc["ntp_synced"].as<bool>());
    TEST_ASSERT_TRUE(doc["timestamp_utc"].isNull());

    // A verified Unix/NTP time must produce the Sprint 2 ISO-8601 timestamp.
    client.setMockUnixTime(1775347200); // 2026-04-05T00:00:00Z
    TEST_ASSERT_TRUE(client.publishHeartbeat());
    TEST_ASSERT_TRUE(deserializeJson(doc, client.mockLastPublishedPayload()) == DeserializationError::Ok);
    TEST_ASSERT_TRUE(doc["ntp_synced"].as<bool>());
    TEST_ASSERT_FALSE(doc["timestamp_utc"].isNull());
    TEST_ASSERT_EQUAL_STRING("2026-04-05T00:00:00Z", doc["timestamp_utc"].as<const char*>());
}

void test_mqtt_client_publish_relay_telemetry(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    ScheduleManager sm;
    TEST_ASSERT_TRUE(sm.begin(&repo, &clock, &relay, &wdt, &runner));
    IRelayOutput* rc = &relay;

    MqttClient client;
    MqttConfig config {
        "127.0.0.1",
        1883,
        "test_user",
        "test_pass",
        "esp32s3-test"
    };

    RelayRuntimeState state = sm.getRuntimeState(0);

    // Unconnected client publishRelayTelemetry must fail
    TEST_ASSERT_FALSE(client.publishRelayTelemetry(1, state));

    TEST_ASSERT_TRUE(client.begin(config, &sm, rc, &clock));
    TEST_ASSERT_TRUE(client.connect());

    // Invalid relay_id (> TOTAL_RELAYS) must fail
    TEST_ASSERT_FALSE(client.publishRelayTelemetry(5, state));

    // Valid relay IDs [1..4] must succeed
    TEST_ASSERT_TRUE(client.publishRelayTelemetry(1, state));
    TEST_ASSERT_TRUE(client.publishRelayTelemetry(2, state));
    TEST_ASSERT_TRUE(client.publishRelayTelemetry(3, state));
    TEST_ASSERT_TRUE(client.publishRelayTelemetry(4, state));

    // 0-based relay_id 0 (mapped to 1) must succeed
    TEST_ASSERT_TRUE(client.publishRelayTelemetry(0, state));

    // Test with PHASE_COOLING_DOWN state
    state.phase = PHASE_COOLING_DOWN;
    state.is_night_mode = true;
    TEST_ASSERT_TRUE(client.publishRelayTelemetry(1, state));
}

void test_mqtt_client_on_message(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    ScheduleManager sm;
    TEST_ASSERT_TRUE(sm.begin(&repo, &clock, &relay, &wdt, &runner));
    IRelayOutput* rc = &relay;

    MqttClient client;
    MqttConfig config {
        "127.0.0.1",
        1883,
        "test_user",
        "test_pass",
        "esp32s3-test"
    };

    TEST_ASSERT_TRUE(client.begin(config, &sm, rc, &clock));
    TEST_ASSERT_TRUE(client.connect());

    // 1. Valid schedule update for relay 1 (1-based in topic, 0-based in ScheduleManager)
    char topic_sched[128] = "aeroponics/device/esp32s3-test/command/relay/1/schedule";
    char payload_sched[256] = "{\"relay_id\":1,\"spray_day_s\":25,\"cooldown_day_s\":300,\"spray_night_s\":25,\"cooldown_night_s\":300}";
    client.simulateIncomingMessage(topic_sched, (uint8_t*)payload_sched, strlen(payload_sched));

    sm.stepRelayPhase(0);
    RelayRuntimeState state0 = sm.getRuntimeState(0);
    TEST_ASSERT_EQUAL_UINT16(25, state0.current_profile.spray_day_s);
    TEST_ASSERT_EQUAL_UINT16(300, state0.current_profile.cooldown_day_s);

    // 2. Valid manual override START for relay 2 (ON, 15s)
    char topic_override[128] = "aeroponics/device/esp32s3-test/command/relay/2/override";
    char payload_override_start[256] = "{\"relay_id\":2,\"action\":\"START\",\"state\":\"ON\",\"duration_s\":15}";
    client.simulateIncomingMessage(topic_override, (uint8_t*)payload_override_start, strlen(payload_override_start));

    TEST_ASSERT_TRUE(relay.isOverrideActive(1)); // Relay 2 (0-based 1) active
    TEST_ASSERT_EQUAL(RELAY_ON, relay.getRelayState(1));

    // 3. Valid manual override CANCEL for relay 2
    char payload_override_cancel[256] = "{\"relay_id\":2,\"action\":\"CANCEL\"}";
    client.simulateIncomingMessage(topic_override, (uint8_t*)payload_override_cancel, strlen(payload_override_cancel));

    TEST_ASSERT_FALSE(relay.isOverrideActive(1)); // Relay 2 override cancelled

    // 4. Test overflow guard (payload length > MQTT_BUFFER_SIZE - 1)
    char topic_overflow[128] = "aeroponics/device/esp32s3-test/command/relay/1/schedule";
    char payload_overflow[2050] = {0};
    memset(payload_overflow, 'a', 2049);
    client.simulateIncomingMessage(topic_overflow, (uint8_t*)payload_overflow, 2049);

    // 5. Test invalid json error checking
    char payload_bad_json[256] = "{\"relay_id\":1,\"spray_day_s\":25";
    client.simulateIncomingMessage(topic_sched, (uint8_t*)payload_bad_json, strlen(payload_bad_json));

    // 6. Test invalid relay ID (> 4)
    char topic_invalid_relay[128] = "aeroponics/device/esp32s3-test/command/relay/99/schedule";
    client.simulateIncomingMessage(topic_invalid_relay, (uint8_t*)payload_sched, strlen(payload_sched));
}

void test_mqtt_command_validation_rejects_untrusted_input(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;
    ScheduleManager sm;
    TEST_ASSERT_TRUE(sm.begin(&repo, &clock, &relay, &wdt, &runner));
    IRelayOutput* rc = &relay;
    MqttClient client;
    MqttConfig config {"127.0.0.1", 1883, "test_user", "test_pass", "esp32s3-test"};
    TEST_ASSERT_TRUE(client.begin(config, &sm, rc, &clock));
    TEST_ASSERT_TRUE(client.connect());

    // Verbatim Sprint 2 schedule command schema is the only accepted schema.
    char exact_payload[] = "{\"relay_id\":1,\"spray_day_s\":25,\"cooldown_day_s\":300,\"spray_night_s\":25,\"cooldown_night_s\":300}";
    const RelayRuntimeState before = sm.getRuntimeState(0);
    char foreign_device[] = "aeroponics/device/other/command/relay/1/schedule";
    client.simulateIncomingMessage(foreign_device, reinterpret_cast<uint8_t*>(exact_payload), strlen(exact_payload));
    char extra_segment[] = "aeroponics/device/esp32s3-test/command/relay/1/schedule/extra";
    client.simulateIncomingMessage(extra_segment, reinterpret_cast<uint8_t*>(exact_payload), strlen(exact_payload));
    char mismatch[] = "{\"relay_id\":2,\"spray_day_s\":25,\"cooldown_day_s\":300,\"spray_night_s\":25,\"cooldown_night_s\":300}";
    char valid_topic[] = "aeroponics/device/esp32s3-test/command/relay/1/schedule";
    client.simulateIncomingMessage(valid_topic, reinterpret_cast<uint8_t*>(mismatch), strlen(mismatch));
    char invalid_duration[] = "{\"relay_id\":1,\"spray_day_s\":\"25\",\"cooldown_day_s\":300,\"spray_night_s\":25,\"cooldown_night_s\":300}";
    client.simulateIncomingMessage(valid_topic, reinterpret_cast<uint8_t*>(invalid_duration), strlen(invalid_duration));
    sm.stepRelayPhase(0);
    TEST_ASSERT_EQUAL_UINT32(before.current_profile.spray_day_s, sm.getRuntimeState(0).current_profile.spray_day_s);

    char override_topic[] = "aeroponics/device/esp32s3-test/command/relay/2/override";
    char missing_duration[] = "{\"relay_id\":2,\"action\":\"START\",\"state\":\"ON\"}";
    client.simulateIncomingMessage(override_topic, reinterpret_cast<uint8_t*>(missing_duration), strlen(missing_duration));
    char invalid_state[] = "{\"relay_id\":2,\"action\":\"START\",\"state\":\"MAYBE\",\"duration_s\":15}";
    client.simulateIncomingMessage(override_topic, reinterpret_cast<uint8_t*>(invalid_state), strlen(invalid_state));
    TEST_ASSERT_FALSE(relay.isOverrideActive(1));

    char exact_length_payload[] = "{\"relay_id\":2,\"action\":\"CANCEL\"}";
    client.simulateIncomingMessage(override_topic, reinterpret_cast<uint8_t*>(exact_length_payload), strlen(exact_length_payload));
}

void test_mqtt_callback_enforces_payload_length_contract(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;
    ScheduleManager sm;
    TEST_ASSERT_TRUE(sm.begin(&repo, &clock, &relay, &wdt, &runner));
    MqttClient client;
    MqttConfig config{"127.0.0.1", 1883, "user", "pass", "valid-device"};
    TEST_ASSERT_TRUE(client.begin(config, &sm, &relay, &clock));
    TEST_ASSERT_TRUE(client.connect());

    char topic[] = "aeroponics/device/valid-device/command/relay/1/schedule";
    const char accepted[] =
        "{\"relay_id\":1,\"spray_day_s\":26,\"cooldown_day_s\":300,"
        "\"spray_night_s\":25,\"cooldown_night_s\":300}";
    uint8_t max_accepted[MQTT_BUFFER_SIZE - 1];
    memset(max_accepted, ' ', sizeof(max_accepted));
    memcpy(max_accepted, accepted, sizeof(accepted) - 1);
    client.simulateIncomingMessage(topic, max_accepted, sizeof(max_accepted));
    sm.stepRelayPhase(0);
    TEST_ASSERT_EQUAL_UINT32(26, sm.getRuntimeState(0).current_profile.spray_day_s);

    const char rejected[] =
        "{\"relay_id\":1,\"spray_day_s\":27,\"cooldown_day_s\":300,"
        "\"spray_night_s\":25,\"cooldown_night_s\":300}";
    uint8_t too_large[MQTT_BUFFER_SIZE];
    memset(too_large, ' ', sizeof(too_large));
    memcpy(too_large, rejected, sizeof(rejected) - 1);
    client.simulateIncomingMessage(topic, too_large, sizeof(too_large));
    sm.stepRelayPhase(0);
    TEST_ASSERT_EQUAL_UINT32(26, sm.getRuntimeState(0).current_profile.spray_day_s);
}

void test_mqtt_config_provider_load(void) {
    MqttConfig config = MqttConfigProvider::load();
    TEST_ASSERT_NOT_NULL(config.device_id);
    TEST_ASSERT_EQUAL_STRING("", config.device_id);
    TEST_ASSERT_EQUAL_UINT16(1883, config.broker_port);
}

void test_mqtt_config_rejects_unsafe_device_id(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;
    ScheduleManager sm;
    TEST_ASSERT_TRUE(sm.begin(&repo, &clock, &relay, &wdt, &runner));
    IRelayOutput* output = &relay;
    MqttClient client;

    const char* invalid_ids[] = {"bad/id", "bad+id", "bad#id", "bad id", "bad\n id"};
    for (const char* id : invalid_ids) {
        MqttConfig config{"127.0.0.1", 1883, "user", "pass", id};
        TEST_ASSERT_FALSE(client.begin(config, &sm, output, &clock));
    }

    char too_long[MQTT_DEVICE_ID_BUFFER_SIZE + 1];
    memset(too_long, 'a', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    MqttConfig boundary{"127.0.0.1", 1883, "user", "pass", too_long};
    TEST_ASSERT_FALSE(client.begin(boundary, &sm, output, &clock));
}

void test_mqtt_connect_is_atomic_on_publish_or_subscribe_failure(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;
    ScheduleManager sm;
    TEST_ASSERT_TRUE(sm.begin(&repo, &clock, &relay, &wdt, &runner));
    MqttClient client;
    MqttConfig config{"127.0.0.1", 1883, "user", "pass", "valid-device"};
    TEST_ASSERT_TRUE(client.begin(config, &sm, &relay, &clock));

    client.setMockPublishResult(false);
    TEST_ASSERT_FALSE(client.connect());
    TEST_ASSERT_FALSE(client.isConnected());

    client.setMockPublishResult(true);
    client.setMockSubscribeResult(false);
    TEST_ASSERT_FALSE(client.connect());
    TEST_ASSERT_FALSE(client.isConnected());
}

void test_mqtt_task_create_failure_rolls_back_facade_state(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;
    ScheduleManager sm;
    TEST_ASSERT_TRUE(sm.begin(&repo, &clock, &relay, &wdt, &runner));

    MqttClient client;
    MqttConfig config{"127.0.0.1", 1883, "user", "pass", "valid-device"};
    TEST_ASSERT_TRUE(client.begin(config, &sm, &relay, &clock));
    TEST_ASSERT_TRUE(client.isInitialized());
    TEST_ASSERT_TRUE(client.connect());
    TEST_ASSERT_TRUE(client.isConnected());

    // Failure injection for xTaskCreatePinnedToCore: false must invoke the
    // same startup finalizer used by main.cpp, not merely a direct reset.
    TEST_ASSERT_FALSE(finalizeMqttTaskStartup(client, false));
    TEST_ASSERT_FALSE(client.isInitialized());
    TEST_ASSERT_FALSE(client.isConnected());
    TEST_ASSERT_FALSE(client.connect());
}

void test_mqtt_reconnect_backoff_logic(void) {
    MqttTaskState state;
    TEST_ASSERT_TRUE(mqttReconnectDue(state, 0));
    mqttRecordReconnectAttempt(state, 0);
    mqttRecordReconnectFailure(state);
    TEST_ASSERT_EQUAL_UINT32(2, state.backoff_s);
    TEST_ASSERT_FALSE(mqttReconnectDue(state, 1999));
    TEST_ASSERT_TRUE(mqttReconnectDue(state, 2000));

    for (int i = 0; i < 6; ++i) mqttRecordReconnectFailure(state);
    TEST_ASSERT_EQUAL_UINT32(MQTT_RECONNECT_MAX_S, state.backoff_s);

    mqttRecordReconnectSuccess(state, 5000);
    TEST_ASSERT_EQUAL_UINT32(MQTT_RECONNECT_BASE_S, state.backoff_s);
    TEST_ASSERT_TRUE(state.was_connected);
    TEST_ASSERT_EQUAL_UINT32(5000, state.last_heartbeat_ms);

    mqttRecordWifiLoss(state);
    TEST_ASSERT_FALSE(state.was_connected);
    TEST_ASSERT_EQUAL_UINT32(MQTT_RECONNECT_BASE_S, state.backoff_s);
    TEST_ASSERT_TRUE(mqttReconnectDue(state, 5000));
    TEST_ASSERT_TRUE(mqttHeartbeatDue(state, 15000));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_fake_relay_override);
    RUN_TEST(test_override_pauses_auto_timer_spraying);
    RUN_TEST(test_override_pauses_auto_timer_cooldown);
    RUN_TEST(test_fake_clock_night_mode);
    RUN_TEST(test_profile_repository_validation);
    RUN_TEST(test_nvs_storage_not_found_uses_safe_defaults_successfully);
    RUN_TEST(test_nvs_storage_open_error_returns_false_and_safe_defaults);
    RUN_TEST(test_nvs_storage_each_get_error_returns_false_and_safe_defaults);
    RUN_TEST(test_nvs_storage_load_all_reports_any_production_read_error);
    RUN_TEST(test_schedule_manager_di_and_step);
    RUN_TEST(test_schedule_manager_uses_composition_root_boot_snapshot);
    RUN_TEST(test_emergency_fault_latching);
    RUN_TEST(test_schedule_manager_step_failure_propagation);
    RUN_TEST(test_schedule_manager_update_profile_rejection);
    RUN_TEST(test_profile_save_failure_keeps_ram_and_repository_consistent);
    RUN_TEST(test_profile_save_contention_does_not_latch_relay_or_starve_wdt);
    RUN_TEST(test_relay_iteration_feeds_wdt_before_stop_check_and_stops_within_one_tick);
    RUN_TEST(test_task_creation_fault_injection_and_rollback);
    RUN_TEST(test_task_startup_uses_configured_timeout);
    RUN_TEST(test_callback_exit_during_task_creation_rolls_back_safely);
    RUN_TEST(test_rollback_timeout_keeps_manager_lifetime_pending_and_latches_relays);
    RUN_TEST(test_wdt_registration_failure_rolls_back_without_affecting_main_wdt);
    RUN_TEST(test_duplicate_start_all_tasks_rejection);
    RUN_TEST(test_get_runtime_state_safely_validation);
    RUN_TEST(test_mqtt_client_connect_and_lwt);
    RUN_TEST(test_mqtt_client_publish_heartbeat);
    RUN_TEST(test_mqtt_client_publish_relay_telemetry);
    RUN_TEST(test_mqtt_client_on_message);
    RUN_TEST(test_mqtt_command_validation_rejects_untrusted_input);
    RUN_TEST(test_mqtt_callback_enforces_payload_length_contract);
    RUN_TEST(test_mqtt_config_provider_load);
    RUN_TEST(test_mqtt_config_rejects_unsafe_device_id);
    RUN_TEST(test_mqtt_connect_is_atomic_on_publish_or_subscribe_failure);
    RUN_TEST(test_mqtt_task_create_failure_rolls_back_facade_state);
    RUN_TEST(test_mqtt_reconnect_backoff_logic);
    return UNITY_END();
}
