#include "group_scheduler.h"
#include <cstring>

GroupScheduler::GroupScheduler()
    : rtc_(nullptr), node_registry_(nullptr), wdt_(nullptr), safety_sink_(nullptr),
      controller_(nullptr), initialized_(false), gateway_degraded_(false),
      active_assignment_version_(0), audit_cb_(nullptr), audit_cb_user_data_(nullptr) {
    for (uint8_t i = 0; i < MAX_TIMER_GROUPS; ++i) {
        groups_[i].group_id = i + 1;
        groups_[i].assignment_state = GroupAssignmentState::UNASSIGNED;
        groups_[i].current_phase = GroupPhase::PHASE_SPRAYING;
        groups_[i].phase_remaining_s = 0;
        groups_[i].profile = GroupProfile{};
        groups_[i].season_id = 0;
        groups_[i].treatment_version_id = 0;
        groups_[i].treatment_version = 0;
        groups_[i].is_night_mode = false;
        groups_[i].pause_remaining_s = 0;
    }
}

GroupScheduler::~GroupScheduler() {}

bool GroupScheduler::begin(IClock* rtc, NodeRegistry* node_registry, IWatchdog* wdt,
                           ICommandOutcomeSink* safety_sink,
                           PumpNodeController* controller) {
    if (rtc == nullptr || node_registry == nullptr) {
        return false;
    }
    rtc_ = rtc;
    node_registry_ = node_registry;
    wdt_ = wdt;
    safety_sink_ = safety_sink;
    controller_ = controller;
    initialized_ = true;
    gateway_degraded_ = false;
    return true;
}

void GroupScheduler::latchGatewayDegraded(const char* reason) {
    if (gateway_degraded_) return;
    gateway_degraded_ = true;
    if (safety_sink_ != nullptr) {
        safety_sink_->publishSafetyAudit("GATEWAY_DEGRADED_SAFE_OFF", reason);
    }
}

void GroupScheduler::emitAuditEvent(const AssignmentAuditEvent& event) {
    if (audit_cb_ != nullptr) {
        audit_cb_(event, audit_cb_user_data_);
    }
    if (safety_sink_ != nullptr) {
        safety_sink_->publishSafetyAudit("GROUP_ASSIGNMENT_MUTATED", event.reason ? event.reason : "ASSIGNMENT_CHANGED");
    }
}

bool GroupScheduler::setGroupProfile(uint8_t group_id, const GroupProfile &profile) {
    if (!isValidGroupId(group_id)) return false;
    if (!profile.isValid()) return false;

    groups_[group_id - 1].profile = profile;
    return true;
}

bool GroupScheduler::setGroupActive(uint8_t group_id, bool active) {
    if (!isValidGroupId(group_id)) return false;

    GroupRuntimeState &group = groups_[group_id - 1];
    if (active && (group.season_id == 0 || group.treatment_version_id == 0 || group.treatment_version == 0)) {
        return false;
    }
    group.assignment_state = active ? GroupAssignmentState::ACTIVE : GroupAssignmentState::UNASSIGNED;
    if (!active) {
        group.current_phase = GroupPhase::PHASE_SPRAYING;
        group.phase_remaining_s = group.profile.spray_day_s;
        if (node_registry_ && !node_registry_->updateDesiredStateForGroup(group_id, NodePumpState::OFF)) {
            latchGatewayDegraded("GROUP_DEACTIVATION_LOCK_TIMEOUT");
            return false;
        }
    }
    return true;
}

bool GroupScheduler::applyPublishedTreatment(uint8_t group_id,
                                             const PublishedTreatmentAssignment& assignment) {
    if (!isValidGroupId(group_id) || !assignment.isValid() ||
        !setGroupProfile(group_id, assignment.profile)) {
        return false;
    }
    GroupRuntimeState& group = groups_[group_id - 1];
    group.season_id = assignment.season_id;
    group.treatment_version_id = assignment.treatment_version_id;
    group.treatment_version = assignment.version;
    group.current_phase = GroupPhase::PHASE_SPRAYING;
    group.phase_remaining_s = assignment.profile.spray_day_s;
    return setGroupActive(group_id, true);
}

