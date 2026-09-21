#include "flow_fault_evaluator.h"
#include <cstdio>
#include <cstring>

namespace {
bool isBinaryState(uint8_t value) {
    return value == 0 || value == 1;
}
}

const char* FlowFaultEvaluator::getFsmStateString(FlowIrrigationFsmState state) {
    switch (state) {
        case FlowIrrigationFsmState::IDLE_SAFE_OFF:        return "IDLE_SAFE_OFF";
        case FlowIrrigationFsmState::COMMAND_DISPATCHED:   return "COMMAND_DISPATCHED";
        case FlowIrrigationFsmState::RF_ACKNOWLEDGED:      return "RF_ACKNOWLEDGED";
        case FlowIrrigationFsmState::PUMP_FEEDBACK_ON:     return "PUMP_FEEDBACK_ON";
        case FlowIrrigationFsmState::FLOW_CONFIRMED:       return "FLOW_CONFIRMED";
        case FlowIrrigationFsmState::FAULT_LATCHED:        return "FAULT_LATCHED";
        default:                                           return "UNKNOWN_STATE";
    }
}

const char* FlowFaultEvaluator::getFaultTypeString(FlowFaultType fault) {
    switch (fault) {
        case FlowFaultType::FAULT_NONE:                         return "FAULT_NONE";
        case FlowFaultType::FAULT_NO_FLOW:                      return "NO_FLOW_FAULT";
        case FlowFaultType::FAULT_UNEXPECTED_FLOW:              return "UNEXPECTED_FLOW_FAULT";
        case FlowFaultType::FAULT_OVER_RANGE_FLOW:              return "OVER_RANGE_FLOW_FAULT";
        case FlowFaultType::FAULT_STALE_OR_DISCONNECTED_SENSOR: return "STALE_OR_DISCONNECTED_SENSOR_FAULT";
        case FlowFaultType::FAULT_INVALID_PARAMETERS:           return "INVALID_PARAMETERS_FAULT";
        case FlowFaultType::FAULT_DRIVER_FEEDBACK_MISMATCH:     return "DRIVER_FEEDBACK_MISMATCH_FAULT";
        case FlowFaultType::FAULT_ELECTRICAL_LOAD_FAULT:        return "ELECTRICAL_LOAD_FAULT";
        case FlowFaultType::FAULT_RF_TIMEOUT_OR_NACK:           return "RF_TIMEOUT_OR_NACK_FAULT";
        default:                                                return "UNKNOWN_FAULT";
    }
}

FlowFaultEvaluator::FlowFaultEvaluator()
    : FlowFaultEvaluator(0) {}

FlowFaultEvaluator::FlowFaultEvaluator(uint8_t node_id)
    : node_id_(node_id),
      config_{},
      current_state_(FlowIrrigationFsmState::IDLE_SAFE_OFF),
      latched_fault_(FlowFaultType::FAULT_NONE),
      active_command_id_(0),
      commanded_on_(false),
      command_dispatched_ms_(0),
      ack_received_ms_(0),
      feedback_asserted_ms_(0),
      last_telemetry_ms_(0),
      last_pulse_count_(0),
      last_pulse_changed_ms_(0),
      last_flow_confirmed_ms_(0),
      off_transition_ms_(0),
      last_flow_lpm_x100_(0),
      last_current_ma_(0),
      last_driver_feedback_(0),
      last_audit_record_{} {
}

void FlowFaultEvaluator::reset() {
    current_state_ = FlowIrrigationFsmState::IDLE_SAFE_OFF;
    latched_fault_ = FlowFaultType::FAULT_NONE;
    active_command_id_ = 0;
    commanded_on_ = false;
    command_dispatched_ms_ = 0;
    ack_received_ms_ = 0;
    feedback_asserted_ms_ = 0;
    last_telemetry_ms_ = 0;
    last_pulse_count_ = 0;
    last_pulse_changed_ms_ = 0;
    last_flow_confirmed_ms_ = 0;
    off_transition_ms_ = 0;
    last_flow_lpm_x100_ = 0;
    last_current_ma_ = 0;
    last_driver_feedback_ = 0;
    std::memset(&last_audit_record_, 0, sizeof(last_audit_record_));
}

bool FlowFaultEvaluator::configure(const FlowSafetyConfig& config) {
    if (!config.isValid()) {
        return false;
    }
    config_ = config;
    return true;
}

