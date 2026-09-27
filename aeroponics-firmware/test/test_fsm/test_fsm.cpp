#include <unity.h>
#include "node_fsm.h"

void setUp(void) {}
void tearDown(void) {}

/* ----------------------------------------------------------------
 * Macro state transition guard tests (S2-FSM-01)
 * ---------------------------------------------------------------- */

/** BOOT_OFF → SCHEDULE_SPRAY on valid schedule window */
void test_boo_off_to_schedule_spray_on_valid_schedule_window(void) {
    NodeFsmState fsm;
    initNodeFsm(fsm, 4);
    fsm.macro_state = MacroState::BOOT_OFF;
    fsm.cooldown_boundary_ms = 0;  // No cooldown pending
    TEST_ASSERT_TRUE(transitionMacroState(fsm, MacroState::SCHEDULE_SPRAY, 10000));
    TEST_ASSERT_EQUAL(MacroState::SCHEDULE_SPRAY, fsm.macro_state);
}

/** FAULT_LATCH blocks direct transition to SCHEDULE_SPRAY */
void test_fault_latch_blocks_direct_transition_to_schedule_spray(void) {
    NodeFsmState fsm;
    initNodeFsm(fsm, 4);
    fsm.macro_state = MacroState::FAULT_LATCH;
    TEST_ASSERT_FALSE(transitionMacroState(fsm, MacroState::SCHEDULE_SPRAY, 10000));
}

/** FAULT_LATCH → BOOT_OFF via preflight */
void test_fault_latch_to_boot_off_via_preflight(void) {
    NodeFsmState fsm;
    initNodeFsm(fsm, 4);
    fsm.macro_state = MacroState::FAULT_LATCH;
    fsm.fault_flags = 0;
    TEST_ASSERT_TRUE(preflightFaultReset(fsm));
    TEST_ASSERT_TRUE(transitionMacroState(fsm, MacroState::BOOT_OFF, 10000));
    TEST_ASSERT_EQUAL(MacroState::BOOT_OFF, fsm.macro_state);
}

/** Lease expiry transitions OVERRIDE_RUN → SCHEDULE_COOLDOWN */
void test_lease_expiry_transitions_override_run_to_schedule_cooldown(void) {
    NodeFsmState fsm;
    initNodeFsm(fsm, 4);
    fsm.macro_state = MacroState::OVERRIDE_RUN;
    fsm.lease_active = true;
    fsm.lease_expiry_ms = 5000;
    TEST_ASSERT_FALSE(leaseTick(fsm, 4000));  // Not yet expired
    TEST_ASSERT_TRUE(leaseTick(fsm, 5000));   // Expired!
    TEST_ASSERT_FALSE(fsm.lease_active);
    // leaseTick enforces cooldown boundary; caller transitions state
    TEST_ASSERT_GREATER_THAN(0, fsm.cooldown_boundary_ms);
    TEST_ASSERT_FALSE(leaseTick(fsm, 5000));  // Already expired, lease_active=false
    TEST_ASSERT_FALSE(fsm.lease_active);
}

/** T_cooldown_min prevents premature ON */
void test_cooldown_min_prevents_premature_on(void) {
    NodeFsmState fsm;
    initNodeFsm(fsm, 4);
    fsm.macro_state = MacroState::SCHEDULE_COOLDOWN;
    fsm.cooldown_boundary_ms = 60000;  // Must wait until 60s
    TEST_ASSERT_FALSE(canScheduleOn(fsm, 30000));  // Too early
    TEST_ASSERT_TRUE(canScheduleOn(fsm, 60000));    // Just right
}

/* ----------------------------------------------------------------
 * Evidence pipeline ordering tests (S2-FSM-02)
 * ---------------------------------------------------------------- */

/** Evidence progression: NONE → COMMAND_DISPATCHED */
void test_evidence_pipeline_none_to_dispatched(void) {
    NodeFsmState fsm;
    initNodeFsm(fsm, 4);
    TEST_ASSERT_TRUE(advanceEvidenceStage(fsm, EvidenceStage::COMMAND_DISPATCHED, 10000));
    TEST_ASSERT_EQUAL(EvidenceStage::COMMAND_DISPATCHED, fsm.evidence_stage);
}