bool GroupScheduler::assignNodeVersioned(const VersionedGroupAssignment& assignment) {
    if (!initialized_ || node_registry_ == nullptr) return false;

    // 1. Boundary enforcement: node_id in 1..15, group_id in 0..4
    if (!isValidNodeId(assignment.node_id)) {
        return false;
    }
    if (assignment.group_id > MAX_TIMER_GROUPS) {
        return false;
    }

    // 2. Monotonic versioning: must be strictly greater than active version
    if (assignment.assignment_version <= active_assignment_version_) {
        return false;
    }

    // 3. Single active group invariant: check current group
    uint8_t current_group = node_registry_->getNodeGroup(assignment.node_id);
    if (current_group > 0 && current_group != assignment.group_id && assignment.group_id != 0) {
        // Node is already assigned to another active group
        if (groups_[current_group - 1].assignment_state == GroupAssignmentState::ACTIVE) {
            return false; // Reject duplicate active assignment fail-closed
        }
    }

    // 4. Time-safe transition: force node OFF before changing group association
    if (!node_registry_->setDesiredState(assignment.node_id, NodePumpState::OFF)) {
        latchGatewayDegraded("REASSIGNMENT_SAFE_OFF_FAILED");
        return false;
    }

    // 5. Apply assignment in registry
    if (!node_registry_->assignNodeToGroup(assignment.node_id, assignment.group_id)) {
        return false;
    }

    active_assignment_version_ = assignment.assignment_version;

    // 6. Emit audit event
    AssignmentAuditEvent audit{};
    audit.assignment_version = assignment.assignment_version;
    audit.timestamp_ms = assignment.effective_at;
    std::strncpy(audit.actor, assignment.actor, sizeof(audit.actor) - 1);
    audit.actor[sizeof(audit.actor) - 1] = '\0';
    audit.node_id = assignment.node_id;
    audit.old_group_id = current_group;
    audit.new_group_id = assignment.group_id;
    audit.reason = (assignment.group_id == 0) ? "NODE_UNASSIGNED" : "NODE_ASSIGNED_TO_GROUP";
    emitAuditEvent(audit);

    return true;
}

bool GroupScheduler::unassignNodeVersioned(uint8_t node_id, uint32_t assignment_version,
                                          const char* actor, uint32_t timestamp_ms) {
    VersionedGroupAssignment assign{};
    assign.assignment_version = assignment_version;
    assign.node_id = node_id;
    assign.group_id = 0; // UNASSIGNED
    assign.effective_at = timestamp_ms;
    if (actor != nullptr) {
        std::strncpy(assign.actor, actor, sizeof(assign.actor) - 1);
        assign.actor[sizeof(assign.actor) - 1] = '\0';
    }
    return assignNodeVersioned(assign);
}

bool GroupScheduler::getGroupRuntimeState(uint8_t group_id, GroupRuntimeState &out_state) const {
    if (!isValidGroupId(group_id)) return false;
    out_state = groups_[group_id - 1];
    return true;
}

bool GroupScheduler::forceSafeOff() {
    if (!initialized_ || node_registry_ == nullptr) return false;
    for (uint8_t i = 0; i < MAX_TIMER_GROUPS; ++i) {
        GroupRuntimeState& group = groups_[i];
        group.assignment_state = GroupAssignmentState::UNASSIGNED;
        group.current_phase = GroupPhase::PHASE_SPRAYING;
        group.phase_remaining_s = 0;
        group.is_night_mode = false;
        group.pause_remaining_s = 0;
        if (!node_registry_->updateDesiredStateForGroup(group.group_id, NodePumpState::OFF)) {
            latchGatewayDegraded("FORCE_SAFE_OFF_LOCK_TIMEOUT");
            return false;
        }
    }
    return true;
}

bool GroupScheduler::validateRuntimeClock(bool& night_mode) {
    if (!initialized_ || rtc_ == nullptr || node_registry_ == nullptr) return false;
    if (wdt_ != nullptr) wdt_->resetWatchdog(0);
    SystemTime t = rtc_->getTime();
    if (!t.is_valid) {
        if (!forceSafeOff()) latchGatewayDegraded("RTC_INVALID_SAFE_OFF_FAILED");
        return false;
    }
    night_mode = rtc_->isNightMode();
    return true;
}

bool GroupScheduler::forceUnassignedGroupOff(GroupRuntimeState& group) {
    if (node_registry_->updateDesiredStateForGroup(group.group_id, NodePumpState::OFF)) return true;
    latchGatewayDegraded("UNASSIGNED_GROUP_SAFE_OFF_LOCK_TIMEOUT");
    return false;
}

void GroupScheduler::advanceGroupPhase(GroupRuntimeState& group, bool night_mode) {
    if (group.phase_remaining_s > 1) {
        --group.phase_remaining_s;
        return;
    }
    const bool spraying = group.current_phase == GroupPhase::PHASE_SPRAYING;
    group.current_phase = spraying ? GroupPhase::PHASE_COOLING_DOWN : GroupPhase::PHASE_SPRAYING;
    group.phase_remaining_s = spraying
        ? (night_mode ? group.profile.cooldown_night_s : group.profile.cooldown_day_s)
        : (night_mode ? group.profile.spray_night_s : group.profile.spray_day_s);
}

