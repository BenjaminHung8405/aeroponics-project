#include <unity.h>
#include <chrono>
#include <thread>
#include "legacy_relay_config.h"
#include "nvs_storage.h"
#include "schedule_manager.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeWatchdog.h"
#include "fakes/FakeNvsBackend.h"
#include "fakes/FakeRelayOutput.h"
#include "fakes/FakeProfileRepository.h"
#include "fakes/FakeTaskRunner.h"

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

    for (int i = 0; i < 4; ++i) {
        relay.tickOverride(0);
        TEST_ASSERT_TRUE(relay.isOverrideActive(0));
        TEST_ASSERT_EQUAL(RELAY_ON, relay.getRelayState(0));
    }

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
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));

    RelayRuntimeState st_initial = mgr.getRuntimeState(0);
    TEST_ASSERT_EQUAL(PHASE_SPRAYING, st_initial.phase);
    uint32_t remaining_before = st_initial.phase_remaining_s;

    TEST_ASSERT_TRUE(relay.startManualOverride(0, RELAY_OFF, 5));
    TEST_ASSERT_TRUE(relay.isOverrideActive(0));
    TEST_ASSERT_EQUAL(RELAY_OFF, relay.getRelayState(0));

    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT_TRUE(mgr.stepRelayPhase(0));
        RelayRuntimeState st = mgr.getRuntimeState(0);
        TEST_ASSERT_EQUAL(PHASE_SPRAYING, st.phase);
        TEST_ASSERT_EQUAL_UINT32(remaining_before, st.phase_remaining_s);
        TEST_ASSERT_TRUE(relay.isOverrideActive(0));
        TEST_ASSERT_EQUAL(RELAY_OFF, relay.getRelayState(0));
    }

    TEST_ASSERT_TRUE(mgr.stepRelayPhase(0));

    TEST_ASSERT_FALSE(relay.isOverrideActive(0));
    TEST_ASSERT_EQUAL(RELAY_ON, relay.getRelayState(0));
    RelayRuntimeState st_exp = mgr.getRuntimeState(0);
    TEST_ASSERT_EQUAL(PHASE_SPRAYING, st_exp.phase);
    TEST_ASSERT_EQUAL_UINT32(remaining_before, st_exp.phase_remaining_s);

    TEST_ASSERT_TRUE(mgr.stepRelayPhase(0));
    RelayRuntimeState st_resumed = mgr.getRuntimeState(0);
    TEST_ASSERT_EQUAL(PHASE_SPRAYING, st_resumed.phase);
    TEST_ASSERT_EQUAL_UINT32(remaining_before - 1, st_resumed.phase_remaining_s);
    TEST_ASSERT_EQUAL(RELAY_ON, relay.getRelayState(0));
}

void test_override_pauses_auto_timer_cooldown(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));

    for (uint32_t i = 0; i < DEFAULT_SPRAY_DAY_S; ++i) {
        TEST_ASSERT_TRUE(mgr.stepRelayPhase(0));
    }
    TEST_ASSERT_EQUAL(PHASE_COOLING_DOWN, mgr.getRuntimeState(0).phase);

    TEST_ASSERT_TRUE(relay.startManualOverride(0, RELAY_ON, 5));
    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT_TRUE(mgr.stepRelayPhase(0));
    }
    TEST_ASSERT_FALSE(relay.isOverrideActive(0));
    TEST_ASSERT_EQUAL(RELAY_OFF, relay.getRelayState(0));
}

void test_profile_repository_validation(void) {
    FakeProfileRepository repo;
    RelayProfile invalid_profile{ MIN_SPRAY_DURATION_S - 1, DEFAULT_COOLDOWN_DAY_S, DEFAULT_SPRAY_NIGHT_S, DEFAULT_COOLDOWN_NIGHT_S };
    TEST_ASSERT_FALSE(repo.saveProfile(0, invalid_profile));

    RelayProfile valid_profile{ 15, 120, 15, 300 };
    TEST_ASSERT_TRUE(repo.saveProfile(0, valid_profile));
}

void test_nvs_storage_not_found_uses_safe_defaults_successfully(void) {
    FakeNvsBackend backend;
    for (uint8_t f = 0; f < 4; ++f) backend.setGetResult(f, FakeNvsBackend::NOT_FOUND);

    NvsStorage storage(&backend);
    TEST_ASSERT_TRUE(storage.begin());

    RelayProfile profile{};
    TEST_ASSERT_TRUE(storage.loadProfile(0, profile));
    TEST_ASSERT_EQUAL_UINT32(DEFAULT_SPRAY_DAY_S, profile.spray_day_s);
    TEST_ASSERT_EQUAL_UINT32(DEFAULT_COOLDOWN_DAY_S, profile.cooldown_day_s);
    TEST_ASSERT_EQUAL_UINT32(DEFAULT_SPRAY_NIGHT_S, profile.spray_night_s);
    TEST_ASSERT_EQUAL_UINT32(DEFAULT_COOLDOWN_NIGHT_S, profile.cooldown_night_s);
}

void test_nvs_storage_open_error_returns_false_and_safe_defaults(void) {
    FakeNvsBackend backend;
    backend.setOpenResult(FakeNvsBackend::IO_ERROR);

    NvsStorage storage(&backend);
    TEST_ASSERT_TRUE(storage.begin());

    RelayProfile profile{};
    TEST_ASSERT_FALSE(storage.loadProfile(0, profile));
    TEST_ASSERT_EQUAL_UINT32(DEFAULT_SPRAY_DAY_S, profile.spray_day_s);
}

