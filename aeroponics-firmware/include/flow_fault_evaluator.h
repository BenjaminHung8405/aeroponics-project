#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Safety FSM and Fault Evaluation Engine for Hydraulic Flow & Pump Actuation
 * 
 * Implements the deterministic safety state machine:
 *   COMMAND_DISPATCHED -> RF_ACKED -> PUMP_FEEDBACK_ON -> FLOW_CONFIRMED
 *   (or transition to FAULT_LATCHED with autonomous Safe-OFF)
 * 
 * Invariants & Architecture Rules (SPEC-FLOW-SAFETY-001):
 * 1. ACK receipt is never evidence of physical pump state or watering success.
 * 2. Successful watering is confirmed ONLY after physical flow reaches min_flow_lpm.
 * 3. Flow thresholds and timeouts are configurable per node/treatment version.
 * 4. Faults are latched fail-closed; intermittent telemetry never self-clears a fault.
 * 5. Over-range flow (> max_flow_lpm) or unexpected flow when OFF triggers immediate Safe-OFF.
 */

#define MAX_EVALUATOR_NODES 4
#define FLOW_AUDIT_STRING_MAX_LEN 96

/**
 * @brief Formal Safety FSM States
 */
enum class FlowIrrigationFsmState : uint8_t {
    IDLE_SAFE_OFF = 0,             // Actuator is OFF, flow is below off-leakage limit
    COMMAND_DISPATCHED = 1,        // Command sent, awaiting RF ACK from remote node
    RF_ACKNOWLEDGED = 2,           // RF ACK received, awaiting physical driver gate feedback
    PUMP_FEEDBACK_ON = 3,          // Driver/load feedback confirmed ON, awaiting flow establishment
    FLOW_CONFIRMED = 4,            // Physical volumetric flow verified within target operating range
    FAULT_LATCHED = 5              // Safety fault latched, hard safe-off active, requires explicit reset
};

/**
 * @brief Comprehensive Fault Classification Codes
 */
enum class FlowFaultType : uint8_t {
    FAULT_NONE = 0,
    FAULT_NO_FLOW = 1,                     // Commanded ON, but flow < min_flow within timeout (dry/clogged)
    FAULT_UNEXPECTED_FLOW = 2,             // Commanded OFF, but flow > max_off_flow after settling window
    FAULT_OVER_RANGE_FLOW = 3,             // Flow exceeded max_flow_lpm (burst pipe / sensor noise)
    FAULT_STALE_OR_DISCONNECTED_SENSOR = 4,// Sensor stopped pulsing / disconnected during active pumping
    FAULT_INVALID_PARAMETERS = 5,          // Unprovisioned policy, corrupted telemetry, or out-of-bounds value
    FAULT_DRIVER_FEEDBACK_MISMATCH = 6,    // Gate/optocoupler feedback does not match commanded state
    FAULT_ELECTRICAL_LOAD_FAULT = 7,       // Motor open circuit / stall overcurrent detected
    FAULT_RF_TIMEOUT_OR_NACK = 8           // Node rejected command or failed to ACK within deadline
};

/**
 * @brief Provenance metadata binding flow safety thresholds to approved control-plane versions
 */
struct FlowSafetyProvenance {
    uint32_t policy_version = 0;
    uint32_t treatment_version_id = 0;
    uint32_t calibration_id = 0;

    constexpr FlowSafetyProvenance() = default;
    constexpr FlowSafetyProvenance(uint32_t pol_ver, uint32_t treat_ver, uint32_t cal_id)
        : policy_version(pol_ver), treatment_version_id(treat_ver), calibration_id(cal_id) {}

    bool isValid() const {
        return policy_version > 0 && treatment_version_id > 0 && calibration_id > 0;
    }
};

/**
 * @brief Configurable flow safety thresholds per node and treatment recipe
 */