void FlowFaultEvaluator::recordAudit(uint32_t now_ms, const char* reason) {
    last_audit_record_.timestamp_ms = now_ms;
    last_audit_record_.command_id = active_command_id_;
    last_audit_record_.node_id = node_id_;
    last_audit_record_.state = current_state_;
    last_audit_record_.fault_type = latched_fault_;
    last_audit_record_.flow_lpm_x100 = last_flow_lpm_x100_;
    last_audit_record_.current_ma = last_current_ma_;
    last_audit_record_.driver_feedback = last_driver_feedback_;
    if (reason != nullptr) {
        std::snprintf(last_audit_record_.reason_phrase, sizeof(last_audit_record_.reason_phrase), "%s", reason);
    } else {
        last_audit_record_.reason_phrase[0] = '\0';
    }
}

void FlowFaultEvaluator::latchFault(uint32_t now_ms, FlowFaultType fault, const char* reason) {
    if (latched_fault_ == FlowFaultType::FAULT_NONE) {
        latched_fault_ = fault;
    }
    current_state_ = FlowIrrigationFsmState::FAULT_LATCHED;
    recordAudit(now_ms, reason);
}

bool FlowFaultEvaluator::clearLatchedFault(uint32_t now_ms) {
    // Only allow clearing fault if physical state is idle/off and not leaking flow
    if (last_flow_lpm_x100_ > config_.max_off_flow_lpm_x100 && config_.is_provisioned) {
        return false; // Still physically leaking flow, unsafe to clear
    }
    if (last_driver_feedback_ != 0) {
        return false; // Driver gate still energized, unsafe to clear
    }

    latched_fault_ = FlowFaultType::FAULT_NONE;
    current_state_ = FlowIrrigationFsmState::IDLE_SAFE_OFF;
    commanded_on_ = false;
    recordAudit(now_ms, "FAULT_CLEARED_EXPLICIT_SAFE_OFF");
    return true;
}

bool FlowFaultEvaluator::onCommandDispatched(uint32_t now_ms, uint32_t command_id, bool desired_on) {
    if (isFaultLatched()) {
        return false; // Reject command dispatch when fault is latched (fail-closed)
    }

    if (desired_on && !config_.isValid()) {
        latchFault(now_ms, FlowFaultType::FAULT_INVALID_PARAMETERS, "UNPROVISIONED_OR_INVALID_POLICY");
        return false;
    }

    active_command_id_ = command_id;
    commanded_on_ = desired_on;
    command_dispatched_ms_ = now_ms;
    ack_received_ms_ = 0;
    feedback_asserted_ms_ = 0;
    current_state_ = FlowIrrigationFsmState::COMMAND_DISPATCHED;

    if (!desired_on) {
        off_transition_ms_ = now_ms;
    }

    recordAudit(now_ms, desired_on ? "COMMAND_ON_DISPATCHED" : "COMMAND_OFF_DISPATCHED");
    return true;
}

bool FlowFaultEvaluator::onRfAckReceived(uint32_t now_ms, uint32_t ack_sequence, uint8_t ack_outcome) {
    (void)ack_sequence;
    if (isFaultLatched()) {
        return false;
    }

    if (ack_outcome != 0) { // Non-zero indicates node rejection / NACK
        latchFault(now_ms, FlowFaultType::FAULT_RF_TIMEOUT_OR_NACK, "RF_NODE_NACK_RECEIVED");
        return false;
    }

    if (current_state_ == FlowIrrigationFsmState::COMMAND_DISPATCHED) {
        ack_received_ms_ = now_ms;
        current_state_ = FlowIrrigationFsmState::RF_ACKNOWLEDGED;
        recordAudit(now_ms, "RF_ACK_SUCCESS_AWAITING_PUMP_FEEDBACK");
    }

    return true;
}

