#include "group_schedule_manager.h"
#include "command_manager.h"

GroupScheduleManager::GroupScheduleManager()
    : rtc_(nullptr), node_registry_(nullptr), wdt_(nullptr), safety_sink_(nullptr), initialized_(false) {
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
    }
}

GroupScheduleManager::~GroupScheduleManager() {}

bool GroupScheduleManager::begin(IClock* rtc, NodeRegistry* node_registry, IWatchdog* wdt,
                                 ICommandOutcomeSink* safety_sink) {
    if (rtc == nullptr || node_registry == nullptr) {
        return false;
    }
    rtc_ = rtc;
    node_registry_ = node_registry;
    wdt_ = wdt;
    safety_sink_ = safety_sink;
    initialized_ = true;
    return true;
}

void GroupScheduleManager::latchGatewayDegraded(const char* reason) {
    if (gateway_degraded_) return;
    gateway_degraded_ = true;
    if (safety_sink_ != nullptr) safety_sink_->publishSafetyAudit("GATEWAY_DEGRADED_SAFE_OFF", reason);
}

bool GroupScheduleManager::setGroupProfile(uint8_t group_id, const GroupProfile &profile) {
    if (!isValidGroupId(group_id)) return false;

    // Validate bounds
    if (profile.spray_day_s < GROUP_MIN_SPRAY_DURATION_S || profile.spray_day_s > GROUP_MAX_SPRAY_DURATION_S ||
        profile.cooldown_day_s < GROUP_MIN_COOLDOWN_DURATION_S || profile.cooldown_day_s > GROUP_MAX_COOLDOWN_DURATION_S ||
        profile.spray_night_s < GROUP_MIN_SPRAY_DURATION_S || profile.spray_night_s > GROUP_MAX_SPRAY_DURATION_S ||
        profile.cooldown_night_s < GROUP_MIN_COOLDOWN_DURATION_S || profile.cooldown_night_s > GROUP_MAX_COOLDOWN_DURATION_S) {
        return false;
    }

    groups_[group_id - 1].profile = profile;
    return true;
}

bool GroupScheduleManager::setGroupActive(uint8_t group_id, bool active) {
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

bool GroupScheduleManager::applyPublishedTreatment(uint8_t group_id,
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

bool GroupScheduleManager::getGroupRuntimeState(uint8_t group_id, GroupRuntimeState &out_state) const {
    if (!isValidGroupId(group_id)) return false;
    out_state = groups_[group_id - 1];
    return true;
}

bool GroupScheduleManager::forceSafeOff() {
    if (!initialized_ || node_registry_ == nullptr) return false;
    for (uint8_t i = 0; i < MAX_TIMER_GROUPS; ++i) {
        GroupRuntimeState& group = groups_[i];
        group.assignment_state = GroupAssignmentState::UNASSIGNED;
        group.current_phase = GroupPhase::PHASE_SPRAYING;
        group.phase_remaining_s = 0;
        group.is_night_mode = false;
        if (!node_registry_->updateDesiredStateForGroup(group.group_id, NodePumpState::OFF)) {
            latchGatewayDegraded("FORCE_SAFE_OFF_LOCK_TIMEOUT");
            return false;
        }
    }
    return true;
}

bool GroupScheduleManager::stepGroupSchedule() {
    if (!initialized_ || rtc_ == nullptr || node_registry_ == nullptr) {
        return false;
    }

    if (wdt_) {
        wdt_->resetWatchdog(0);
    }

    SystemTime sys_time = rtc_->getTime();
    if (!sys_time.is_valid) {
        if (!forceSafeOff()) latchGatewayDegraded("RTC_INVALID_SAFE_OFF_FAILED");
        return false;
    }
    bool night_mode = rtc_->isNightMode();

    for (uint8_t i = 0; i < MAX_TIMER_GROUPS; ++i) {
        GroupRuntimeState &group = groups_[i];
        group.is_night_mode = night_mode;

        if (group.assignment_state == GroupAssignmentState::UNASSIGNED) {
            if (!node_registry_->updateDesiredStateForGroup(group.group_id, NodePumpState::OFF)) {
                latchGatewayDegraded("UNASSIGNED_GROUP_SAFE_OFF_LOCK_TIMEOUT");
                return false;
            }
            continue;
        }

        if (group.phase_remaining_s > 1) {
            group.phase_remaining_s -= 1;
        } else {
            if (group.current_phase == GroupPhase::PHASE_SPRAYING) {
                group.current_phase = GroupPhase::PHASE_COOLING_DOWN;
                group.phase_remaining_s = night_mode ? group.profile.cooldown_night_s : group.profile.cooldown_day_s;
            } else {
                group.current_phase = GroupPhase::PHASE_SPRAYING;
                group.phase_remaining_s = night_mode ? group.profile.spray_night_s : group.profile.spray_day_s;
            }
        }

        NodePumpState target_state = (group.current_phase == GroupPhase::PHASE_SPRAYING)
                                         ? NodePumpState::ON
                                         : NodePumpState::OFF;
        if (!node_registry_->updateDesiredStateForGroup(group.group_id, target_state)) {
            latchGatewayDegraded("SCHEDULE_FANOUT_LOCK_TIMEOUT");
            return false;
        }
    }

    return true;
}
