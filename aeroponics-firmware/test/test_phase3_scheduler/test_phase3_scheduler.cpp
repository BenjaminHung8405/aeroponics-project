#include <unity.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <map>
#include <string>
#include <algorithm>

#include "group_scheduler.h"
#include "node_registry.h"
#include "pump_node_controller.h"
#include "nvs_storage.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeRfTransport.h"

namespace {

class InMemoryNvsBackend final : public INvsBackend {
public:
    Result flashInit() override { return 0; }
    Result flashErase() override {
        storage_.clear();
        pending_.clear();
        blob_storage_.clear();
        pending_blobs_.clear();
        return 0;
    }
    bool isOk(Result r) const override { return r == 0; }
    bool isNotFound(Result r) const override { return r == 1; }
    bool requiresFlashErase(Result) const override { return false; }
    const char* errorName(Result) const override { return "OK"; }

    Result open(const char*, bool, Handle& h) override { h = 1; return 0; }
    Result getU32(Handle, const char* key, uint32_t& value) override {
        auto it = storage_.find(key);
        if (it == storage_.end()) return 1;
        value = it->second;
        return 0;
    }
    Result setU32(Handle, const char* key, uint32_t value) override {
        if (fail_writes_) return -1;
        pending_[key] = value;
        return 0;
    }
    Result getBlob(Handle, const char* key, void* out_data, size_t* inout_len) override {
        auto it = blob_storage_.find(key);
        if (it == blob_storage_.end()) return 1;
        if (out_data && inout_len) {
            size_t to_copy = std::min(*inout_len, it->second.size());
            std::memcpy(out_data, it->second.data(), to_copy);
            *inout_len = to_copy;
        }
        return 0;
    }
    Result setBlob(Handle, const char* key, const void* data, size_t len) override {
        if (fail_writes_) return -1;
        pending_blobs_[key] = std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(data),
                                                  reinterpret_cast<const uint8_t*>(data) + len);
        return 0;
    }
    Result commit(Handle) override {
        if (fail_commit_) return -1;
        commit_calls_++;
        for (const auto& kv : pending_) {
            storage_[kv.first] = kv.second;
        }
        pending_.clear();
        for (const auto& kv : pending_blobs_) {
            blob_storage_[kv.first] = kv.second;
        }
        pending_blobs_.clear();
        return 0;
    }
    Result eraseAll(Handle) override {
        storage_.clear();
        pending_.clear();
        blob_storage_.clear();
        pending_blobs_.clear();
        return 0;
    }
    void close(Handle) override {}

    void setFailWrites(bool fail) { fail_writes_ = fail; }
    void setFailCommit(bool fail) { fail_commit_ = fail; }
    uint32_t commitCalls() const { return commit_calls_; }

private:
    std::map<std::string, uint32_t> storage_;
    std::map<std::string, uint32_t> pending_;
    std::map<std::string, std::vector<uint8_t>> blob_storage_;
    std::map<std::string, std::vector<uint8_t>> pending_blobs_;
    bool fail_writes_ = false;
    bool fail_commit_ = false;
    uint32_t commit_calls_ = 0;
};

bool provisionTestPsk(PumpNodeController& controller) {
    const uint8_t test_psk[16] = {0xA5};
    return controller.setPskKey(test_psk, sizeof(test_psk));
}

bool provisionTestNodePolicy(PumpNodeController& controller, uint8_t node_id, uint16_t min_flow = 50,
                             uint16_t max_off_flow = 20, uint16_t max_flow = 600,
                             uint32_t flow_timeout_ms = 3000) {
    const FlowPolicyProvenance provenance{1, static_cast<uint32_t>(100 + node_id),
                                          static_cast<uint32_t>(1000 + node_id)};
    return controller.provisionNodeControlPolicy(node_id, 60000, 300000, min_flow, max_off_flow,
                                                 max_flow, flow_timeout_ms, provenance);
}

} // namespace

void setUp(void) {}
void tearDown(void) {}

