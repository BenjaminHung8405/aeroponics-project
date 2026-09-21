#pragma once

#include <cstdint>
#include <cstddef>
#include "config.h"
#include "rf_frame_codec.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#else
#include <mutex>
#endif

enum class NodePumpState : uint8_t {
    OFF = 0x00,
    ON  = 0x01
};

enum class NodeHealthStatus : uint8_t {
    OFFLINE = 0x00,
    ONLINE  = 0x01,
    STALE   = 0x02,
    FAULT   = 0x03
};

struct NodeState {
    uint8_t node_id;               // 1..12
    uint8_t group_id;              // 0 = UNASSIGNED, 4..7 = Timer Groups
    NodePumpState desired_state;   // OFF or ON
    NodePumpState reported_state;  // OFF or ON
    uint8_t driver_feedback;       // 0 = LOW, 1 = HIGH
    uint16_t current_ma;           // Load current in mA
    uint16_t voltage_mv;           // Voltage in mV
    uint16_t flow_lpm_x100;        // e.g. 520 = 5.20 L/min
    uint32_t pulse_count;          // Cumulative flow sensor pulses
    uint32_t delivered_volume_ml;  // total mL delivered
    uint32_t node_timestamp_ms;    // Node-reported monotonic ms / uptime
    uint32_t last_seen_ms;         // Gateway timestamp (ms)
    uint32_t last_command_id;      // Last command ID reported by node
    uint32_t boot_session_id;      // remote node boot session counter for reboot detection
    uint8_t fault_flags;           // Hardware fault flags reported by node
    NodeHealthStatus health;       // OFFLINE, ONLINE, STALE, FAULT
    bool fault_latched;            // ON is denied until an authenticated fault reset
};

/** ON requires a current, authenticated node state; OFF is always safe to request. */
bool canAcceptPumpOn(const NodeState& state);

using NodeRebootCallback = void (*)(uint8_t node_id, uint32_t old_session, uint32_t new_session, void* user_data);

/**
 * @brief Thread-safe Node Registry managing up to 12 dynamic pump nodes (production scope: nodes 4..7).
 */
class NodeRegistry {
public:
    NodeRegistry();
    ~NodeRegistry();

    bool init();
    bool begin() { return init(); }

    /**
     * @brief Assign a node (4..7) to a group (0 = UNASSIGNED, 4..7).
     */
    bool assignNodeToGroup(uint8_t node_id, uint8_t group_id);

    /**
     * @brief Get group assignment for node_id (4..7). Returns 0 if invalid or unassigned.
     */
    uint8_t getNodeGroup(uint8_t node_id) const;

    /**
     * @brief Fan-out target desired pump state to all nodes assigned to group_id (4..7).
     * If group_id is 0 (UNASSIGNED), all nodes in group 0 are set to OFF.
     */
    bool updateDesiredStateForGroup(uint8_t group_id, NodePumpState desired);

    /**
     * @brief Directly set desired state for a single node (4..7).
     */
    bool setDesiredState(uint8_t node_id, NodePumpState desired);

    /**
     * Atomically update a prepared set of node desired states. Every target is
     * validated while one registry lock is held; on failure no target changes.
     */
    bool setDesiredStateForMask(uint16_t node_mask, NodePumpState desired);

    /**
     * @brief Retrieve snapshot of state for node_id (4..7).
     */
    bool getNodeState(uint8_t node_id, NodeState &out_state) const;

    /**
     * @brief Update reported telemetry state for node_id (4..7).
     */
    bool updateTelemetry(uint8_t node_id, NodePumpState reported, uint8_t driver_fb,
                         uint16_t flow_lpm_x100, uint32_t volume_ml, uint32_t timestamp_ms);

    /**
     * @brief Update detailed telemetry with dual timestamps and electrical load feedback.
     */
    bool updateTelemetryDetailed(uint8_t node_id, NodePumpState reported, uint8_t driver_fb,
                                 uint16_t current_ma, uint16_t voltage_mv,
                                 uint16_t flow_lpm_x100, uint32_t pulse_count, uint32_t volume_ml,
                                 uint32_t node_timestamp_ms, uint32_t gateway_timestamp_ms,
                                 uint32_t last_command_id = 0, uint8_t fault_flags = 0);

    /** Refresh authenticated RF liveness without interpreting pump feedback. */
    bool refreshLiveness(uint8_t node_id, uint32_t timestamp_ms);

    /**
     * @brief Update health status for node_id (4..7).
     */
    bool updateHealth(uint8_t node_id, NodeHealthStatus health);
    bool updateHealthStatus(uint8_t node_id, NodeHealthStatus health) { return updateHealth(node_id, health); }

    /** Atomically force a node OFF and retain its fault latch across telemetry. */
    bool latchFaultSafeOff(uint8_t node_id);
    /** Clear a fault latch only from an authenticated control boundary. */
    bool resetFault(uint8_t node_id);

    /**
     * @brief Set global stale timeout threshold in milliseconds (default 15000ms).
     */
    void setStaleThresholdMs(uint32_t threshold_ms) { stale_threshold_ms_ = threshold_ms; }
    uint32_t getStaleThresholdMs() const { return stale_threshold_ms_; }

    /**
     * @brief Check node boot session and detect reboot. Emits reboot callback if session changed.
     * @return true if boot session is valid and updated, false on rejection (e.g. invalid node_id).
     */
    bool updateBootSession(uint8_t node_id, uint32_t boot_session_id, bool& out_reboot_detected);

    /**
     * @brief Register callback to receive notifications when a node reboots.
     */
    void setRebootCallback(NodeRebootCallback cb, void* user_data = nullptr) {
        reboot_cb_ = cb;
        reboot_cb_user_data_ = user_data;
    }

    /**
     * @brief Evaluate stale status for all nodes based on timeout threshold.
     * @param current_time_ms Current time in ms.
     * @param stale_threshold_ms If 0, uses registry default (stale_threshold_ms_).
     * @return Bitmask of newly stale nodes (bit i set for node i+1).
     */
    uint16_t evaluateStaleNodes(uint32_t current_time_ms, uint32_t stale_threshold_ms = 0);

private:
    NodeState nodes_[MAX_NODES];
    bool initialized_;
    uint32_t stale_threshold_ms_ = 15000;
    NodeRebootCallback reboot_cb_ = nullptr;
    void* reboot_cb_user_data_ = nullptr;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    mutable SemaphoreHandle_t mutex_;
#else
    mutable std::mutex mutex_;
#endif

    /** Returns true if node_id is within the PRODUCTION-accepted range (4..7).
     *  Node IDs 5..12 are protocol-capacity backlog; reject in all production paths. */
    bool isValidNodeId(uint8_t node_id) const {
        return isProductionNodeId(node_id);
    }

    static uint8_t nodeIndex(uint8_t node_id) { return static_cast<uint8_t>(node_id - 1U); }

    /** Returns true if node_id is within the full protocol capacity (1..12).
     *  Use only in prototype/backlog paths, never in production enforcement. */
    bool isProtocolCapacityNodeId(uint8_t node_id) const {
        return node_id >= RF_MIN_NODE_ID && node_id <= MAX_NODES;
    }

    bool isValidGroupId(uint8_t group_id) const {
        return group_id <= MAX_TIMER_GROUPS;
    }
};