bool FlowFaultEvaluator::evaluateTelemetry(uint32_t now_ms,
                                           uint32_t last_command_id,
                                           uint8_t reported_pump_state,
                                           uint8_t driver_feedback,
                                           uint16_t current_ma,
                                           uint16_t flow_lpm_x100,
                                           uint32_t pulse_count,
                                           uint8_t fault_flags) {
    last_telemetry_ms_ = now_ms;
    last_driver_feedback_ = driver_feedback;
    last_current_ma_ = current_ma;
    last_flow_lpm_x100_ = flow_lpm_x100;

    // Track pulse movement for stale sensor detection
    if (pulse_count != last_pulse_count_) {
        last_pulse_count_ = pulse_count;
        last_pulse_changed_ms_ = now_ms;
    }

    // Invariant: Once a fault is latched, intermittent telemetry NEVER clears it
    if (isFaultLatched()) {
        return false;
    }

    // 1. Input sanity and parameter validation
    if (!isBinaryState(reported_pump_state) || !isBinaryState(driver_feedback)) {
        latchFault(now_ms, FlowFaultType::FAULT_INVALID_PARAMETERS, "CORRUPTED_TELEMETRY_BINARY_STATE");
        return false;
    }

    if (fault_flags != 0) {
        latchFault(now_ms, FlowFaultType::FAULT_ELECTRICAL_LOAD_FAULT, "NODE_HARDWARE_FAULT_FLAGS_NONZERO");
        return false;
    }

    if (!config_.isValid()) {
        latchFault(now_ms, FlowFaultType::FAULT_INVALID_PARAMETERS, "UNPROVISIONED_NODE_FLOW_POLICY");
        return false;
    }

    // 2. Over-range flow boundary check (Pipe burst / sensor glitch)
    if (flow_lpm_x100 > config_.max_flow_lpm_x100) {
        latchFault(now_ms, FlowFaultType::FAULT_OVER_RANGE_FLOW, "OVER_RANGE_FLOW_BURST_DETECTED");
        return false;
    }

    // 3. Evaluation when commanded ON
    if (commanded_on_) {
        // If driver feedback is LOW despite commanded ON past grace
        if (driver_feedback == 0) {
            const uint32_t elapsed_since_dispatch = (now_ms >= command_dispatched_ms_) ? (now_ms - command_dispatched_ms_) : 0;
            if (elapsed_since_dispatch >= 1000) { // 1.0s timeout for driver feedback assertion
                latchFault(now_ms, FlowFaultType::FAULT_DRIVER_FEEDBACK_MISMATCH, "DRIVER_FEEDBACK_NOT_ASSERTED_ON");
                return false;
            }
            return true; // Still within grace window
        }

        // Driver feedback is confirmed ON (Tier 1 PASS)
        if (current_state_ == FlowIrrigationFsmState::RF_ACKNOWLEDGED ||
            current_state_ == FlowIrrigationFsmState::COMMAND_DISPATCHED) {
            current_state_ = FlowIrrigationFsmState::PUMP_FEEDBACK_ON;
            feedback_asserted_ms_ = now_ms;
            if (last_pulse_changed_ms_ == 0) {
                last_pulse_changed_ms_ = now_ms;
            }
            recordAudit(now_ms, "PUMP_FEEDBACK_ON_AWAITING_FLOW");
        }

        // 4. Hydraulic flow verification (Tier 3)
        if (flow_lpm_x100 >= config_.min_flow_lpm_x100 && flow_lpm_x100 <= config_.max_flow_lpm_x100) {
            // Flow successfully established!
            if (current_state_ != FlowIrrigationFsmState::FLOW_CONFIRMED) {
                current_state_ = FlowIrrigationFsmState::FLOW_CONFIRMED;
                last_flow_confirmed_ms_ = now_ms;
                recordAudit(now_ms, "FLOW_CONFIRMED_SUCCESS");
            }
            return true;
        }

        // Flow is below minimum threshold
        if (current_state_ == FlowIrrigationFsmState::FLOW_CONFIRMED) {
            // Flow WAS previously confirmed, but has dropped / pulses starved: check stale sensor timeout
            const uint32_t pulse_starvation_ms = (now_ms >= last_pulse_changed_ms_) ? (now_ms - last_pulse_changed_ms_) : 0;
            if (pulse_starvation_ms >= config_.stale_sensor_timeout_ms) {
                latchFault(now_ms, FlowFaultType::FAULT_STALE_OR_DISCONNECTED_SENSOR, "STALE_OR_DISCONNECTED_FLOW_SENSOR");
                return false;
            }
        } else {
            // Still in startup phase: check flow start timeout
            const uint32_t flow_wait_elapsed = (feedback_asserted_ms_ > 0 && now_ms >= feedback_asserted_ms_)
                                                   ? (now_ms - feedback_asserted_ms_)
                                                   : ((now_ms >= command_dispatched_ms_) ? (now_ms - command_dispatched_ms_) : 0);

            if (flow_wait_elapsed >= config_.flow_start_timeout_ms) {
                latchFault(now_ms, FlowFaultType::FAULT_NO_FLOW, "NO_FLOW_FAULT_START_TIMEOUT_EXPIRED");
                return false;
            }
        }
    } else {
        // 5. Evaluation when commanded OFF
        const bool is_settled = (off_transition_ms_ == 0) ||
                                (now_ms >= off_transition_ms_ && (now_ms - off_transition_ms_) >= config_.off_settling_window_ms);

        if (is_settled) {
            if (driver_feedback != 0) {
                latchFault(now_ms, FlowFaultType::FAULT_DRIVER_FEEDBACK_MISMATCH, "DRIVER_STUCK_HIGH_WHEN_COMMANDED_OFF");
                return false;
            }

            if (flow_lpm_x100 > config_.max_off_flow_lpm_x100) {
                latchFault(now_ms, FlowFaultType::FAULT_UNEXPECTED_FLOW, "UNEXPECTED_FLOW_FAULT_LEAK_WHEN_OFF");
                return false;
            }

            if (current_state_ != FlowIrrigationFsmState::IDLE_SAFE_OFF) {
                current_state_ = FlowIrrigationFsmState::IDLE_SAFE_OFF;
                recordAudit(now_ms, "CONFIRMED_SAFE_OFF");
            }
        }
    }

    (void)last_command_id;
    return true;
}