/**
 * TEST VECTOR 1:
 * Nominal 10s ON / 30s COOLDOWN continuous cycle across 5 consecutive cycles.
 * Proves ZERO cycle skipping, exact relative cooldown timing, and NO distortion to 120s.
 */
void test_vector_1_nominal_continuous_cycle_exact_relative_cooldown(void) {
    FakeClock clock(10, true);
    clock.setTime(10, 0, 0, true); // 10:00:00 ICT (Day Mode)

    NodeRegistry registry;
    TEST_ASSERT_TRUE(registry.init());
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    registry.refreshLiveness(1, 1000);

    GroupScheduler scheduler;
    TEST_ASSERT_TRUE(scheduler.begin(&clock, &registry));

    int64_t current_time_us = 1000000000LL; // 1000.0s baseline
    scheduler.setMockMonotonicTimeUs(current_time_us);

    // Day: 10s spray / 30s cooldown (Total cycle = 40s)
    // Night: 5s spray / 30s cooldown (Total cycle = 35s)
    PublishedTreatmentAssignment treat{100, 1, 1, {10, 30, 5, 30}};
    TEST_ASSERT_TRUE(scheduler.applyPublishedTreatment(1, treat));

    // Verify initial cycle begins in SPRAYING
    TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());
    GroupRuntimeState st{};
    TEST_ASSERT_TRUE(scheduler.getGroupRuntimeState(1, st));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(GroupPhase::PHASE_SPRAYING), static_cast<uint8_t>(st.current_phase));
    TEST_ASSERT_EQUAL_UINT32(10, st.phase_remaining_s);

    NodeState node_st{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, node_st));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(NodePumpState::ON), static_cast<uint8_t>(node_st.desired_state));

    const uint32_t NUM_CYCLES = 5;
    for (uint32_t cycle = 1; cycle <= NUM_CYCLES; ++cycle) {
        // --- 1. SPRAYING PHASE (10 seconds) ---
        for (uint32_t s = 1; s <= 10; ++s) {
            current_time_us += 1000000LL;
            scheduler.setMockMonotonicTimeUs(current_time_us);
            TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());
            TEST_ASSERT_TRUE(scheduler.getGroupRuntimeState(1, st));

            if (s < 10) {
                TEST_ASSERT_EQUAL(static_cast<uint8_t>(GroupPhase::PHASE_SPRAYING), static_cast<uint8_t>(st.current_phase));
                TEST_ASSERT_EQUAL_UINT32(10 - s, st.phase_remaining_s);
                TEST_ASSERT_TRUE(registry.getNodeState(1, node_st));
                TEST_ASSERT_EQUAL(static_cast<uint8_t>(NodePumpState::ON), static_cast<uint8_t>(node_st.desired_state));
            } else {
                // At s = 10, spray boundary hit: switches to COOLDOWN
                TEST_ASSERT_EQUAL(static_cast<uint8_t>(GroupPhase::PHASE_COOLING_DOWN), static_cast<uint8_t>(st.current_phase));
                TEST_ASSERT_EQUAL_UINT32(30, st.phase_remaining_s);
                TEST_ASSERT_TRUE(registry.getNodeState(1, node_st));
                TEST_ASSERT_EQUAL(static_cast<uint8_t>(NodePumpState::OFF), static_cast<uint8_t>(node_st.desired_state));
            }
        }

        // Notify pump cutoff completed at boundary
        scheduler.notifyPumpCutoff(1, current_time_us);

        // --- 2. COOLDOWN PHASE (30 seconds) ---
        for (uint32_t s = 1; s <= 30; ++s) {
            current_time_us += 1000000LL;
            scheduler.setMockMonotonicTimeUs(current_time_us);
            TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());
            TEST_ASSERT_TRUE(scheduler.getGroupRuntimeState(1, st));

            if (s < 30) {
                TEST_ASSERT_EQUAL(static_cast<uint8_t>(GroupPhase::PHASE_COOLING_DOWN), static_cast<uint8_t>(st.current_phase));
                TEST_ASSERT_TRUE(registry.getNodeState(1, node_st));
                TEST_ASSERT_EQUAL(static_cast<uint8_t>(NodePumpState::OFF), static_cast<uint8_t>(node_st.desired_state));
            } else {
                // At s = 30, exactly 30s after cutoff, enters next SPRAYING phase
                TEST_ASSERT_EQUAL(static_cast<uint8_t>(GroupPhase::PHASE_SPRAYING), static_cast<uint8_t>(st.current_phase));
                TEST_ASSERT_EQUAL_UINT32(10, st.phase_remaining_s);
                TEST_ASSERT_TRUE(registry.getNodeState(1, node_st));
                TEST_ASSERT_EQUAL(static_cast<uint8_t>(NodePumpState::ON), static_cast<uint8_t>(node_st.desired_state));
            }
        }
    }

    // Final assertion: 5 cycles ran in exactly 5 * (10s + 30s) = 200s
    // ZERO cycle skipping observed!
    TEST_ASSERT_EQUAL_INT64(1000000000LL + 200000000LL, current_time_us);
}