bool GroupScheduler::stepActiveGroup(GroupRuntimeState& group, bool night_mode) {
    group.is_night_mode = night_mode;

    // If group is paused, count down pause timer and keep OFF
    if (group.assignment_state == GroupAssignmentState::PAUSED) {
        if (group.pause_remaining_s > 0) {
            --group.pause_remaining_s;
        }
        if (group.pause_remaining_s == 0) {
            group.assignment_state = GroupAssignmentState::ACTIVE;
            group.current_phase = GroupPhase::PHASE_COOLING_DOWN;
            group.phase_remaining_s = night_mode ? group.profile.cooldown_night_s : group.profile.cooldown_day_s;
        }
        return node_registry_->updateDesiredStateForGroup(group.group_id, NodePumpState::OFF);
    }

    advanceGroupPhase(group, night_mode);
    const NodePumpState target = (group.current_phase == GroupPhase::PHASE_SPRAYING)
        ? NodePumpState::ON : NodePumpState::OFF;
    if (node_registry_->updateDesiredStateForGroup(group.group_id, target)) return true;
    latchGatewayDegraded("SCHEDULE_FANOUT_LOCK_TIMEOUT");
    return false;
}

bool GroupScheduler::stepGroupSchedule() {
    bool night_mode = false;
    if (!validateRuntimeClock(night_mode)) return false;
    for (uint8_t i = 0; i < MAX_TIMER_GROUPS; ++i) {
        GroupRuntimeState &group = groups_[i];
        group.is_night_mode = night_mode;
        if (group.assignment_state == GroupAssignmentState::UNASSIGNED) {
            if (!forceUnassignedGroupOff(group)) return false;
        } else if (!stepActiveGroup(group, night_mode)) {
            return false;
        }
    }
    return true;
}

bool GroupScheduler::isIctDayMode(const SystemTime& time) {
    if (!time.is_valid) return true; // Safe fallback DAY mode
    return isIctDayMode(time.hour, time.minute);
}

bool GroupScheduler::isIctDayMode(uint8_t hour, uint8_t minute) {
    (void) minute;
    return (hour >= DAY_START_HOUR && hour < NIGHT_START_HOUR);
}

bool GroupScheduler::applyManualNodeOverride(uint8_t node_id, NodePumpState desired,
                                            uint32_t lease_or_duration_ms,
                                            const char* command_id, const char* source) {
    if (!initialized_ || node_registry_ == nullptr) return false;
    if (!isValidNodeId(node_id)) return false;
    if (lease_or_duration_ms == 0) return false;

    NodeState state{};
    if (!node_registry_->getNodeState(node_id, state)) return false;

    // Fail-safe check: Node in FAULT cannot be commanded ON!
    if (desired == NodePumpState::ON) {
        if (state.health == NodeHealthStatus::FAULT || state.fault_latched) {
            return false; // Strict safety FSM lockout
        }
        if (state.health == NodeHealthStatus::STALE || state.health == NodeHealthStatus::OFFLINE) {
            return false; // Cannot command ON offline/stale node
        }
    }

    ExternalOverridePolicy policy{source ? source : "MANUAL_OVERRIDE",
                                  desired == NodePumpState::ON ? lease_or_duration_ms : 0,
                                  desired == NodePumpState::OFF ? lease_or_duration_ms : 0};

    if (controller_ != nullptr) {
        return controller_->queueExternalNodeCommand(node_id, desired, command_id, &policy);
    }

    return node_registry_->setDesiredState(node_id, desired);
}

bool GroupScheduler::pauseGroup(uint8_t group_id, uint32_t pause_duration_s, const char* command_id) {
    if (!isValidGroupId(group_id) || pause_duration_s == 0) return false;

    GroupRuntimeState& group = groups_[group_id - 1];
    if (group.assignment_state != GroupAssignmentState::ACTIVE) {
        return false;
    }

    group.assignment_state = GroupAssignmentState::PAUSED;
    group.pause_remaining_s = pause_duration_s;

    ExternalOverridePolicy policy{"MANUAL_OVERRIDE", 0, pause_duration_s * 1000U};
    if (controller_ != nullptr) {
        return controller_->queueExternalGroupCommand(group_id, NodePumpState::OFF, command_id, &policy);
    }
    return node_registry_->updateDesiredStateForGroup(group_id, NodePumpState::OFF);
}

bool GroupScheduler::resumeGroup(uint8_t group_id, const char* command_id) {
    (void) command_id;
    if (!isValidGroupId(group_id)) return false;

    GroupRuntimeState& group = groups_[group_id - 1];
    if (group.assignment_state != GroupAssignmentState::PAUSED) {
        return false;
    }

    group.assignment_state = GroupAssignmentState::ACTIVE;
    group.pause_remaining_s = 0;
    group.current_phase = GroupPhase::PHASE_COOLING_DOWN;
    group.phase_remaining_s = group.is_night_mode ? group.profile.cooldown_night_s : group.profile.cooldown_day_s;
    return true;
}

bool GroupScheduler::getGroupState(uint8_t group_id, GroupRuntimeState& out_state) const {
    if (!isValidGroupId(group_id)) return false;
    out_state = groups_[group_id - 1];
    return true;
}
