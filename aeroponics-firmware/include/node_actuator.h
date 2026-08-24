#ifndef NODE_ACTUATOR_H
#define NODE_ACTUATOR_H

#include <cstdint>
#include <stdbool.h>
#include "node_command_processor.h"
#include "pump_feedback_evaluator.h"

/**
 * @brief Node Actuator implementing explicit multi-tier feedback sensing and safety FSM.
 * 
 * Hardware & Architecture Contract (SPEC-FEEDBACK-001):
 * - Tier 0: Commanded Output Level (GPIO Control Pin)
 * - Tier 1: Driver Feedback (Optocoupler / Gate Voltage Sense)
 * - Tier 2: Electrical Load Feedback (Current Sensor ACS712 / Shunt mA)
 * - Tier 3: Hydraulic / Flow Feedback (Pulse Counter / Flow Meter)
 * 
 * Explicit State Invariant:
 * Commanded State != Driver Feedback != Load Current != Flow Rate
 */
class NodeActuator : public IPumpActuatorDriver {
public:
    NodeActuator();
    explicit NodeActuator(const PumpFeedbackConfig& config);
    ~NodeActuator() override = default;

    /**
     * @brief Initialize actuator. Guarantees physical output is forced LOW (Safe-OFF) at boot.
     */
    void begin();

    // IPumpActuatorDriver interface
    void setPumpOutput(bool level) override;
    bool readDriverSense() override;
    bool readLoadSense() override;
    uint16_t readCurrentMa() override;
    uint16_t readFlowLpmX100() override;
    uint32_t readDeliveredVolumeMl() override;
    uint32_t readPulseCount() override;
    bool getOutputLevel() const override;

    // Safety & Feedback evaluation
    void updateFeedback(uint32_t current_time_ms, float flow_lpm = 0.0f) override;
    bool isActuatorFaultLatched() const override;
    uint8_t getActuatorFaultCode() const override;
    void resetActuatorFault() override;

    // Direct telemetry & query getters
    uint8_t getReportedPumpState() const { return output_pin_level_ ? 1 : 0; }
    uint8_t getDriverFeedbackState() const { return driver_sense_level_ ? 1 : 0; }
    uint8_t getLoadFeedbackState() const { return load_sense_level_ ? 1 : 0; }
    PumpHealthState getHealthState() const { return evaluator_.getHealthState(); }
    FeedbackFaultCode getEvaluatorFaultCode() const { return evaluator_.getFaultCode(); }

    // Simulation / Hardware injection hooks (for testability & hardware abstraction)
    void setDriverSenseSimulated(bool level) {
        simulated_driver_sense_ = true;
        driver_sense_level_ = level;
    }
    void setLoadSenseSimulated(bool level) {
        simulated_load_sense_ = true;
        load_sense_level_ = level;
    }
    void setCurrentMaSimulated(uint16_t ma) {
        simulated_current_ma_ = true;
        current_ma_ = ma;
        load_sense_level_ = (ma >= config_.current_open_load_min_ma);
    }
    void setFlowLpmX100(uint16_t flow) { flow_lpm_x100_ = flow; }
    void setDeliveredVolumeMl(uint32_t vol) { volume_ml_ = vol; }
    void setPulseCount(uint32_t pulses) { pulses_ = pulses; }
    void clearSimulatedOverrides() {
        simulated_driver_sense_ = false;
        simulated_load_sense_ = false;
        simulated_current_ma_ = false;
    }

private:
    PumpFeedbackConfig config_;
    PumpFeedbackEvaluator evaluator_;

    bool output_pin_level_;
    bool driver_sense_level_;
    bool load_sense_level_;
    uint16_t current_ma_;
    uint16_t flow_lpm_x100_;
    uint32_t volume_ml_;
    uint32_t pulses_;

    bool simulated_driver_sense_;
    bool simulated_load_sense_;
    bool simulated_current_ma_;

    void syncHardwareReadings();
};

#endif // NODE_ACTUATOR_H