struct FlowSafetyConfig {
    uint16_t min_flow_lpm_x100 = 50;         // Minimum flow to confirm watering (e.g. 50 = 0.50 L/min)
    uint16_t max_off_flow_lpm_x100 = 15;     // Maximum allowable leakage when OFF (e.g. 15 = 0.15 L/min)
    uint16_t max_flow_lpm_x100 = 600;        // Upper flow boundary before burst trip (e.g. 600 = 6.00 L/min)
    uint32_t flow_start_timeout_ms = 3000;   // Timeout for flow to establish after pump ON
    uint32_t off_settling_window_ms = 200;   // Settling grace period after turning OFF before leak trip
    uint32_t stale_sensor_timeout_ms = 3000; // Timeout for sensor pulse starvation during pump ON
    FlowSafetyProvenance provenance{};
    bool is_provisioned = false;

    FlowSafetyConfig() = default;
    FlowSafetyConfig(uint16_t min_flow, uint16_t max_off, uint16_t max_flow,
                     uint32_t start_timeout, uint32_t settling_window,
                     uint32_t stale_timeout, const FlowSafetyProvenance& prov)
        : min_flow_lpm_x100(min_flow), max_off_flow_lpm_x100(max_off),
          max_flow_lpm_x100(max_flow), flow_start_timeout_ms(start_timeout),
          off_settling_window_ms(settling_window), stale_sensor_timeout_ms(stale_timeout),
          provenance(prov), is_provisioned(true) {}

    bool isValid() const {
        if (!is_provisioned || !provenance.isValid()) return false;
        if (min_flow_lpm_x100 == 0 || min_flow_lpm_x100 > max_flow_lpm_x100) return false;
        if (max_off_flow_lpm_x100 >= min_flow_lpm_x100 || max_off_flow_lpm_x100 > max_flow_lpm_x100) return false;
        if (max_flow_lpm_x100 > 1000) return false; // Hard physical ceiling 10.0 L/min
        if (flow_start_timeout_ms == 0 || flow_start_timeout_ms > 30000) return false;
        if (off_settling_window_ms > 5000) return false;
        if (stale_sensor_timeout_ms == 0 || stale_sensor_timeout_ms > 30000) return false;
        return true;
    }
};

/**
 * @brief Audit snapshot record generated on state transitions or fault latches
 */
struct FlowSafetyAuditRecord {
    uint32_t timestamp_ms = 0;
    uint32_t command_id = 0;
    uint8_t node_id = 0;
    FlowIrrigationFsmState state = FlowIrrigationFsmState::IDLE_SAFE_OFF;
    FlowFaultType fault_type = FlowFaultType::FAULT_NONE;
    uint16_t flow_lpm_x100 = 0;
    uint16_t current_ma = 0;
    uint8_t driver_feedback = 0;
    char reason_phrase[FLOW_AUDIT_STRING_MAX_LEN] = {};
};

/**
 * @brief Independent Flow & Pump Safety Evaluator FSM for one Node
 */
class FlowFaultEvaluator {
public:
    FlowFaultEvaluator();
    explicit FlowFaultEvaluator(uint8_t node_id);

    /**
     * @brief Configure safety thresholds for this node
     */
    bool configure(const FlowSafetyConfig& config);
    const FlowSafetyConfig& getConfig() const { return config_; }
    bool isConfigured() const { return config_.isValid(); }

    /**
     * @brief Reset node evaluator to clean safe state (only if safe to do so)
     */
    void reset();

    /**
     * @brief Explicitly clear a latched fault (requires supervisor / safe-off confirmation)
     */
    bool clearLatchedFault(uint32_t now_ms);

    /**
     * @brief Event: Outbound command dispatched to node
     */
    bool onCommandDispatched(uint32_t now_ms, uint32_t command_id, bool desired_on);

    /**
     * @brief Event: Inbound RF ACK received from node
     */
    bool onRfAckReceived(uint32_t now_ms, uint32_t ack_sequence, uint8_t ack_outcome);

