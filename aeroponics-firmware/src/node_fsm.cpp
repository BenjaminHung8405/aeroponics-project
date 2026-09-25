#include "node_fsm.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <esp_log.h>
static const char* TAG = "NODE_FSM";
#endif

namespace {
// Safe addition on unsigned monotonic timers.
static inline uint32_t addClamped(uint32_t now, uint32_t delta) {
    return (now > UINT32_MAX - delta) ? UINT32_MAX : now + delta;
}

// Advance evidence one step only; no skip.
static bool isNextEvidenceStage(EvidenceStage current, EvidenceStage target) {
    return static_cast<uint8_t>(target) == static_cast<uint8_t>(current) + 1U;
}
}  // namespace

bool transitionMacroState(NodeFsmState& fsm, MacroState target, uint32_t now_ms) {
    if (fsm.macro_state == MacroState::FAULT_LATCH &&
        target != MacroState::BOOT_OFF) {
        return false; // FAULT_LATCH must reset via BOOT_OFF first (S2-FSM-01)
    }

    if (fsm.macro_state == MacroState::OVERRIDE_RUN &&
        target == MacroState::SCHEDULE_SPRAY) {
        return false; // OVERRIDE_RUN must flow through SCHEDULE_COOLDOWN
    }

    if (target == MacroState::SCHEDULE_SPRAY && !canScheduleOn(fsm, now_ms)) {
        return false; // Enforce T_cooldown_min before next ON.
    }

    if (target == MacroState::SCHEDULE_COOLDOWN &&
        fsm.macro_state != MacroState::SCHEDULE_COOLDOWN) {
        // Update cooldown boundary so an ON is blocked for T_cooldown_min.
        const uint32_t boundary = addClamped(
            now_ms, NodeFsmLimits::T_COOLDOWN_MIN_MS);
        if (boundary > fsm.cooldown_boundary_ms) {
            fsm.cooldown_boundary_ms = boundary;
        }
    }

    fsm.macro_state = target;
    fsm.last_transition_ms = now_ms;
    resetEvidenceStage(fsm, now_ms);
    return true;
}

bool advanceEvidenceStage(NodeFsmState& fsm, EvidenceStage target, uint32_t now_ms) {
    const auto current_stage = static_cast<uint8_t>(fsm.evidence_stage);
    const auto target_stage = static_cast<uint8_t>(target);
    if (fsm.evidence_stage == EvidenceStage::FLOW_CONFIRMED ||
        target_stage <= current_stage) {
        return false; // No skip, no regress, no advancement beyond terminal.
    }

    if (!isNextEvidenceStage(fsm.evidence_stage, target)) {
        return false; // Evidence must progress stepwise only.
    }

    fsm.evidence_stage = target;
    fsm.last_evidence_ms = now_ms;
    return true;
}

void resetEvidenceStage(NodeFsmState& fsm, uint32_t now_ms) {
    fsm.evidence_stage = EvidenceStage::NONE;
    fsm.last_evidence_ms = now_ms;
}

bool leaseTick(NodeFsmState& fsm, uint32_t now_ms) {
    if (!fsm.lease_active) {
        return false;
    }

    if (fsm.lease_expiry_ms == 0 || now_ms < fsm.lease_expiry_ms) {
        return false; // Lease still valid.
    }

    // Lease expired: cancel lease and enforce cooling-off before next ON.
    fsm.lease_active = false;
    fsm.lease_start_ms = 0;
    fsm.last_lifecycle_event = LifecycleEvent::LEASE_EXPIRED_SAFE_OFF;
    fsm.cooldown_boundary_ms = addClamped(
        now_ms, NodeFsmLimits::T_COOLDOWN_MIN_MS);
    return true;
}

bool canScheduleOn(const NodeFsmState& fsm, uint32_t now_ms) {
    return now_ms >= fsm.cooldown_boundary_ms;
}

bool preflightFaultReset(const NodeFsmState& fsm) {
    if (fsm.macro_state != MacroState::FAULT_LATCH) {
        return false;
    }
    if (fsm.fault_flags != 0 || fsm.lease_active) {
        return false;
    }
    return true;
}

void initNodeFsm(NodeFsmState& fsm, uint8_t node_id) {
    fsm.node_id = isProductionNodeId(node_id) ? node_id : 0;
    fsm.macro_state = MacroState::BOOT_OFF;
    fsm.evidence_stage = EvidenceStage::NONE;
    fsm.lease_expiry_ms = 0;
    fsm.lease_start_ms = 0;
    fsm.run_lease_ms = 0;
    fsm.lease_active = false;
    fsm.cooldown_boundary_ms = 0;
    fsm.fault_flags = 0;
    fsm.last_lifecycle_event = LifecycleEvent::NONE;
    fsm.last_transition_ms = 0;
    fsm.last_evidence_ms = 0;
}

