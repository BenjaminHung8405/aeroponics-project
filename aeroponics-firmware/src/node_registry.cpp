#include "node_registry.h"
#include <cstring>

bool canAcceptPumpOn(const NodeState& state) {
    return state.group_id != UNASSIGNED_GROUP_ID &&
           state.health == NodeHealthStatus::ONLINE && !state.fault_latched;
}

NodeRegistry::NodeRegistry() : initialized_(false) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    mutex_ = nullptr;
#endif
    for (uint8_t i = 0; i < MAX_NODES; ++i) {
        nodes_[i].node_id = i + 1;
        nodes_[i].group_id = UNASSIGNED_GROUP_ID;
        nodes_[i].desired_state = NodePumpState::OFF;
        nodes_[i].reported_state = NodePumpState::OFF;
        nodes_[i].driver_feedback = 0;
        nodes_[i].current_ma = 0;
        nodes_[i].voltage_mv = 0;
        nodes_[i].flow_lpm_x100 = 0;
        nodes_[i].pulse_count = 0;
        nodes_[i].delivered_volume_ml = 0;
        nodes_[i].node_timestamp_ms = 0;
        nodes_[i].last_seen_ms = 0;
        nodes_[i].last_command_id = 0;
        nodes_[i].boot_session_id = 0;
        nodes_[i].fault_flags = 0;
        nodes_[i].health = NodeHealthStatus::OFFLINE;
        nodes_[i].fault_latched = false;
    }
}

NodeRegistry::~NodeRegistry() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
#endif
}

bool NodeRegistry::init() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutex();
        if (mutex_ == nullptr) {
            return false;
        }
    }
#endif
    initialized_ = true;
    return true;
}

bool NodeRegistry::assignNodeToGroup(uint8_t node_id, uint8_t group_id) {
    if (!isValidNodeId(node_id) || !isValidGroupId(group_id)) {
        return false;
    }

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
#else
    std::lock_guard<std::mutex> lock(mutex_);
#endif

    nodes_[nodeIndex(node_id)].group_id = group_id;
    if (group_id == UNASSIGNED_GROUP_ID) {
        nodes_[nodeIndex(node_id)].desired_state = NodePumpState::OFF;
    }

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(mutex_);
#endif
    return true;
}

uint8_t NodeRegistry::getNodeGroup(uint8_t node_id) const {
    if (!isValidNodeId(node_id)) return UNASSIGNED_GROUP_ID;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return UNASSIGNED_GROUP_ID;
#else
    std::lock_guard<std::mutex> lock(mutex_);
#endif

    uint8_t gid = nodes_[nodeIndex(node_id)].group_id;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(mutex_);
#endif
    return gid;
}

bool NodeRegistry::updateDesiredStateForGroup(uint8_t group_id, NodePumpState desired) {
    if (!isValidGroupId(group_id)) return false;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
#else
    std::lock_guard<std::mutex> lock(mutex_);
#endif

    for (uint8_t i = 0; i < MAX_NODES; ++i) {
        if (nodes_[i].group_id == group_id) {
            // UNASSIGNED group (0) is always OFF
            if (group_id == UNASSIGNED_GROUP_ID) {
                nodes_[i].desired_state = NodePumpState::OFF;
            } else if (desired == NodePumpState::OFF || canAcceptPumpOn(nodes_[i])) {
                nodes_[i].desired_state = desired;
            }
        }
    }

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(mutex_);
#endif
    return true;
}

bool NodeRegistry::setDesiredState(uint8_t node_id, NodePumpState desired) {
    if (!isValidNodeId(node_id)) return false;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
#else
    std::lock_guard<std::mutex> lock(mutex_);
#endif

    NodeState& node = nodes_[nodeIndex(node_id)];
    if (desired == NodePumpState::ON && !canAcceptPumpOn(node)) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        xSemaphoreGive(mutex_);
#endif
        return false;
    }
    node.desired_state = desired;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(mutex_);
#endif
    return true;
}

bool NodeRegistry::setDesiredStateForMask(uint16_t node_mask, NodePumpState desired) {
    if (node_mask == 0) return false;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
#else
    std::lock_guard<std::mutex> lock(mutex_);
#endif

    for (uint8_t i = 0; i < MAX_NODES; ++i) {
        if ((node_mask & (static_cast<uint16_t>(1U) << i)) != 0 &&
            desired == NodePumpState::ON && !canAcceptPumpOn(nodes_[i])) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
            xSemaphoreGive(mutex_);
#endif
            return false;
        }
    }
    for (uint8_t i = 0; i < MAX_NODES; ++i) {
        if ((node_mask & (static_cast<uint16_t>(1U) << i)) != 0) nodes_[i].desired_state = desired;
    }

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(mutex_);
#endif
    return true;
}

bool NodeRegistry::getNodeState(uint8_t node_id, NodeState &out_state) const {
    if (!isValidNodeId(node_id)) return false;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
#else
    std::lock_guard<std::mutex> lock(mutex_);
#endif

    out_state = nodes_[nodeIndex(node_id)];

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(mutex_);
#endif
    return true;
}

bool NodeRegistry::updateTelemetry(uint8_t node_id, NodePumpState reported, uint8_t driver_fb,
                                    uint16_t flow_lpm_x100, uint32_t volume_ml, uint32_t timestamp_ms) {
    return updateTelemetryDetailed(node_id, reported, driver_fb, 0, 0,
                                   flow_lpm_x100, 0, volume_ml,
                                   timestamp_ms, timestamp_ms, 0, 0);
}

