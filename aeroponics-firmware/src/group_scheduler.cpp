#include "group_scheduler.h"
#include <cstring>
#include <cstdio>

GroupScheduler::GroupScheduler()
    : rtc_(nullptr), node_registry_(nullptr), wdt_(nullptr), safety_sink_(nullptr),
      controller_(nullptr), nvs_(nullptr), initialized_(false), gateway_degraded_(false),
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
                           PumpNodeController* controller,
                           NvsStorage* nvs) {
    if (rtc == nullptr || node_registry == nullptr) {
        return false;
    }
    rtc_ = rtc;
    node_registry_ = node_registry;
    wdt_ = wdt;
    safety_sink_ = safety_sink;
    controller_ = controller;
    nvs_ = nvs;
    initialized_ = true;
    gateway_degraded_ = false;

    if (nvs_ != nullptr && nvs_->isInitialized()) {
        loadFromStorage();
    }
    return true;
}

bool GroupScheduler::loadFromStorage() {
    if (nvs_ == nullptr || !nvs_->isInitialized()) return false;

    // 1. Load Node Assignments
    PersistentNodeAssignmentTable assign_table{};
    size_t assign_len = sizeof(assign_table);
    if (nvs_->getBlob(NVS_KEY_NODE_ASSIGN, &assign_table, &assign_len)) {
        if (assign_len == sizeof(assign_table) && assign_table.isValid()) {
            active_assignment_version_ = assign_table.assignment_version;
            if (node_registry_ != nullptr) {
                for (uint8_t i = 0; i < MAX_NODES; ++i) {
                    uint8_t node_id = i + 1;
                    if (isValidNodeId(node_id)) {
                        node_registry_->assignNodeToGroup(node_id, assign_table.node_groups[i]);
                    }
                }
            }
        } else {
            // Bad checksum or length: fail closed
            if (safety_sink_ != nullptr) {
                safety_sink_->publishSafetyAudit("GATEWAY_NVS_CORRUPTED", "NODE_ASSIGNMENT_CRC_INVALID");
            }
        }
    }

    // 2. Load Group Schedules
    for (uint8_t gid = 1; gid <= MAX_TIMER_GROUPS; ++gid) {
        char key[16] = {};
        std::snprintf(key, sizeof(key), "%s%u", NVS_KEY_GRP_PREFIX, gid);
        PersistentGroupScheduleRecord rec{};
        size_t rec_len = sizeof(rec);
        if (nvs_->getBlob(key, &rec, &rec_len)) {
            if (rec_len == sizeof(rec) && rec.isValid()) {
                GroupProfile prof{rec.spray_day_s, rec.cooldown_day_s, rec.spray_night_s, rec.cooldown_night_s};
                if (setGroupProfile(gid, prof)) {
                    GroupRuntimeState& group = groups_[gid - 1];
                    group.season_id = rec.season_id;
                    group.treatment_version_id = rec.treatment_version_id;
                    group.treatment_version = rec.treatment_version;
                    if (rec.is_active) {
                        group.assignment_state = GroupAssignmentState::ACTIVE;
                        // On cold boot, start in safe cooldown phase so pumps do not slam on simultaneously
                        group.current_phase = GroupPhase::PHASE_COOLING_DOWN;
                        group.phase_remaining_s = prof.cooldown_day_s;
                    } else {
                        group.assignment_state = GroupAssignmentState::UNASSIGNED;
                    }
                }
            } else {
                if (safety_sink_ != nullptr) {
                    safety_sink_->publishSafetyAudit("GATEWAY_NVS_CORRUPTED", "GROUP_RECORD_CRC_INVALID");
                }
            }
        }
    }
    return true;
}