// ============================================================================
// PendingCommandTable (static array; zero dynamic allocation)
// ============================================================================

PendingCommandTable::PendingCommandTable() {
    std::memset(entries_, 0, sizeof(entries_));
    next_rf_command_id_ = 1;
    count_ = 0;
}

uint32_t PendingCommandTable::insert(uint8_t node_id, const char* mqtt_command_id) {
    return insert(node_id, mqtt_command_id, 0);
}

uint32_t PendingCommandTable::insert(uint8_t node_id,
                                     const char* mqtt_command_id,
                                     uint32_t now_ms) {
    if (!isProductionNodeId(node_id) || mqtt_command_id == nullptr) {
        return 0;
    }

    const size_t len = strnlen(mqtt_command_id, sizeof(entries_[0].mqtt_command_id));
    if (len == 0 || len >= sizeof(entries_[0].mqtt_command_id)) {
        return 0;
    }

    // When table is at live capacity, attempt cleanup to reclaim resolved slots.
    if (count_ >= kMaxEntries) {
        cleanup(now_ms);
    }

    // Find the first free slot: either never used (rf_command_id == 0) or
    // resolved (ready for reuse).  Skip active entries whose mqtt_command_id
    // is populated and still unresolved.
    size_t free_index = 0;
    bool found = false;
    for (size_t i = 0; i < kMaxEntries; ++i) {
        if (entries_[i].rf_command_id == 0 || entries_[i].resolved) {
            free_index = i;
            found = true;
            break;
        }
    }
    if (!found) {
        return 0; // Table is genuinely at live capacity.
    }

    PendingCommandEntry& entry = entries_[free_index];

    // count_ tracks allocated-but-not-yet-reclaimed slots (rf_command_id != 0,
    // not yet memset by cleanup). A virgin slot (rf_command_id == 0) is not
    // counted yet, so increment. A resolved slot is already counted (resolve()
    // does not decrement), so do not increment again — cleanup() will
    // decrement exactly once when it finally reclaims the slot.
    if (entry.rf_command_id == 0) {
        ++count_;
    }

    entry.node_id = node_id;
    std::memcpy(entry.mqtt_command_id, mqtt_command_id, len + 1);
    entry.mqtt_command_id[sizeof(entry.mqtt_command_id) - 1] = '\0';
    entry.inserted_ms = now_ms;
    entry.resolved = false;

    if (next_rf_command_id_ == 0) {
        next_rf_command_id_ = 1;
    }
    entry.rf_command_id = next_rf_command_id_;
    ++next_rf_command_id_;
    if (next_rf_command_id_ == 0) {
        next_rf_command_id_ = 1;
    }

    return entry.rf_command_id;
}

const char* PendingCommandTable::find(uint32_t rf_command_id) const {
    for (size_t i = 0; i < kMaxEntries; ++i) {
        const PendingCommandEntry& entry = entries_[i];
        // Fail-closed: return nullptr if resolved or unknown.
        if (entry.rf_command_id == rf_command_id && !entry.resolved &&
            entry.mqtt_command_id[0] != '\0') {
            return entry.mqtt_command_id;
        }
    }
    return nullptr;
}

void PendingCommandTable::resolve(uint32_t rf_command_id) {
    for (size_t i = 0; i < kMaxEntries; ++i) {
        PendingCommandEntry& entry = entries_[i];
        if (entry.rf_command_id == rf_command_id && !entry.resolved) {
            entry.resolved = true;
            break;
        }
    }
}

void PendingCommandTable::cleanup(uint32_t now_ms) {
    for (size_t i = 0; i < kMaxEntries; ++i) {
        PendingCommandEntry& entry = entries_[i];
        // Skip empty slots.
        if (entry.rf_command_id == 0) {
            continue;
        }

        bool expired = false;
        if (entry.resolved) {
            // Resolved entries are candidates for immediate cleanup.
            expired = true;
        } else if (now_ms > entry.inserted_ms) {
            const uint32_t age_ms = now_ms - entry.inserted_ms;
            expired = age_ms >= NodeFsmLimits::COMMAND_TABLE_TTL_MS;
        }

        if (expired) {
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
            if (!entry.resolved) {
                ESP_LOGW(TAG,
                         "Command TTL expired without resolution: rf_id=%u mqtt_id=%s",
                         entry.rf_command_id, entry.mqtt_command_id);
            }
#endif
            std::memset(&entry, 0, sizeof(entry));
            --count_;
        }
    }
}

size_t PendingCommandTable::size() const {
    return count_;
}
