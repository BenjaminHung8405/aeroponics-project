#pragma once

#include <cstddef>
#include <cstdint>

#include "config.h"

// ============================================================================
// Virtual FSM Core — Data structures per STATE_MACHINE_MATRIX v1.0.0 §3-§5
// Track A (Sprint 2): clean data structures + transition logic only.
// Hard rules:
//  - No heap allocation (new/malloc) anywhere in this module.
//  - No hardcoded node_id; addresses flow in through initNodeFsm().
//  - Bounded RAM: the command correlation table stays static and well under
//    the 20 KB FSM budget.
// ============================================================================

// Local safety-timer bounds used by Track A. Track B promotes the canonical
// values into config.h (SECTION 13) and reuses these same numeric bounds.
namespace NodeFsmLimits {
inline constexpr uint32_t RUN_LEASE_MIN_MS = 1000U;
inline constexpr uint32_t RUN_LEASE_MAX_MS = 300000U;
inline constexpr uint32_t T_COOLDOWN_MIN_MS = 10000U;
inline constexpr uint32_t COMMAND_TABLE_TTL_MS = 2000U;
inline constexpr size_t COMMAND_TABLE_MAX_ENTRIES = 16U;
}  // namespace NodeFsmLimits

// SCREAMING_SNAKE_CASE values are mandated by the global naming convention.

// ----------------------------------------------------------------------------
// STATE_MACHINE_MATRIX §3.1: six macro states, one per legacy node.
// ----------------------------------------------------------------------------
enum class MacroState : uint8_t {
    BOOT_OFF,            // Gateway/node boot, resync, unknown state (safe)
    SCHEDULE_SPRAY,      // Scheduled irrigation permitted and active
    SCHEDULE_COOLDOWN,   // Scheduled pause between sprays
    OVERRIDE_RUN,        // Manual/authorized temporary ON (lease bounded)
    OVERRIDE_HOLD_OFF,   // Manual temporary OFF suppressing schedule
    FAULT_LATCH          // Safety fault or unsafe evidence (hard safe-off)
};

// ----------------------------------------------------------------------------
// STATE_MACHINE_MATRIX §3.2: orthogonal per-command evidence pipeline.
// ----------------------------------------------------------------------------
enum class EvidenceStage : uint8_t {
    NONE,                 // No pending command evidence
    COMMAND_DISPATCHED,   // AGU frame dispatched, awaiting ACK
    RF_ACKNOWLEDGED,      // 0x5A received within t_ack
    GATE_FEEDBACK_ON,     // Driver feedback = ON from telemetry
    CURRENT_DETECTED,     // Current in valid load range
    FLOW_CONFIRMED        // Flow >= min_flow within T_flow_settle
};

// ----------------------------------------------------------------------------
// interface-wire-contract §9 terminal/lifecycle events.
// ----------------------------------------------------------------------------
enum class LifecycleEvent : uint8_t {
    NONE,
    ACCEPTED,
    REJECTED,
    RF_ACKED,
    PUMP_FEEDBACK_ON,
    CURRENT_DETECTED,
    FLOW_CONFIRMED,
    COMPLETED,
    RF_TIMEOUT_OR_NACK,
    SAFE_OFF_UNCONFIRMED,
    FAULT_LATCHED,
    LEASE_EXPIRED_SAFE_OFF,
    RESET_REJECTED
};

// Per-node runtime FSM state (pure RAM; one instance per production node 1..15).
struct NodeFsmState {
    uint8_t node_id = 0;               // 1..15 (production)
    MacroState macro_state = MacroState::BOOT_OFF;
    EvidenceStage evidence_stage = EvidenceStage::NONE;

    // Deadman lease tracking (ESP32 RAM, monotonic millis()).
    uint32_t lease_expiry_ms = 0;      // Absolute expiry timestamp
    uint32_t lease_start_ms = 0;       // When the current lease started
    uint32_t run_lease_ms = 0;         // Configured bounded lease duration
    bool lease_active = false;         // Whether a lease is running

    // Water-shock cooldown: earliest timestamp the next ON is permitted.
    uint32_t cooldown_boundary_ms = 0;

    // Fault tracking.
    uint8_t fault_flags = 0;           // Hardware fault flags from telemetry
    LifecycleEvent last_lifecycle_event = LifecycleEvent::NONE;