bool GroupScheduler::persistGroupSchedule(uint8_t group_id) {
    if (nvs_ == nullptr || !nvs_->isInitialized() || !isValidGroupId(group_id)) return false;

    const GroupRuntimeState& group = groups_[group_id - 1];
    PersistentGroupScheduleRecord rec{};
    rec.magic = PERSISTENT_RECORD_MAGIC;
    rec.group_id = group_id;
    rec.is_active = (group.assignment_state == GroupAssignmentState::ACTIVE) ? 1 : 0;
    rec.season_id = group.season_id;
    rec.treatment_version_id = group.treatment_version_id;
    rec.treatment_version = group.treatment_version;
    rec.spray_day_s = group.profile.spray_day_s;
    rec.cooldown_day_s = group.profile.cooldown_day_s;
    rec.spray_night_s = group.profile.spray_night_s;
    rec.cooldown_night_s = group.profile.cooldown_night_s;
    rec.checksum = rec.computeChecksum();

    char key[16] = {};
    std::snprintf(key, sizeof(key), "%s%u", NVS_KEY_GRP_PREFIX, group_id);

    // Atomic write-then-verify
    if (!nvs_->setBlob(key, &rec, sizeof(rec))) {
        return false;
    }

    PersistentGroupScheduleRecord verify_rec{};
    size_t verify_len = sizeof(verify_rec);
    if (!nvs_->getBlob(key, &verify_rec, &verify_len) ||
        verify_len != sizeof(verify_rec) ||
        std::memcmp(&rec, &verify_rec, sizeof(rec)) != 0) {
        return false;
    }
    return true;
}