bool NodeRegistry::updateTelemetryDetailed(uint8_t node_id, NodePumpState reported, uint8_t driver_fb,
                                         uint16_t current_ma, uint16_t voltage_mv,
                                         uint16_t flow_lpm_x100, uint32_t pulse_count, uint32_t volume_ml,
                                         uint32_t node_timestamp_ms, uint32_t gateway_timestamp_ms,
                                         uint32_t last_command_id, uint8_t fault_flags) {
    if (!isValidNodeId(node_id)) return false;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
#else
    std::lock_guard<std::mutex> lock(mutex_);
#endif

    NodeState &node = nodes_[nodeIndex(node_id)];
    node.reported_state = reported;
    node.driver_feedback = driver_fb;
    node.current_ma = current_ma;
    node.voltage_mv = voltage_mv;
    node.flow_lpm_x100 = flow_lpm_x100;
    node.pulse_count = pulse_count;
    node.delivered_volume_ml = volume_ml;
    node.node_timestamp_ms = node_timestamp_ms;
    node.last_seen_ms = gateway_timestamp_ms;
    node.last_command_id = last_command_id;
    node.fault_flags = fault_flags;

    if (node.health == NodeHealthStatus::STALE) {
        node.health = NodeHealthStatus::FAULT;
        node.fault_latched = true;
        node.desired_state = NodePumpState::OFF;
    } else if (node.health != NodeHealthStatus::FAULT) {
        node.health = node.fault_latched ? NodeHealthStatus::FAULT : NodeHealthStatus::ONLINE;
    }

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(mutex_);
#endif
    return true;
}

bool NodeRegistry::refreshLiveness(uint8_t node_id, uint32_t timestamp_ms) {
    if (!isValidNodeId(node_id)) return false;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
#else
    std::lock_guard<std::mutex> lock(mutex_);
#endif

    NodeState& node = nodes_[nodeIndex(node_id)];
    node.last_seen_ms = timestamp_ms;
    if (node.health != NodeHealthStatus::FAULT && !node.fault_latched) {
        node.health = NodeHealthStatus::ONLINE;
    }

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(mutex_);
#endif
    return true;
}

bool NodeRegistry::updateHealth(uint8_t node_id, NodeHealthStatus health) {
    if (!isValidNodeId(node_id)) return false;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
#else
    std::lock_guard<std::mutex> lock(mutex_);
#endif

    NodeState& node = nodes_[nodeIndex(node_id)];
    if (health == NodeHealthStatus::FAULT || health == NodeHealthStatus::STALE) {
        node.desired_state = NodePumpState::OFF;
        node.fault_latched = true;
    }
    node.health = node.fault_latched ? NodeHealthStatus::FAULT : health;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(mutex_);
#endif
    return true;
}

bool NodeRegistry::latchFaultSafeOff(uint8_t node_id) {
    return updateHealth(node_id, NodeHealthStatus::FAULT);
}

bool NodeRegistry::resetFault(uint8_t node_id) {
    if (!isValidNodeId(node_id)) return false;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
#else
    std::lock_guard<std::mutex> lock(mutex_);
#endif

    NodeState& node = nodes_[nodeIndex(node_id)];
    node.fault_latched = false;
    node.desired_state = NodePumpState::OFF;
    node.health = NodeHealthStatus::OFFLINE;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(mutex_);
#endif
    return true;
}

bool NodeRegistry::updateBootSession(uint8_t node_id, uint32_t boot_session_id, bool& out_reboot_detected) {
    out_reboot_detected = false;
    if (!isValidNodeId(node_id) || boot_session_id == 0) return false;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
#else
    std::lock_guard<std::mutex> lock(mutex_);
#endif

    NodeState& node = nodes_[nodeIndex(node_id)];
    uint32_t old_session = node.boot_session_id;
    if (old_session != 0 && old_session != boot_session_id) {
        out_reboot_detected = true;
    }
    node.boot_session_id = boot_session_id;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(mutex_);
#endif

    if (out_reboot_detected && reboot_cb_ != nullptr) {
        reboot_cb_(node_id, old_session, boot_session_id, reboot_cb_user_data_);
    }
    return true;
}

uint16_t NodeRegistry::evaluateStaleNodes(uint32_t current_time_ms, uint32_t stale_threshold_ms) {
    const uint32_t effective_threshold = (stale_threshold_ms > 0) ? stale_threshold_ms : stale_threshold_ms_;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
#else
    std::lock_guard<std::mutex> lock(mutex_);
#endif

    uint16_t newly_stale_mask = 0;
    for (uint8_t node_id = RF_PRODUCTION_MIN_NODE_ID; node_id <= RF_PRODUCTION_MAX_NODE_ID; ++node_id) {
        NodeState& node = nodes_[nodeIndex(node_id)];
        if (node.health == NodeHealthStatus::ONLINE) {
            if (current_time_ms > node.last_seen_ms &&
                (current_time_ms - node.last_seen_ms) > effective_threshold) {
                node.health = NodeHealthStatus::STALE;
                node.desired_state = NodePumpState::OFF;
                node.fault_latched = true;
                newly_stale_mask |= static_cast<uint16_t>(1U << (node_id - RF_PRODUCTION_MIN_NODE_ID));
            }
        }
    }

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(mutex_);
#endif
    return newly_stale_mask;
}