    /**
     * @brief Periodic / Telemetry update: Evaluate physical sensors and update Safety FSM
     * 
     * @param now_ms Monotonic timestamp in milliseconds
     * @param last_command_id Command ID reported by node
     * @param reported_pump_state 1 = ON, 0 = OFF
     * @param driver_feedback 1 = High, 0 = Low
     * @param current_ma Measured motor load current in mA
     * @param flow_lpm_x100 Volumetric flow rate in L/min * 100
     * @param pulse_count Cumulative pulses
     * @param fault_flags Node hardware fault flags
     * @return true if healthy, false if fault latched or input invalid
     */
    bool evaluateTelemetry(uint32_t now_ms,
                           uint32_t last_command_id,
                           uint8_t reported_pump_state,
                           uint8_t driver_feedback,
                           uint16_t current_ma,
                           uint16_t flow_lpm_x100,
                           uint32_t pulse_count,
                           uint8_t fault_flags);

    /**
     * @brief Periodic service tick to enforce timeouts (flow start, RF ACK, stale sensor)
     */
    void serviceTimeouts(uint32_t now_ms);

    // Status queries
    FlowIrrigationFsmState getFsmState() const { return current_state_; }
    FlowFaultType getLatchedFault() const { return latched_fault_; }
    bool isFaultLatched() const { return latched_fault_ != FlowFaultType::FAULT_NONE; }
    bool isFlowConfirmed() const { return current_state_ == FlowIrrigationFsmState::FLOW_CONFIRMED; }
    bool isPumpFeedbackOn() const { return current_state_ == FlowIrrigationFsmState::PUMP_FEEDBACK_ON; }
    bool isSafeOff() const {
        return current_state_ == FlowIrrigationFsmState::IDLE_SAFE_OFF ||
               current_state_ == FlowIrrigationFsmState::FAULT_LATCHED;
    }

    uint32_t getActiveCommandId() const { return active_command_id_; }
    uint16_t getLastMeasuredFlowLpmX100() const { return last_flow_lpm_x100_; }
    uint32_t getLastFlowConfirmedTimestamp() const { return last_flow_confirmed_ms_; }
    const FlowSafetyAuditRecord& getLastAuditRecord() const { return last_audit_record_; }

    static const char* getFsmStateString(FlowIrrigationFsmState state);
    static const char* getFaultTypeString(FlowFaultType fault);

private:
    uint8_t node_id_;
    FlowSafetyConfig config_;
    FlowIrrigationFsmState current_state_;
    FlowFaultType latched_fault_;

    uint32_t active_command_id_;
    bool commanded_on_;
    uint32_t command_dispatched_ms_;
    uint32_t ack_received_ms_;
    uint32_t feedback_asserted_ms_;
    uint32_t last_telemetry_ms_;
    uint32_t last_pulse_count_;
    uint32_t last_pulse_changed_ms_;
    uint32_t last_flow_confirmed_ms_;
    uint32_t off_transition_ms_;

    uint16_t last_flow_lpm_x100_;
    uint16_t last_current_ma_;
    uint8_t last_driver_feedback_;

    FlowSafetyAuditRecord last_audit_record_;

    void latchFault(uint32_t now_ms, FlowFaultType fault, const char* reason);
    void recordAudit(uint32_t now_ms, const char* reason);
};

/**
 * @brief Multi-Node Registry of Flow & Pump Safety Evaluators for 4 MEGA8 Nodes
 */
class FlowFaultEvaluatorRegistry {
public:
    FlowFaultEvaluatorRegistry();

    void reset();

    bool configureNode(uint8_t node_id, const FlowSafetyConfig& config);
    bool isNodeConfigured(uint8_t node_id) const;

    FlowFaultEvaluator* getEvaluator(uint8_t node_id);
    const FlowFaultEvaluator* getEvaluator(uint8_t node_id) const;

    void serviceAllTimeouts(uint32_t now_ms);

    bool anyNodeFaultLatched() const;
    bool allNodesSafeOff() const;

private:
    FlowFaultEvaluator evaluators_[MAX_EVALUATOR_NODES + 1];
};

/** Canonical Aliases for Production Flow Evaluator */
using FlowEvaluator = FlowFaultEvaluator;
using FlowEvaluatorRegistry = FlowFaultEvaluatorRegistry;