/**
 * TEST VECTOR 2:
 * Safe Schedule Hot-Reload during SPRAYING (mid-cycle at t=5s of 10s spray).
 * Proves current spray completes full 10s duration without timer cut,
 * commits to NVS immediately, marks pending_schedule=true,
 * and atomically commits pending profile to active at pump cutoff boundary.
 */
void test_vector_2_hot_reload_during_spraying_double_buffer(void) {
    InMemoryNvsBackend nvs_backend;
    NvsStorage storage(&nvs_backend, "aeroponics");
    TEST_ASSERT_TRUE(storage.begin());

    FakeClock clock(10, true);
    clock.setTime(11, 0, 0, true);

    NodeRegistry registry;
    TEST_ASSERT_TRUE(registry.init());
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    registry.refreshLiveness(1, 1000);

    GroupScheduler scheduler;
    TEST_ASSERT_TRUE(scheduler.begin(&clock, &registry, nullptr, nullptr, nullptr, &storage));

    int64_t current_time_us = 2000000000LL;
    scheduler.setMockMonotonicTimeUs(current_time_us);

    // Initial treatment: 10s ON / 30s OFF
    PublishedTreatmentAssignment initial_treat{100, 1, 1, {10, 30, 10, 30}};
    TEST_ASSERT_TRUE(scheduler.applyPublishedTreatment(1, initial_treat));

    TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());
    GroupRuntimeState st{};
    TEST_ASSERT_TRUE(scheduler.getGroupRuntimeState(1, st));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(GroupPhase::PHASE_SPRAYING), static_cast<uint8_t>(st.current_phase));
    TEST_ASSERT_EQUAL_UINT32(10, st.phase_remaining_s);
    TEST_ASSERT_FALSE(st.has_pending_schedule);

    // Advance 5 seconds into spray (halfway through 10s spray)
    current_time_us += 5000000LL;
    scheduler.setMockMonotonicTimeUs(current_time_us);
    TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());
    TEST_ASSERT_TRUE(scheduler.getGroupRuntimeState(1, st));
    TEST_ASSERT_EQUAL_UINT32(5, st.phase_remaining_s);

    // Now hot-reload a new schedule: 15s ON / 45s OFF
    PublishedTreatmentAssignment new_treat{101, 1, 2, {15, 45, 10, 45}};
    TEST_ASSERT_TRUE(scheduler.applyPublishedTreatment(1, new_treat));

    // Verify NVS was committed immediately
    PersistentGroupScheduleRecord rec{};
    size_t rec_len = sizeof(rec);
    TEST_ASSERT_TRUE(storage.getBlob("tr_grp1", &rec, &rec_len));
    TEST_ASSERT_EQUAL_UINT32(15, rec.spray_day_s);
    TEST_ASSERT_EQUAL_UINT32(45, rec.cooldown_day_s);

    // Verify active profile was NOT interrupted mid-spray
    TEST_ASSERT_TRUE(scheduler.hasPendingSchedule(1));
    TEST_ASSERT_TRUE(scheduler.getGroupRuntimeState(1, st));
    TEST_ASSERT_TRUE(st.has_pending_schedule);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(GroupPhase::PHASE_SPRAYING), static_cast<uint8_t>(st.current_phase));
    TEST_ASSERT_EQUAL_UINT32(10, st.profile.spray_day_s); // still 10s active!
    TEST_ASSERT_EQUAL_UINT32(5, st.phase_remaining_s);    // still 5s remaining in current spray!

    // Node pump must remain ON
    NodeState node_st{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, node_st));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(NodePumpState::ON), static_cast<uint8_t>(node_st.desired_state));

    // Advance remaining 5 seconds to finish the initial 10s spray
    current_time_us += 5000000LL;
    scheduler.setMockMonotonicTimeUs(current_time_us);
    TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());

    // Cutoff boundary reached: notify cutoff
    scheduler.notifyPumpCutoff(1, current_time_us);

    // Step again to verify atomic transition to pending profile
    TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());
    TEST_ASSERT_TRUE(scheduler.getGroupRuntimeState(1, st));

    // Assert: Pending profile committed to active!
    TEST_ASSERT_FALSE(st.has_pending_schedule);
    TEST_ASSERT_FALSE(scheduler.hasPendingSchedule(1));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(GroupPhase::PHASE_COOLING_DOWN), static_cast<uint8_t>(st.current_phase));
    TEST_ASSERT_EQUAL_UINT32(15, st.profile.spray_day_s);
    TEST_ASSERT_EQUAL_UINT32(45, st.profile.cooldown_day_s);
    TEST_ASSERT_EQUAL_UINT32(45, st.phase_remaining_s); // New cooldown duration applied!

    // Advance through 45 seconds of new cooldown
    current_time_us += 45000000LL;
    scheduler.setMockMonotonicTimeUs(current_time_us);
    TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());
    TEST_ASSERT_TRUE(scheduler.getGroupRuntimeState(1, st));

    // Next spray begins with new 15s duration!
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(GroupPhase::PHASE_SPRAYING), static_cast<uint8_t>(st.current_phase));
    TEST_ASSERT_EQUAL_UINT32(15, st.phase_remaining_s);
}

