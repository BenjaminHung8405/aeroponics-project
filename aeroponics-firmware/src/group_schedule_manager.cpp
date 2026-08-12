#include "group_schedule_manager.h"

GroupScheduleManager::GroupScheduleManager()
    : rtc_(nullptr), node_registry_(nullptr), wdt_(nullptr), initialized_(false) {
    for (uint8_t i = 0; i < MAX_TIMER_GROUPS; ++i) {
        groups_[i].group_id = i + 1;
        groups_[i].assignment_state = GroupAssignmentState::UNASSIGNED;
        groups_[i].current_phase = GroupPhase::PHASE_SPRAYING;
        groups_[i].phase_remaining_s = DEFAULT_SPRAY_DAY_S;
        groups_[i].profile.spray_day_s = DEFAULT_SPRAY_DAY_S;
        groups_[i].profile.cooldown_day_s = DEFAULT_COOLDOWN_DAY_S;
        groups_[i].profile.spray_night_s = DEFAULT_SPRAY_NIGHT_S;
        groups_[i].profile.cooldown_night_s = DEFAULT_COOLDOWN_NIGHT_S;
        groups_[i].is_night_mode = false;
    }
}

GroupScheduleManager::~GroupScheduleManager() {}

bool GroupScheduleManager::begin(IClock* rtc, NodeRegistry* node_registry, IWatchdog* wdt) {
    if (rtc == nullptr || node_registry == nullptr) {
        return false;
    }
    rtc_ = rtc;
    node_registry_ = node_registry;
    wdt_ = wdt;
    initialized_ = true;
    return true;
}

bool GroupScheduleManager::setGroupProfile(uint8_t group_id, const GroupProfile &profile) {
    if (!isValidGroupId(group_id)) return false;

    // Validate bounds
    if (profile.spray_day_s < MIN_SPRAY_DURATION_S || profile.spray_day_s > MAX_SPRAY_DURATION_S ||
        profile.cooldown_day_s < MIN_COOLDOWN_DURATION_S || profile.cooldown_day_s > MAX_COOLDOWN_DURATION_S ||
        profile.spray_night_s < MIN_SPRAY_DURATION_S || profile.spray_night_s > MAX_SPRAY_DURATION_S ||
        profile.cooldown_night_s < MIN_COOLDOWN_DURATION_S || profile.cooldown_night_s > MAX_COOLDOWN_DURATION_S) {
        return false;
    }

    groups_[group_id - 1].profile = profile;
    return true;
}

bool GroupScheduleManager::setGroupActive(uint8_t group_id, bool active) {
    if (!isValidGroupId(group_id)) return false;

    GroupRuntimeState &group = groups_[group_id - 1];
    group.assignment_state = active ? GroupAssignmentState::ACTIVE : GroupAssignmentState::UNASSIGNED;
    if (!active) {
        group.current_phase = GroupPhase::PHASE_SPRAYING;
        group.phase_remaining_s = group.profile.spray_day_s;
        // Fan out OFF to node registry for this group
        if (node_registry_) {
            node_registry_->updateDesiredStateForGroup(group_id, NodePumpState::OFF);
        }
    }
    return true;
}

bool GroupScheduleManager::getGroupRuntimeState(uint8_t group_id, GroupRuntimeState &out_state) const {
    if (!isValidGroupId(group_id)) return false;
    out_state = groups_[group_id - 1];
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
    bool night_mode = sys_time.is_valid ? rtc_->isNightMode() : false;

    for (uint8_t i = 0; i < MAX_TIMER_GROUPS; ++i) {
        GroupRuntimeState &group = groups_[i];
        group.is_night_mode = night_mode;

        if (group.assignment_state == GroupAssignmentState::UNASSIGNED) {
            node_registry_->updateDesiredStateForGroup(group.group_id, NodePumpState::OFF);
            continue;
        }

        // Active Group: advance spray/cooldown phase
        if (group.phase_remaining_s > 1) {
            group.phase_remaining_s -= 1;
        } else {
            // Flip phase
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
        node_registry_->updateDesiredStateForGroup(group.group_id, target_state);
    }

    return true;
}
