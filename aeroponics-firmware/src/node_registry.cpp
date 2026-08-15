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
        nodes_[i].flow_lpm_x100 = 0;
        nodes_[i].delivered_volume_ml = 0;
        nodes_[i].last_seen_ms = 0;
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

    nodes_[node_id - 1].group_id = group_id;
    if (group_id == UNASSIGNED_GROUP_ID) {
        nodes_[node_id - 1].desired_state = NodePumpState::OFF;
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

    uint8_t gid = nodes_[node_id - 1].group_id;

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

    NodeState& node = nodes_[node_id - 1];
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

    out_state = nodes_[node_id - 1];

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(mutex_);
#endif
    return true;
}

bool NodeRegistry::updateTelemetry(uint8_t node_id, NodePumpState reported, uint8_t driver_fb,
                                    uint16_t flow_lpm_x100, uint32_t volume_ml, uint32_t timestamp_ms) {
    if (!isValidNodeId(node_id)) return false;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
#else
    std::lock_guard<std::mutex> lock(mutex_);
#endif

    NodeState &node = nodes_[node_id - 1];
    node.reported_state = reported;
    node.driver_feedback = driver_fb;
    node.flow_lpm_x100 = flow_lpm_x100;
    node.delivered_volume_ml = volume_ml;
    node.last_seen_ms = timestamp_ms;
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

    NodeState& node = nodes_[node_id - 1];
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

    NodeState& node = nodes_[node_id - 1];
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

    NodeState& node = nodes_[node_id - 1];
    node.fault_latched = false;
    node.desired_state = NodePumpState::OFF;
    node.health = NodeHealthStatus::OFFLINE;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(mutex_);
#endif
    return true;
}

uint16_t NodeRegistry::evaluateStaleNodes(uint32_t current_time_ms, uint32_t stale_threshold_ms) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
#else
    std::lock_guard<std::mutex> lock(mutex_);
#endif

    uint16_t newly_stale_mask = 0;
    for (uint8_t i = 0; i < MAX_NODES; ++i) {
        if (nodes_[i].health == NodeHealthStatus::ONLINE) {
            if (current_time_ms > nodes_[i].last_seen_ms &&
                (current_time_ms - nodes_[i].last_seen_ms) > stale_threshold_ms) {
                nodes_[i].health = NodeHealthStatus::STALE;
                nodes_[i].desired_state = NodePumpState::OFF;
                nodes_[i].fault_latched = true;
                newly_stale_mask |= (1 << i);
            }
        }
    }

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(mutex_);
#endif
    return newly_stale_mask;
}
