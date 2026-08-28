#include "node_actuator.h"

NodeActuator::NodeActuator()
    : config_(PumpFeedbackConfig::defaultDcConfig()),
      evaluator_(config_),
      flow_counter_(nullptr),
      output_pin_level_(false),
      driver_sense_level_(false),
      load_sense_level_(false),
      current_ma_(0),
      flow_lpm_x100_(0),
      volume_ml_(0),
      pulses_(0),
      simulated_driver_sense_(false),
      simulated_load_sense_(false),
      simulated_current_ma_(false) {
}

NodeActuator::NodeActuator(const PumpFeedbackConfig& config)
    : config_(config),
      evaluator_(config),
      flow_counter_(nullptr),
      output_pin_level_(false),
      driver_sense_level_(false),
      load_sense_level_(false),
      current_ma_(0),
      flow_lpm_x100_(0),
      volume_ml_(0),
      pulses_(0),
      simulated_driver_sense_(false),
      simulated_load_sense_(false),
      simulated_current_ma_(false) {
}

void NodeActuator::begin() {
    // Invariant: Physical output forced LOW at boot before logic/communication
    output_pin_level_ = false;
    evaluator_.resetFault();
    syncHardwareReadings();
}

void NodeActuator::syncHardwareReadings() {
    if (!simulated_driver_sense_) {
        // In real hardware, read GPIO driver sense pin; in default mock, matches physical output unless faulted
        driver_sense_level_ = output_pin_level_;
    }
    if (!simulated_current_ma_) {
        // In real hardware, read ADC current; in default mock, nominal 2.0A when ON, 0mA when OFF
        current_ma_ = output_pin_level_ ? 2000 : 0;
    }
    if (!simulated_load_sense_) {
        load_sense_level_ = (current_ma_ >= config_.current_open_load_min_ma);
    }
}

void NodeActuator::setPumpOutput(bool level) {
    if (evaluator_.isFaultLatched()) {
        // Safety interlock: cannot actuate ON while fault is latched
        output_pin_level_ = false;
        syncHardwareReadings();
        return;
    }

    output_pin_level_ = level;
    syncHardwareReadings();
}

bool NodeActuator::readDriverSense() {
    return driver_sense_level_;
}

bool NodeActuator::readLoadSense() {
    return load_sense_level_;
}

uint16_t NodeActuator::readCurrentMa() {
    return current_ma_;
}

uint16_t NodeActuator::readFlowLpmX100() {
    return flow_lpm_x100_;
}

uint32_t NodeActuator::readDeliveredVolumeMl() {
    return volume_ml_;
}

uint32_t NodeActuator::readPulseCount() {
    return pulses_;
}

bool NodeActuator::getOutputLevel() const {
    return output_pin_level_;
}

void NodeActuator::updateFeedback(uint32_t current_time_ms, float flow_lpm) {
    if (flow_counter_ != nullptr) {
        FlowSnapshot snap = flow_counter_->takeSnapshot(current_time_ms, output_pin_level_);
        flow_lpm_x100_ = snap.flow_lpm_x100;
        volume_ml_ = snap.delivered_volume_ml;
        pulses_ = snap.pulse_count;
        flow_lpm = snap.flow_lpm;
    }

    evaluator_.update(current_time_ms,
                      output_pin_level_,
                      driver_sense_level_,
                      current_ma_,
                      flow_lpm);

    if (evaluator_.isFaultLatched()) {
        // Autonomous fail-safe: force hard safe-off upon fault detection
        output_pin_level_ = false;
        if (!simulated_driver_sense_) {
            driver_sense_level_ = false;
        }
        if (!simulated_current_ma_) {
            current_ma_ = 0;
        }
        if (!simulated_load_sense_) {
            load_sense_level_ = false;
        }
    }
}

bool NodeActuator::isActuatorFaultLatched() const {
    return evaluator_.isFaultLatched();
}

uint8_t NodeActuator::getActuatorFaultCode() const {
    return static_cast<uint8_t>(evaluator_.getFaultCode());
}

void NodeActuator::resetActuatorFault() {
    evaluator_.resetFault();
    output_pin_level_ = false;
    syncHardwareReadings();
}