/**
 * TEST VECTOR 3:
 * Safe Schedule Hot-Reload during COOLDOWN and Minimum Dwell Time enforcement (>= 10s).
 * Proves anti-chattering and water hammer protection: even if a new schedule has
 * shorter remaining cooldown than minimum dwell time, pump will NEVER turn on before 10s dwell time.
 */
void test_vector_3_hot_reload_during_cooldown_minimum_dwell_time(void) {
    FakeClock clock(10, true);
    clock.setTime(12, 0, 0, true);

    NodeRegistry registry;
    TEST_ASSERT_TRUE(registry.init());
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    registry.refreshLiveness(1, 1000);

    GroupScheduler scheduler;
    TEST_ASSERT_TRUE(scheduler.begin(&clock, &registry));

    int64_t current_time_us = 3000000000LL;
    scheduler.setMockMonotonicTimeUs(current_time_us);

    // Initial treatment: 10s spray / 60s cooldown
    PublishedTreatmentAssignment treat1{100, 1, 1, {10, 60, 10, 60}};
    TEST_ASSERT_TRUE(scheduler.applyPublishedTreatment(1, treat1));

    // Start spray and complete 10s
    TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());
    current_time_us += 10000000LL;
    scheduler.setMockMonotonicTimeUs(current_time_us);
    TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());

    // Cutoff completed at t_cutoff
    int64_t t_cutoff_us = current_time_us;
    scheduler.notifyPumpCutoff(1, t_cutoff_us);

    // Group is in COOLDOWN. Advance 25 seconds into cooldown (of 60s)
    current_time_us += 25000000LL;
    scheduler.setMockMonotonicTimeUs(current_time_us);
    TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());

    GroupRuntimeState st{};
    TEST_ASSERT_TRUE(scheduler.getGroupRuntimeState(1, st));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(GroupPhase::PHASE_COOLING_DOWN), static_cast<uint8_t>(st.current_phase));

    // Hot-reload a new treatment with cooldown = 30s
    // Notice: 25s has already elapsed. New cooldown is 30s.
    // Mathematical difference: 30s - 25s = 5s!
    // However, MINIMUM_DWELL_TIME_S = 10s!
    // FSM must clamp remaining cooldown to at least 10s (dwell_min) to avoid premature switch-on!
    PublishedTreatmentAssignment treat2{101, 1, 2, {10, 30, 10, 30}};
    TEST_ASSERT_TRUE(scheduler.applyPublishedTreatment(1, treat2));

    // Step scheduler: remaining cooldown must be clamped to MINIMUM_DWELL_TIME_S (10s)
    TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());
    TEST_ASSERT_TRUE(scheduler.getGroupRuntimeState(1, st));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(GroupPhase::PHASE_COOLING_DOWN), static_cast<uint8_t>(st.current_phase));
    TEST_ASSERT_EQUAL_UINT32(10, st.phase_remaining_s);

    // Advance 9 seconds: still in cooldown, pump remains strictly OFF
    current_time_us += 9000000LL;
    scheduler.setMockMonotonicTimeUs(current_time_us);
    TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());
    TEST_ASSERT_TRUE(scheduler.getGroupRuntimeState(1, st));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(GroupPhase::PHASE_COOLING_DOWN), static_cast<uint8_t>(st.current_phase));
    TEST_ASSERT_EQUAL_UINT32(1, st.phase_remaining_s);

    NodeState node_st{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, node_st));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(NodePumpState::OFF), static_cast<uint8_t>(node_st.desired_state));

    // Advance 1 more second (total 10s dwell satisfied: now transitions safely to SPRAYING)
    current_time_us += 1000000LL;
    scheduler.setMockMonotonicTimeUs(current_time_us);
    TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());
    TEST_ASSERT_TRUE(scheduler.getGroupRuntimeState(1, st));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(GroupPhase::PHASE_SPRAYING), static_cast<uint8_t>(st.current_phase));
    TEST_ASSERT_TRUE(registry.getNodeState(1, node_st));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(NodePumpState::ON), static_cast<uint8_t>(node_st.desired_state));
}