void FlowFaultEvaluator::serviceTimeouts(uint32_t now_ms) {
    if (isFaultLatched()) {
        return;
    }

    if (commanded_on_) {
        // Timeout waiting for RF ACK
        if (current_state_ == FlowIrrigationFsmState::COMMAND_DISPATCHED) {
            const uint32_t ack_wait = (now_ms >= command_dispatched_ms_) ? (now_ms - command_dispatched_ms_) : 0;
            if (ack_wait >= 4000) { // 4s timeout for RF ACK
                latchFault(now_ms, FlowFaultType::FAULT_RF_TIMEOUT_OR_NACK, "RF_ACK_TIMEOUT_DEADLINE_EXPIRED");
                return;
            }
        }

        // Timeout waiting for Flow Confirmation
        if (current_state_ == FlowIrrigationFsmState::PUMP_FEEDBACK_ON) {
            const uint32_t flow_wait = (feedback_asserted_ms_ > 0 && now_ms >= feedback_asserted_ms_)
                                           ? (now_ms - feedback_asserted_ms_)
                                           : ((now_ms >= command_dispatched_ms_) ? (now_ms - command_dispatched_ms_) : 0);
            if (flow_wait >= config_.flow_start_timeout_ms) {
                latchFault(now_ms, FlowFaultType::FAULT_NO_FLOW, "NO_FLOW_FAULT_SERVICE_TIMEOUT");
                return;
            }
        }
    }
}

// FlowFaultEvaluatorRegistry implementation
FlowFaultEvaluatorRegistry::FlowFaultEvaluatorRegistry() {
    reset();
}

void FlowFaultEvaluatorRegistry::reset() {
    for (uint8_t id = RF_PRODUCTION_MIN_NODE_ID; id <= RF_PRODUCTION_MAX_NODE_ID; ++id) {
        evaluators_[id] = FlowFaultEvaluator(id);
    }
}

bool FlowFaultEvaluatorRegistry::configureNode(uint8_t node_id, const FlowSafetyConfig& config) {
    if (!isProductionNodeId(node_id)) {
        return false;
    }
    return evaluators_[node_id].configure(config);
}

bool FlowFaultEvaluatorRegistry::isNodeConfigured(uint8_t node_id) const {
    if (!isProductionNodeId(node_id)) {
        return false;
    }
    return evaluators_[node_id].isConfigured();
}

FlowFaultEvaluator* FlowFaultEvaluatorRegistry::getEvaluator(uint8_t node_id) {
    if (!isProductionNodeId(node_id)) {
        return nullptr;
    }
    return &evaluators_[node_id];
}

const FlowFaultEvaluator* FlowFaultEvaluatorRegistry::getEvaluator(uint8_t node_id) const {
    if (!isProductionNodeId(node_id)) {
        return nullptr;
    }
    return &evaluators_[node_id];
}

void FlowFaultEvaluatorRegistry::serviceAllTimeouts(uint32_t now_ms) {
    for (uint8_t id = RF_PRODUCTION_MIN_NODE_ID; id <= RF_PRODUCTION_MAX_NODE_ID; ++id) {
        evaluators_[id].serviceTimeouts(now_ms);
    }
}

bool FlowFaultEvaluatorRegistry::anyNodeFaultLatched() const {
    for (uint8_t id = RF_PRODUCTION_MIN_NODE_ID; id <= RF_PRODUCTION_MAX_NODE_ID; ++id) {
        if (evaluators_[id].isFaultLatched()) {
            return true;
        }
    }
    return false;
}

bool FlowFaultEvaluatorRegistry::allNodesSafeOff() const {
    for (uint8_t id = RF_PRODUCTION_MIN_NODE_ID; id <= RF_PRODUCTION_MAX_NODE_ID; ++id) {
        if (!evaluators_[id].isSafeOff()) {
            return false;
        }
    }
    return true;
}
