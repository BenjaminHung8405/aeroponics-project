#pragma once

#include <cstdint>
#include "config.h"
#include "core/IClock.h"
#include "core/IWatchdog.h"
#include "node_registry.h"

constexpr uint32_t GROUP_DEFAULT_SPRAY_DAY_S = 30;
constexpr uint32_t GROUP_DEFAULT_COOLDOWN_DAY_S = 300;
constexpr uint32_t GROUP_DEFAULT_SPRAY_NIGHT_S = 30;
constexpr uint32_t GROUP_DEFAULT_COOLDOWN_NIGHT_S = 600;

constexpr uint32_t GROUP_MIN_SPRAY_DURATION_S = 5;
constexpr uint32_t GROUP_MAX_SPRAY_DURATION_S = 300;
constexpr uint32_t GROUP_MIN_COOLDOWN_DURATION_S = 30;
constexpr uint32_t GROUP_MAX_COOLDOWN_DURATION_S = 7200;

enum class GroupAssignmentState : uint8_t {
    UNASSIGNED = 0x00,
    ACTIVE     = 0x01
};

enum class GroupPhase : uint8_t {
    PHASE_SPRAYING     = 0x00,
    PHASE_COOLING_DOWN = 0x01
};

struct GroupProfile {
    uint32_t spray_day_s;
    uint32_t cooldown_day_s;
    uint32_t spray_night_s;
    uint32_t cooldown_night_s;
};

struct GroupRuntimeState {
    uint8_t group_id;                     // 1..4
    GroupAssignmentState assignment_state; // UNASSIGNED or ACTIVE
    GroupPhase current_phase;             // PHASE_SPRAYING or PHASE_COOLING_DOWN
    uint32_t phase_remaining_s;
    GroupProfile profile;
    bool is_night_mode;
};

/**
 * @brief Group Schedule Manager orchestrating 4 dynamic timer groups and fanning out commands via NodeRegistry.
 */
class GroupScheduleManager {
public:
    GroupScheduleManager();
    ~GroupScheduleManager();

    /**
     * @brief Initialize Group Schedule Manager with required RTC and NodeRegistry dependencies.
     */
    bool begin(IClock* rtc, NodeRegistry* node_registry, IWatchdog* wdt = nullptr);

    /**
     * @brief Configure schedule profile for group (1..4).
     */
    bool setGroupProfile(uint8_t group_id, const GroupProfile &profile);

    /**
     * @brief Set group state (1..4) to ACTIVE or UNASSIGNED.
     */
    bool setGroupActive(uint8_t group_id, bool active);

    /**
     * @brief Query runtime snapshot for group (1..4).
     */
    bool getGroupRuntimeState(uint8_t group_id, GroupRuntimeState &out_state) const;

    /** RTC loss is a fail-safe event: force OFF and require later re-authorization. */
    bool forceSafeOff();

    /**
     * @brief Step 1-second deterministic schedule tick across all 4 groups and fan-out to NodeRegistry.
     */
    bool stepGroupSchedule();

private:
    IClock* rtc_;
    NodeRegistry* node_registry_;
    IWatchdog* wdt_;

    GroupRuntimeState groups_[MAX_TIMER_GROUPS];
    bool initialized_;

    bool isValidGroupId(uint8_t group_id) const {
        return group_id >= 1 && group_id <= MAX_TIMER_GROUPS;
    }
};