/**
 * TEST VECTOR 4:
 * Node reassignment during spraying: immediate SAFE_OFF prior to group mapping mutation.
 * Proves that unassigning or reassigning a running node never leaves pump running stranded.
 */
void test_vector_4_node_reassignment_during_spraying_forces_immediate_safe_off(void) {
    FakeClock clock(10, true);
    clock.setTime(13, 0, 0, true);

    NodeRegistry registry;
    TEST_ASSERT_TRUE(registry.init());
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    registry.refreshLiveness(1, 1000);

    FakeRfTransport rf;
    TEST_ASSERT_TRUE(rf.begin());
    PumpNodeController controller;
    TEST_ASSERT_TRUE(controller.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(controller));
    TEST_ASSERT_TRUE(provisionTestNodePolicy(controller, 1));

    GroupScheduler scheduler;
    TEST_ASSERT_TRUE(scheduler.begin(&clock, &registry, nullptr, nullptr, &controller));

    int64_t current_time_us = 4000000000LL;
    scheduler.setMockMonotonicTimeUs(current_time_us);

    PublishedTreatmentAssignment treat1{100, 1, 1, {10, 30, 10, 30}};
    TEST_ASSERT_TRUE(scheduler.applyPublishedTreatment(1, treat1));

    // Step into SPRAYING: Node 1 is ON
    TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());
    NodeState st1{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, st1));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(NodePumpState::ON), static_cast<uint8_t>(st1.desired_state));

    // Reassign Node 1 by unassigning it first (group_id = 0)
    TEST_ASSERT_TRUE(scheduler.unassignNodeVersioned(1, 2, "operator_test", 1000));

    // Verify Node 1 was immediately set to SAFE_OFF
    TEST_ASSERT_TRUE(registry.getNodeState(1, st1));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(NodePumpState::OFF), static_cast<uint8_t>(st1.desired_state));
    TEST_ASSERT_EQUAL_UINT8(0, registry.getNodeGroup(1));

    // Verify PUMP_OFF command was dispatched via controller
    TEST_ASSERT_TRUE(controller.isPending(1));

    // Now assign to Group 2 with version 3
    VersionedGroupAssignment assign_grp2{3, 1, 2, 1050, "operator_test"};
    TEST_ASSERT_TRUE(scheduler.assignNodeVersioned(assign_grp2));
    TEST_ASSERT_EQUAL_UINT8(2, registry.getNodeGroup(1));
    TEST_ASSERT_TRUE(registry.getNodeState(1, st1));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(NodePumpState::OFF), static_cast<uint8_t>(st1.desired_state));
}

