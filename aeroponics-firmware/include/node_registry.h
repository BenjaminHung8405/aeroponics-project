#pragma once

#include <cstdint>
#include <cstddef>
#include "config.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#else
#include <mutex>
#endif

constexpr uint8_t MAX_NODES = 12;
constexpr uint8_t MAX_TIMER_GROUPS = 4;
constexpr uint8_t UNASSIGNED_GROUP_ID = 0;

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
    uint8_t group_id;              // 0 = UNASSIGNED, 1..4 = Timer Groups
    NodePumpState desired_state;   // OFF or ON
    NodePumpState reported_state;  // OFF or ON
    uint8_t driver_feedback;       // 0 = LOW, 1 = HIGH
    uint16_t flow_lpm_x100;        // e.g. 520 = 5.20 L/min
    uint32_t delivered_volume_ml;  // total mL delivered
    uint32_t last_seen_ms;         // last telemetry / ACK timestamp
    NodeHealthStatus health;       // OFFLINE, ONLINE, STALE, FAULT
    bool fault_latched;            // ON is denied until an authenticated fault reset
};

/** ON requires a current, authenticated node state; OFF is always safe to request. */
bool canAcceptPumpOn(const NodeState& state);

/**
 * @brief Thread-safe Node Registry managing up to 12 dynamic pump nodes.
 */
class NodeRegistry {
public:
    NodeRegistry();
    ~NodeRegistry();

    bool init();
    bool begin() { return init(); }

    /**
     * @brief Assign a node (1..12) to a group (0 = UNASSIGNED, 1..4).
     */
    bool assignNodeToGroup(uint8_t node_id, uint8_t group_id);

    /**
     * @brief Get group assignment for node_id (1..12). Returns 0 if invalid or unassigned.
     */
    uint8_t getNodeGroup(uint8_t node_id) const;

    /**
     * @brief Fan-out target desired pump state to all nodes assigned to group_id (1..4).
     * If group_id is 0 (UNASSIGNED), all nodes in group 0 are set to OFF.
     */
    bool updateDesiredStateForGroup(uint8_t group_id, NodePumpState desired);

    /**
     * @brief Directly set desired state for a single node (1..12).
     */
    bool setDesiredState(uint8_t node_id, NodePumpState desired);

    /**
     * @brief Retrieve snapshot of state for node_id (1..12).
     */
    bool getNodeState(uint8_t node_id, NodeState &out_state) const;

    /**
     * @brief Update reported telemetry state for node_id (1..12).
     */
    bool updateTelemetry(uint8_t node_id, NodePumpState reported, uint8_t driver_fb,
                         uint16_t flow_lpm_x100, uint32_t volume_ml, uint32_t timestamp_ms);

    /**
     * @brief Update health status for node_id (1..12).
     */
    bool updateHealth(uint8_t node_id, NodeHealthStatus health);
    bool updateHealthStatus(uint8_t node_id, NodeHealthStatus health) { return updateHealth(node_id, health); }

    /** Atomically force a node OFF and retain its fault latch across telemetry. */
    bool latchFaultSafeOff(uint8_t node_id);
    /** Clear a fault latch only from an authenticated control boundary. */
    bool resetFault(uint8_t node_id);

    /**
     * @brief Evaluate stale status for all nodes based on timeout threshold (default 15000ms).
     * @return Bitmask of newly stale nodes (bit i set for node i+1).
     */
    uint16_t evaluateStaleNodes(uint32_t current_time_ms, uint32_t stale_threshold_ms = 15000);

private:
    NodeState nodes_[MAX_NODES];
    bool initialized_;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    mutable SemaphoreHandle_t mutex_;
#else
    mutable std::mutex mutex_;
#endif

    bool isValidNodeId(uint8_t node_id) const {
        return node_id >= 1 && node_id <= MAX_NODES;
    }

    bool isValidGroupId(uint8_t group_id) const {
        return group_id <= MAX_TIMER_GROUPS;
    }
};