    uint32_t last_transition_ms = 0;   // Last macro-state change
    uint32_t last_evidence_ms = 0;     // Last evidence-stage change
};

// Correlation entry: northbound mqtt_command_id <-> gateway rf_command_id.
// Contains no raw RF/AGU bytes (transport data must never be persisted).
struct PendingCommandEntry {
    uint32_t rf_command_id = 0;        // Local gateway monotonic counter
    char mqtt_command_id[65] = {};     // From MQTT payload (max 64 chars)
    uint8_t node_id = 0;               // Target legacy node (1..15)
    uint32_t inserted_ms = 0;          // Insert timestamp for TTL cleanup
    bool resolved = false;             // Terminal lifecycle event received
};

// Bounded correlation table. Static array only — no new/malloc ever.
class PendingCommandTable {
public:
    static constexpr size_t kMaxEntries = NodeFsmLimits::COMMAND_TABLE_MAX_ENTRIES;

    PendingCommandTable();

    /** Insert a new command mapping (inserted_ms = 0 for host-test compatibility). */
    uint32_t insert(uint8_t node_id, const char* mqtt_command_id);

    /** Insert with explicit monotonic timestamp for production telemetry. */
    uint32_t insert(uint8_t node_id, const char* mqtt_command_id, uint32_t now_ms);

    /** Find mqtt_command_id by rf_command_id (nullptr when resolved/unknown). */
    const char* find(uint32_t rf_command_id) const;

    /** Mark entry as resolved once a terminal lifecycle event is published. */
    void resolve(uint32_t rf_command_id);

    /** Remove expired/resolved entries older than COMMAND_TABLE_TTL_MS. */
    void cleanup(uint32_t now_ms);

    /** Number of live (unresolved) command mappings. */
    size_t size() const;

private:
    PendingCommandEntry entries_[kMaxEntries];
    uint32_t next_rf_command_id_ = 1;
    size_t count_ = 0;
};

// ----------------------------------------------------------------------------
// Core FSM functions (Track A)
// ----------------------------------------------------------------------------

/** Transition macro state with guard checks. Fails closed on violated guards. */
bool transitionMacroState(NodeFsmState& fsm, MacroState target, uint32_t now_ms);

/** Advance evidence one step forward only (no skipping allowed). */
bool advanceEvidenceStage(NodeFsmState& fsm, EvidenceStage target, uint32_t now_ms);

/** Reset evidence pipeline to NONE (after an OFF transaction or fault). */
void resetEvidenceStage(NodeFsmState& fsm, uint32_t now_ms);

/** Deadman lease tick. Returns true when the lease expires on this tick. */
bool leaseTick(NodeFsmState& fsm, uint32_t now_ms);

/** Cooldown boundary check — true only when an ON is permitted. */
bool canScheduleOn(const NodeFsmState& fsm, uint32_t now_ms);

/** Pre-flight guard for an explicit FAULT_RESET (never telemetry-driven). */
bool preflightFaultReset(const NodeFsmState& fsm);

/** Initialize RAM FSM state for a node (node_id must be a production id). */
void initNodeFsm(NodeFsmState& fsm, uint8_t node_id);

// Compile-time invariants (Assert bounds and RAM budget at build time).
static_assert(NodeFsmLimits::RUN_LEASE_MIN_MS >= 1000U,
              "Minimum run lease must be >= 1000 ms (S2-TIMER-03)");
static_assert(NodeFsmLimits::RUN_LEASE_MAX_MS <= 300000U,
              "Maximum run lease must be <= 300000 ms (S2-TIMER-03)");
// Actual sizeof(PendingCommandEntry) = 80 bytes (4 rf_command_id + 65 mqtt_command_id + 1 node_id + 4 inserted_ms + 1 resolved + 5 padding = 80).
// 16 entries × 80 = 1280 bytes, well under 20 KB.
static_assert(sizeof(PendingCommandEntry) * PendingCommandTable::kMaxEntries <= 1600U,
              "Pending command table must stay under 1.6 KB of RAM (plan budget)");
static_assert(sizeof(PendingCommandEntry) * PendingCommandTable::kMaxEntries <= 20U * 1024U,
              "Pending command table must stay well under the 20 KB FSM budget");
static_assert(static_cast<uint8_t>(EvidenceStage::FLOW_CONFIRMED) == 5U,
              "Evidence pipeline ordering contract changed");