/**
 * TEST VECTOR 5:
 * Decoupled flow verification: Transaction & Actuation Complete.
 * Proves that state transitions proceed immediately without blocking on flow confirmation.
 */
void test_vector_5_decoupled_flow_verification_transaction_complete(void) {
    FakeClock clock(10, true);
    clock.setTime(14, 0, 0, true);

    NodeRegistry registry;
    TEST_ASSERT_TRUE(registry.init());
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    registry.refreshLiveness(1, 1000);

    // Node flow is 0 LPM (physical sensor removed/decommissioned)
    NodeState st{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, st));
    TEST_ASSERT_EQUAL_UINT16(0, st.flow_lpm_x100);

    GroupScheduler scheduler;
    TEST_ASSERT_TRUE(scheduler.begin(&clock, &registry));

    int64_t current_time_us = 5000000000LL;
    scheduler.setMockMonotonicTimeUs(current_time_us);

    PublishedTreatmentAssignment treat{100, 1, 1, {10, 30, 10, 30}};
    TEST_ASSERT_TRUE(scheduler.applyPublishedTreatment(1, treat));

    // Step into SPRAYING: must transition cleanly without waiting for flow sensor
    TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());
    GroupRuntimeState grp_st{};
    TEST_ASSERT_TRUE(scheduler.getGroupRuntimeState(1, grp_st));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(GroupPhase::PHASE_SPRAYING), static_cast<uint8_t>(grp_st.current_phase));

    // Run 10 seconds of spray with 0 LPM flow
    for (int i = 0; i < 10; ++i) {
        current_time_us += 1000000LL;
        scheduler.setMockMonotonicTimeUs(current_time_us);
        TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());
    }

    // Must transition to COOLDOWN cleanly without timeout errors
    TEST_ASSERT_TRUE(scheduler.getGroupRuntimeState(1, grp_st));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(GroupPhase::PHASE_COOLING_DOWN), static_cast<uint8_t>(grp_st.current_phase));
    TEST_ASSERT_EQUAL_UINT32(30, grp_st.phase_remaining_s);
}

/**
 * TEST VECTOR 6:
 * Day / Night boundary evaluated strictly at cycle boundary.
 * Proves that mid-cycle RTC transitions do NOT cause intra-cycle phase jitter or timer distortion.
 */