bool GroupScheduler::persistNodeAssignments() {
    if (nvs_ == nullptr || !nvs_->isInitialized() || node_registry_ == nullptr) return false;

    PersistentNodeAssignmentTable table{};
    table.magic = PERSISTENT_RECORD_MAGIC;
    table.assignment_version = active_assignment_version_;
    for (uint8_t i = 0; i < MAX_NODES; ++i) {
        uint8_t node_id = i + 1;
        table.node_groups[i] = isValidNodeId(node_id) ? node_registry_->getNodeGroup(node_id) : 0;
    }
    table.checksum = table.computeChecksum();

    // Atomic write-then-verify
    if (!nvs_->setBlob(NVS_KEY_NODE_ASSIGN, &table, sizeof(table))) {
        return false;
    }

    PersistentNodeAssignmentTable verify_table{};
    size_t verify_len = sizeof(verify_table);
    if (!nvs_->getBlob(NVS_KEY_NODE_ASSIGN, &verify_table, &verify_len) ||
        verify_len != sizeof(verify_table) ||
        std::memcmp(&table, &verify_table, sizeof(table)) != 0) {
        return false;
    }
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
    if (nvs_ != nullptr && nvs_->isInitialized()) {
        persistGroupSchedule(group_id);
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

    // Immediately synchronize phase to absolute RTC time if clock is valid
    if (rtc_ != nullptr) {
        SystemTime t = rtc_->getTime();
        if (t.is_valid) {
            calculateAbsolutePhase(t, group.profile, 0, group.current_phase, group.phase_remaining_s, group.is_night_mode);
        } else {
            group.current_phase = GroupPhase::PHASE_SPRAYING;
            group.phase_remaining_s = assignment.profile.spray_day_s;
        }
    } else {
        group.current_phase = GroupPhase::PHASE_SPRAYING;
        group.phase_remaining_s = assignment.profile.spray_day_s;
    }

    const bool active_ok = setGroupActive(group_id, true);
    if (active_ok && nvs_ != nullptr && nvs_->isInitialized()) {
        persistGroupSchedule(group_id);
    }
    return active_ok;
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
    if (nvs_ != nullptr && nvs_->isInitialized()) {
        persistNodeAssignments();
    }

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

void GroupScheduler::calculateAbsolutePhase(const SystemTime& time,
                                           const GroupProfile& profile,
                                           uint32_t stagger_offset_s,
                                           GroupPhase& out_phase,
                                           uint32_t& out_remaining_s,
                                           bool& out_night_mode) {
    if (!time.is_valid) {
        out_phase = GroupPhase::PHASE_COOLING_DOWN;
        out_remaining_s = profile.cooldown_day_s;
        out_night_mode = false;
        return;
    }

    const bool is_day = isIctDayMode(time);
    out_night_mode = !is_day;

    uint32_t spray_s = 0;
    uint32_t cooldown_s = 0;
    uint32_t elapsed = 0;
    const uint32_t sec_of_day = time.toSecondsOfDay();

    if (is_day) {
        spray_s = profile.spray_day_s;
        cooldown_s = profile.cooldown_day_s;
        const uint32_t day_start_sec = static_cast<uint32_t>(DAY_START_HOUR) * 3600U;
        elapsed = (sec_of_day >= day_start_sec) ? (sec_of_day - day_start_sec) : 0U;
    } else {
        spray_s = profile.spray_night_s;
        cooldown_s = profile.cooldown_night_s;
        const uint32_t night_start_sec = static_cast<uint32_t>(NIGHT_START_HOUR) * 3600U;
        if (sec_of_day >= night_start_sec) {
            elapsed = sec_of_day - night_start_sec;
        } else {
            // Past midnight: 00:00:00 to 05:59:59 (seconds from yesterday 18:00)
            elapsed = (86400U - night_start_sec) + sec_of_day;
        }
    }

    const uint32_t cycle_s = spray_s + cooldown_s;
    if (cycle_s == 0) {
        out_phase = GroupPhase::PHASE_COOLING_DOWN;
        out_remaining_s = 0;
        return;
    }

    const uint32_t effective_offset = (elapsed + stagger_offset_s) % cycle_s;
    if (effective_offset < spray_s) {
        out_phase = GroupPhase::PHASE_SPRAYING;
        out_remaining_s = spray_s - effective_offset;
    } else {
        out_phase = GroupPhase::PHASE_COOLING_DOWN;
        out_remaining_s = cycle_s - effective_offset;
    }
}

bool GroupScheduler::validateRuntimeClock(bool& night_mode) {
    SystemTime dummy{};
    return validateRuntimeClock(dummy, night_mode);
}

bool GroupScheduler::validateRuntimeClock(SystemTime& out_time, bool& night_mode) {
    if (!initialized_ || rtc_ == nullptr || node_registry_ == nullptr) return false;
    if (wdt_ != nullptr) wdt_->resetWatchdog(0);
    out_time = rtc_->getTime();
    if (!out_time.is_valid) {
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
    SystemTime time{0, 0, 0, false};
    if (rtc_ != nullptr) {
        time = rtc_->getTime();
    }
    return stepActiveGroup(group, time, night_mode);
}

bool GroupScheduler::stepActiveGroup(GroupRuntimeState& group, const SystemTime& time, bool night_mode) {
    group.is_night_mode = night_mode;

    // If group is paused, count down pause timer and keep OFF
    if (group.assignment_state == GroupAssignmentState::PAUSED) {
        if (group.pause_remaining_s > 0) {
            --group.pause_remaining_s;
        }
        if (group.pause_remaining_s == 0) {
            group.assignment_state = GroupAssignmentState::ACTIVE;
            calculateAbsolutePhase(time, group.profile, 0, group.current_phase, group.phase_remaining_s, group.is_night_mode);
        }
        return node_registry_->updateDesiredStateForGroup(group.group_id, NodePumpState::OFF);
    }

    calculateAbsolutePhase(time, group.profile, 0, group.current_phase, group.phase_remaining_s, group.is_night_mode);
    const NodePumpState target = (group.current_phase == GroupPhase::PHASE_SPRAYING)
        ? NodePumpState::ON : NodePumpState::OFF;
    if (node_registry_->updateDesiredStateForGroup(group.group_id, target)) return true;
    latchGatewayDegraded("SCHEDULE_FANOUT_LOCK_TIMEOUT");
    return false;
}

bool GroupScheduler::stepGroupSchedule() {
    SystemTime time{0, 0, 0, false};
    bool night_mode = false;
    if (!validateRuntimeClock(time, night_mode)) return false;
    for (uint8_t i = 0; i < MAX_TIMER_GROUPS; ++i) {
        GroupRuntimeState &group = groups_[i];
        group.is_night_mode = night_mode;
        if (group.assignment_state == GroupAssignmentState::UNASSIGNED) {
            if (!forceUnassignedGroupOff(group)) return false;
        } else if (!stepActiveGroup(group, time, night_mode)) {
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
    if (rtc_ != nullptr) {
        SystemTime t = rtc_->getTime();
        if (t.is_valid) {
            calculateAbsolutePhase(t, group.profile, 0, group.current_phase, group.phase_remaining_s, group.is_night_mode);
            return true;
        }
    }
    group.current_phase = GroupPhase::PHASE_COOLING_DOWN;
    group.phase_remaining_s = group.is_night_mode ? group.profile.cooldown_night_s : group.profile.cooldown_day_s;
    return true;
}

bool GroupScheduler::getGroupState(uint8_t group_id, GroupRuntimeState& out_state) const {
    if (!isValidGroupId(group_id)) return false;
    out_state = groups_[group_id - 1];
    return true;
}
