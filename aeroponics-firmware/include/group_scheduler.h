#pragma once

#include <cstdint>
#include <cstddef>
#include "config.h"
#include "core/IClock.h"
#include "core/IWatchdog.h"
#include "node_registry.h"
#include "treatment_manager.h"
#include "pump_node_controller.h"

// Forward declaration
class ICommandOutcomeSink;

enum class GroupAssignmentState : uint8_t {
    UNASSIGNED = 0x00,
    ACTIVE     = 0x01,
    PAUSED     = 0x02
};

enum class GroupPhase : uint8_t {
    PHASE_SPRAYING     = 0x00,
    PHASE_COOLING_DOWN = 0x01
};

struct PublishedTreatmentAssignment {
    uint32_t season_id = 0;
    uint32_t treatment_version_id = 0;
    uint32_t version = 0;
    GroupProfile profile{};

    bool isValid() const {
        return season_id > 0 && treatment_version_id > 0 && version > 0 && profile.isValid();
    }
};

struct GroupRuntimeState {
    uint8_t group_id = 0;                     // 1..4
    GroupAssignmentState assignment_state = GroupAssignmentState::UNASSIGNED;
    GroupPhase current_phase = GroupPhase::PHASE_SPRAYING;
    uint32_t phase_remaining_s = 0;
    GroupProfile profile{};
    uint32_t season_id = 0;
    uint32_t treatment_version_id = 0;
    uint32_t treatment_version = 0;
    bool is_night_mode = false;
    uint32_t pause_remaining_s = 0;
};

struct AssignmentAuditEvent {
    uint32_t assignment_version = 0;
    uint32_t timestamp_ms = 0;
    char actor[32] = {};
    uint8_t node_id = 0;
    uint8_t old_group_id = 0;
    uint8_t new_group_id = 0;
    const char* reason = nullptr;
};

using AssignmentAuditCallback = void (*)(const AssignmentAuditEvent& event, void* user_data);

struct VersionedGroupAssignment {
    uint32_t assignment_version = 0;
    uint8_t node_id = 0;   // 4..7
    uint8_t group_id = 0;  // 0..4 (0 = UNASSIGNED)
    uint32_t effective_at = 0;
    char actor[32] = {};
};

/**
 * @brief Production Group Scheduler managing 4 dynamic timer groups, versioned group assignments,
 * Day/Night transitions (Asia/Ho_Chi_Minh UTC+7), and manual override / pause / resume policies.
 */
class GroupScheduler {
public:
    GroupScheduler();
    virtual ~GroupScheduler();

    bool begin(IClock* rtc, NodeRegistry* node_registry, IWatchdog* wdt = nullptr,
               ICommandOutcomeSink* safety_sink = nullptr,
               PumpNodeController* controller = nullptr);

    void setAuditCallback(AssignmentAuditCallback cb, void* user_data = nullptr) {
        audit_cb_ = cb;
        audit_cb_user_data_ = user_data;
    }

    /** Configure schedule profile for group (1..4). */
    bool setGroupProfile(uint8_t group_id, const GroupProfile &profile);

    /** Set group state (1..4) to ACTIVE or UNASSIGNED. */
    bool setGroupActive(uint8_t group_id, bool active);

    /** Accept only a backend-validated PUBLISHED treatment and atomically authorize the group. */
    bool applyPublishedTreatment(uint8_t group_id, const PublishedTreatmentAssignment& assignment);

    /** Apply versioned group assignment with single active group check and audit event emission. */
    bool assignNodeVersioned(const VersionedGroupAssignment& assignment);

    /** Unassign node back to group 0 (UNASSIGNED) safely. */
    bool unassignNodeVersioned(uint8_t node_id, uint32_t assignment_version, const char* actor,
                               uint32_t timestamp_ms);

    /** Query runtime snapshot for group (1..4). */
    bool getGroupRuntimeState(uint8_t group_id, GroupRuntimeState &out_state) const;

    /** RTC loss is a fail-safe event: force OFF and require later re-authorization. */
    bool forceSafeOff();

    /** Step 1-second deterministic schedule tick across all 4 groups and fan-out to NodeRegistry. */
    bool stepGroupSchedule();

    /** Calculate if ICT time corresponds to Day Mode (06:00..18:00) or Night Mode. */
    static bool isIctDayMode(const SystemTime& time);
    static bool isIctDayMode(uint8_t hour, uint8_t minute = 0);

    /** Apply manual override with strict safety FSM check (fault lockout). */
    bool applyManualNodeOverride(uint8_t node_id, NodePumpState desired, uint32_t lease_or_duration_ms,
                                 const char* command_id, const char* source = "MANUAL_OVERRIDE");

    /** Pause an active group for duration_ms. */
    bool pauseGroup(uint8_t group_id, uint32_t pause_duration_s, const char* command_id);

    /** Resume a paused group back to active schedule. */
    bool resumeGroup(uint8_t group_id, const char* command_id);

    bool isGatewayDegraded() const { return gateway_degraded_; }
    uint32_t getActiveAssignmentVersion() const { return active_assignment_version_; }
    bool getGroupState(uint8_t group_id, GroupRuntimeState& out_state) const;

private:
    IClock* rtc_;
    NodeRegistry* node_registry_;
    IWatchdog* wdt_;
    ICommandOutcomeSink* safety_sink_;
    PumpNodeController* controller_;

    GroupRuntimeState groups_[MAX_TIMER_GROUPS];
    bool initialized_;
    bool gateway_degraded_ = false;
    uint32_t active_assignment_version_ = 0;

    AssignmentAuditCallback audit_cb_ = nullptr;
    void* audit_cb_user_data_ = nullptr;

    bool isValidGroupId(uint8_t group_id) const {
        return group_id >= 1 && group_id <= MAX_TIMER_GROUPS;
    }
    bool isValidNodeId(uint8_t node_id) const {
        return isProductionNodeId(node_id);
    }
    bool validateRuntimeClock(bool& night_mode);
    bool forceUnassignedGroupOff(GroupRuntimeState& group);
    bool stepActiveGroup(GroupRuntimeState& group, bool night_mode);
    void advanceGroupPhase(GroupRuntimeState& group, bool night_mode);
    void latchGatewayDegraded(const char* reason);
    void emitAuditEvent(const AssignmentAuditEvent& event);
};

// Backward-compatibility alias
using GroupScheduleManager = GroupScheduler;