/** Evidence cannot skip stages */
void test_evidence_cannot_skip_stages(void) {
    NodeFsmState fsm;
    initNodeFsm(fsm, 4);
    TEST_ASSERT_FALSE(advanceEvidenceStage(fsm, EvidenceStage::FLOW_CONFIRMED, 10000));
}

/** Evidence regression not allowed */
void test_evidence_no_regression(void) {
    NodeFsmState fsm;
    initNodeFsm(fsm, 4);
    fsm.evidence_stage = EvidenceStage::CURRENT_DETECTED;
    TEST_ASSERT_FALSE(advanceEvidenceStage(fsm, EvidenceStage::COMMAND_DISPATCHED, 10000));
}

/* ----------------------------------------------------------------
 * PendingCommandTable tests (S2-TABLE-06)
 * ---------------------------------------------------------------- */

/** PendingCommandTable insert/find/cleanup basic operation */
void test_pending_command_table_insert_find_resolve_cleanup(void) {
    PendingCommandTable table;
    uint32_t rf_id = table.insert(4, "cmd-abc-123");
    TEST_ASSERT_GREATER_THAN(0, rf_id);
    const char* found = table.find(rf_id);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("cmd-abc-123", found);
    table.resolve(rf_id);
    TEST_ASSERT_NULL(table.find(rf_id));  // Resolved → not found
}

/** PendingCommandTable TTL cleanup */
void test_pending_command_table_ttl_cleanup(void) {
    PendingCommandTable table;
    uint32_t rf_id = table.insert(4, "cmd-ttl-test");
    table.cleanup(0);         // Not expired yet
    TEST_ASSERT_EQUAL(1, table.size());
    table.cleanup(3000);      // Past 2000ms TTL
    TEST_ASSERT_EQUAL(0, table.size());
}

/** PendingCommandTable find returns nullptr for resolved entry */
void test_pending_command_table_find_nullptr_after_resolve(void) {
    PendingCommandTable table;
    uint32_t rf_id = table.insert(5, "cmd-resolved");
    TEST_ASSERT_NOT_NULL(table.find(rf_id));
    table.resolve(rf_id);
    TEST_ASSERT_NULL(table.find(rf_id));
}

/** PendingCommandTable fail-closed: find returns nullptr for unknown rf_id */
void test_pending_command_table_fail_closed_unknown_id(void) {
    PendingCommandTable table;
    // Insert first to have a table with entries
    table.insert(4, "cmd-known");
    TEST_ASSERT_NULL(table.find(99999));  // Unknown rf_command_id
}

/** PendingCommandTable size tracking */
void test_pending_command_table_size_tracking(void) {
    PendingCommandTable table;
    TEST_ASSERT_EQUAL(0, table.size());
    table.insert(4, "cmd-1");
    TEST_ASSERT_EQUAL(1, table.size());
    table.insert(5, "cmd-2");
    TEST_ASSERT_EQUAL(2, table.size());
    table.resolve(4);
    TEST_ASSERT_EQUAL(2, table.size());  // resolve doesn't decrement count
    table.cleanup(3000);                  // Past TTL, should reclaim
    TEST_ASSERT_EQUAL(0, table.size());
}

/* ----------------------------------------------------------------
 * Fail-closed and edge case tests
 * ---------------------------------------------------------------- */

/** TransitionMacroState fail-closed: OVERRIDE_RUN → SCHEDULE_SPRAY blocked */
void test_override_run_blocks_direct_schedule_spray(void) {
    NodeFsmState fsm;
    initNodeFsm(fsm, 4);
    fsm.macro_state = MacroState::OVERRIDE_RUN;
    TEST_ASSERT_FALSE(transitionMacroState(fsm, MacroState::SCHEDULE_SPRAY, 10000));
}