void test_nvs_storage_each_get_error_returns_false_and_safe_defaults(void) {
    FakeNvsBackend backend;
    for (uint8_t f = 0; f < 4; ++f) backend.setGetResult(f, FakeNvsBackend::IO_ERROR);

    NvsStorage storage(&backend);
    TEST_ASSERT_TRUE(storage.begin());

    RelayProfile profile{};
    TEST_ASSERT_FALSE(storage.loadProfile(0, profile));
    TEST_ASSERT_EQUAL_UINT32(DEFAULT_SPRAY_DAY_S, profile.spray_day_s);
}

void test_nvs_storage_load_all_reports_any_production_read_error(void) {
    FakeNvsBackend backend;
    NvsStorage storage(&backend);
    TEST_ASSERT_TRUE(storage.begin());

    RelayProfile profiles[TOTAL_RELAYS];
    for (uint8_t f = 0; f < 4; ++f) backend.setGetResult(f, FakeNvsBackend::IO_ERROR);
    TEST_ASSERT_FALSE(storage.loadAllProfiles(profiles));
}

void test_schedule_manager_di_and_step(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));
    TEST_ASSERT_TRUE(mgr.stepRelayPhase(0));
}

void test_schedule_manager_uses_composition_root_boot_snapshot(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    RelayProfile profiles[TOTAL_RELAYS];
    repo.loadAllProfiles(profiles);

    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner, profiles));
    TEST_ASSERT_EQUAL_UINT32(DEFAULT_SPRAY_DAY_S, mgr.getRuntimeState(0).phase_remaining_s);
}

void test_emergency_fault_latching(void) {
    FakeRelayOutput relay;
    TEST_ASSERT_FALSE(relay.isFaultLatched(0));
    TEST_ASSERT_TRUE(relay.forceRelayOffEmergency(0));
    TEST_ASSERT_TRUE(relay.isFaultLatched(0));
    TEST_ASSERT_EQUAL(RELAY_OFF, relay.getRelayState(0));
    TEST_ASSERT_FALSE(relay.setRelay(0, RELAY_ON));
}

void test_schedule_manager_step_failure_propagation(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));

    relay.setFailScheduledApply(0, true);
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

    RelayProfile invalid_p{ 1, 10, 1, 10 };
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

    repo.setSaveFailure(0, true);
    RelayProfile new_p{ 20, 200, 20, 400 };
    TEST_ASSERT_FALSE(mgr.updateProfile(0, new_p));
}

void test_profile_save_contention_does_not_latch_relay_or_starve_wdt(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));

    repo.blockSaves();
    RelayProfile p{ 15, 150, 15, 300 };
    std::thread writer([&]() { mgr.updateProfile(0, p); });

    TEST_ASSERT_TRUE(repo.waitForSaveToStart(1000));
    TEST_ASSERT_TRUE(mgr.stepRelayPhase(0));

    repo.releaseSaves();
    writer.join();
    TEST_ASSERT_FALSE(relay.isFaultLatched(0));
}

void test_relay_iteration_feeds_wdt_before_stop_check_and_stops_within_one_tick(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    OrderedWatchdog watchdog;
    OrderedStopRunner runner(watchdog);
    ScheduleManager mgr;

    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &watchdog, &runner));
    TEST_ASSERT_TRUE(mgr.initializeRelayTask(0));

    runner.requestStopOnNextCheck();
    TEST_ASSERT_FALSE(mgr.runRelayTaskIteration(0, 1));
}

void test_task_creation_fault_injection_and_rollback(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    runner.setStartTaskResult(0, false);
    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));
    TEST_ASSERT_FALSE(mgr.startAllTasks());
}

void test_task_startup_uses_configured_timeout(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    runner.setWaitUntilStartedResult(0, false);
    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));
    TEST_ASSERT_FALSE(mgr.startAllTasks());
    TEST_ASSERT_EQUAL_UINT32(RELAY_TASK_STARTUP_TIMEOUT_MS, runner.lastWaitUntilStartedTimeoutMs());
}

void test_callback_exit_during_task_creation_rolls_back_safely(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    runner.setWaitUntilStartedResult(0, false);
    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));
    TEST_ASSERT_FALSE(mgr.startAllTasks());
    TEST_ASSERT_EQUAL(ScheduleLifecycleState::FAULTED, mgr.getLifecycleState());
}

void test_rollback_timeout_keeps_manager_lifetime_pending_and_latches_relays(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    runner.setWaitUntilStartedResult(1, false);
    runner.setWaitUntilManagerCallbackExitedResult(0, false);

    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));
    TEST_ASSERT_FALSE(mgr.startAllTasks());
    TEST_ASSERT_TRUE(relay.isFaultLatched(0));
}

void test_wdt_registration_failure_rolls_back_without_affecting_main_wdt(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    wdt.setRegistrationFailure(0, true);
    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));
    TEST_ASSERT_FALSE(mgr.initializeRelayTask(0));
}

void test_duplicate_start_all_tasks_rejection(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));
    TEST_ASSERT_TRUE(mgr.startAllTasks());
    TEST_ASSERT_FALSE(mgr.startAllTasks());
}

void test_get_runtime_state_safely_validation(void) {
    FakeProfileRepository repo;
    FakeClock clock(10, true);
    FakeRelayOutput relay;
    FakeWatchdog wdt;
    FakeTaskRunner runner;

    ScheduleManager mgr;
    TEST_ASSERT_TRUE(mgr.begin(&repo, &clock, &relay, &wdt, &runner));

    RelayRuntimeState state{};
    TEST_ASSERT_FALSE(mgr.getRuntimeStateSafely(TOTAL_RELAYS, state));
    TEST_ASSERT_TRUE(mgr.getRuntimeStateSafely(0, state));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_fake_relay_override);
    RUN_TEST(test_override_pauses_auto_timer_spraying);
    RUN_TEST(test_override_pauses_auto_timer_cooldown);
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
    return UNITY_END();
}