void test_vector_6_day_night_boundary_evaluated_at_cycle_boundary(void) {
    FakeClock clock(10, true);
    // Start at 17:59:45 ICT (Day mode: 06:00 - 18:00)
    clock.setTime(17, 59, 45, true);

    NodeRegistry registry;
    TEST_ASSERT_TRUE(registry.init());
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    registry.refreshLiveness(1, 1000);

    GroupScheduler scheduler;
    TEST_ASSERT_TRUE(scheduler.begin(&clock, &registry));

    int64_t current_time_us = 6000000000LL;
    scheduler.setMockMonotonicTimeUs(current_time_us);

    // Profile: Day = 10s ON / 30s OFF; Night = 5s ON / 50s OFF
    PublishedTreatmentAssignment treat{100, 1, 1, {10, 30, 5, 50}};
    TEST_ASSERT_TRUE(scheduler.applyPublishedTreatment(1, treat));

    // 1. Begin Day cycle: Spraying for 10s
    TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());
    GroupRuntimeState st{};
    TEST_ASSERT_TRUE(scheduler.getGroupRuntimeState(1, st));
    TEST_ASSERT_FALSE(st.is_night_mode);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(GroupPhase::PHASE_SPRAYING), static_cast<uint8_t>(st.current_phase));
    TEST_ASSERT_EQUAL_UINT32(10, st.phase_remaining_s);

    // Advance 10s: spray completes at 17:59:55
    current_time_us += 10000000LL;
    clock.setTime(17, 59, 55, true);
    scheduler.setMockMonotonicTimeUs(current_time_us);
    TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());
    scheduler.notifyPumpCutoff(1, current_time_us);

    // Now in Day COOLDOWN (30s duration)
    TEST_ASSERT_TRUE(scheduler.getGroupRuntimeState(1, st));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(GroupPhase::PHASE_COOLING_DOWN), static_cast<uint8_t>(st.current_phase));
    TEST_ASSERT_EQUAL_UINT32(30, st.phase_remaining_s);

    // 2. Mid-cooldown: clock crosses 18:00:00 (Night Mode) at 10s into cooldown
    current_time_us += 10000000LL;
    clock.setTime(18, 0, 5, true); // Now officially night time!
    scheduler.setMockMonotonicTimeUs(current_time_us);
    TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());

    // Intra-cycle assertion: active cooldown must NOT be corrupted or jumped to night cooldown (50s)
    TEST_ASSERT_TRUE(scheduler.getGroupRuntimeState(1, st));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(GroupPhase::PHASE_COOLING_DOWN), static_cast<uint8_t>(st.current_phase));
    TEST_ASSERT_EQUAL_UINT32(20, st.phase_remaining_s); // exactly 20s remaining of Day cooldown!

    // 3. Complete remaining 20s of Day cooldown
    current_time_us += 20000000LL;
    clock.setTime(18, 0, 25, true);
    scheduler.setMockMonotonicTimeUs(current_time_us);
    TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());

    // 4. Cycle boundary reached: Night mode is evaluated and loaded
    TEST_ASSERT_TRUE(scheduler.getGroupRuntimeState(1, st));
    TEST_ASSERT_TRUE(st.is_night_mode); // Evaluated at cycle boundary!
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(GroupPhase::PHASE_SPRAYING), static_cast<uint8_t>(st.current_phase));
    TEST_ASSERT_EQUAL_UINT32(5, st.phase_remaining_s); // Night spray duration (5s)!

    // Complete 5s Night spray
    current_time_us += 5000000LL;
    scheduler.setMockMonotonicTimeUs(current_time_us);
    TEST_ASSERT_TRUE(scheduler.stepGroupSchedule());
    scheduler.notifyPumpCutoff(1, current_time_us);

    // Enters Night COOLDOWN (50s)
    TEST_ASSERT_TRUE(scheduler.getGroupRuntimeState(1, st));
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(GroupPhase::PHASE_COOLING_DOWN), static_cast<uint8_t>(st.current_phase));
    TEST_ASSERT_EQUAL_UINT32(50, st.phase_remaining_s); // Night cooldown duration (50s)!
}

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;
    UNITY_BEGIN();
    RUN_TEST(test_vector_1_nominal_continuous_cycle_exact_relative_cooldown);
    RUN_TEST(test_vector_2_hot_reload_during_spraying_double_buffer);
    RUN_TEST(test_vector_3_hot_reload_during_cooldown_minimum_dwell_time);
    RUN_TEST(test_vector_4_node_reassignment_during_spraying_forces_immediate_safe_off);
    RUN_TEST(test_vector_5_decoupled_flow_verification_transaction_complete);
    RUN_TEST(test_vector_6_day_night_boundary_evaluated_at_cycle_boundary);
    return UNITY_END();
}