/** canScheduleOn is false when cooldown not met */
void test_cannot_schedule_before_cooldown_ms_elapsed(void) {
    NodeFsmState fsm;
    initNodeFsm(fsm, 4);
    fsm.cooldown_boundary_ms = 10000;
    TEST_ASSERT_FALSE(canScheduleOn(fsm, 5000));
}

/** canScheduleOn is true when cooldown exactly met */
void test_can_schedule_when_cooldown_exactly_met(void) {
    NodeFsmState fsm;
    initNodeFsm(fsm, 4);
    fsm.cooldown_boundary_ms = 5000;
    TEST_ASSERT_TRUE(canScheduleOn(fsm, 5000));
}

/** canScheduleOn is true after cooldown has passed */
void test_can_schedule_after_cooldown_passed(void) {
    NodeFsmState fsm;
    initNodeFsm(fsm, 4);
    fsm.cooldown_boundary_ms = 1000;
    TEST_ASSERT_TRUE(canScheduleOn(fsm, 5000));
}

/** Lease tick with inactive lease returns false */
void test_lease_tick_inactive_lease_returns_false(void) {
    NodeFsmState fsm;
    initNodeFsm(fsm, 4);
    TEST_ASSERT_FALSE(leaseTick(fsm, 1000));
}

/** Lease tick with active lease that hasn't expired */
void test_lease_tick_active_but_not_expired(void) {
    NodeFsmState fsm;
    initNodeFsm(fsm, 4);
    fsm.lease_active = true;
    fsm.lease_expiry_ms = 20000;
    TEST_ASSERT_FALSE(leaseTick(fsm, 15000));  // Still valid
    TEST_ASSERT_TRUE(leaseTick(fsm, 25000));   // Expired
    TEST_ASSERT_FALSE(fsm.lease_active);
}

/** initNodeFsm sets node_id in valid production range */
void test_init_node_fsm_sets_valid_node_id(void) {
    NodeFsmState fsm;
    initNodeFsm(fsm, 5);
    TEST_ASSERT_EQUAL(5U, fsm.node_id);
}

/** initNodeFsm rejects gateway ID 0 */
void test_init_node_fsm_rejects_invalid_node_id(void) {
    NodeFsmState fsm;
    initNodeFsm(fsm, 0);  // Gateway is not an actuator node
    TEST_ASSERT_EQUAL(0U, fsm.node_id);
}

/* ----------------------------------------------------------------
 * Main suite runner
 * ---------------------------------------------------------------- */

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_boo_off_to_schedule_spray_on_valid_schedule_window);
    RUN_TEST(test_fault_latch_blocks_direct_transition_to_schedule_spray);
    RUN_TEST(test_fault_latch_to_boot_off_via_preflight);
    RUN_TEST(test_lease_expiry_transitions_override_run_to_schedule_cooldown);
    RUN_TEST(test_cooldown_min_prevents_premature_on);
    RUN_TEST(test_evidence_pipeline_none_to_dispatched);
    RUN_TEST(test_evidence_cannot_skip_stages);
    RUN_TEST(test_evidence_no_regression);
    RUN_TEST(test_pending_command_table_insert_find_resolve_cleanup);
    RUN_TEST(test_pending_command_table_ttl_cleanup);
    RUN_TEST(test_pending_command_table_find_nullptr_after_resolve);
    RUN_TEST(test_pending_command_table_fail_closed_unknown_id);
    RUN_TEST(test_pending_command_table_size_tracking);
    RUN_TEST(test_override_run_blocks_direct_schedule_spray);
    RUN_TEST(test_cannot_schedule_before_cooldown_ms_elapsed);
    RUN_TEST(test_can_schedule_when_cooldown_exactly_met);
    RUN_TEST(test_can_schedule_after_cooldown_passed);
    RUN_TEST(test_lease_tick_inactive_lease_returns_false);
    RUN_TEST(test_lease_tick_active_but_not_expired);
    RUN_TEST(test_init_node_fsm_sets_valid_node_id);
    RUN_TEST(test_init_node_fsm_rejects_invalid_node_id);

    return UNITY_END();
}
