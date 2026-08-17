#include "pump_feedback_evaluator.h"

PumpFeedbackEvaluator::PumpFeedbackEvaluator()
    : _config(PumpFeedbackConfig::defaultDcConfig()),
      _latched_fault(FEEDBACK_FAULT_NONE),
      _health_state(PUMP_HEALTH_OFF_HEALTHY),
      _prev_commanded_on(false),
      _command_transition_ms(0),
      _overcurrent_start_ms(0),
      _in_overcurrent_debounce(false),
      _driver_sense_active(false),
      _load_current_active(false),
      _flow_confirmed(false),
      _last_current_ma(0),
      _last_flow_lpm(0.0f) {
}

PumpFeedbackEvaluator::PumpFeedbackEvaluator(const PumpFeedbackConfig& config)
    : _config(config),
      _latched_fault(FEEDBACK_FAULT_NONE),
      _health_state(PUMP_HEALTH_OFF_HEALTHY),
      _prev_commanded_on(false),
      _command_transition_ms(0),
      _overcurrent_start_ms(0),
      _in_overcurrent_debounce(false),
      _driver_sense_active(false),
      _load_current_active(false),
      _flow_confirmed(false),
      _last_current_ma(0),
      _last_flow_lpm(0.0f) {
}

void PumpFeedbackEvaluator::init(const PumpFeedbackConfig& config) {
    _config = config;
    resetFault();
}

void PumpFeedbackEvaluator::resetFault() {
    _latched_fault = FEEDBACK_FAULT_NONE;
    _health_state = _prev_commanded_on ? PUMP_HEALTH_STARTING_INRUSH : PUMP_HEALTH_OFF_HEALTHY;
    _in_overcurrent_debounce = false;
    _overcurrent_start_ms = 0;
}

void PumpFeedbackEvaluator::_latchFault(FeedbackFaultCode code) {
    if (_latched_fault == FEEDBACK_FAULT_NONE) {
        _latched_fault = code;
    }
    _health_state = PUMP_HEALTH_FAULT_LATCHED;
}

void PumpFeedbackEvaluator::update(uint32_t now_ms,
                                   bool commanded_on,
                                   bool driver_sense_high,
                                   uint16_t current_ma,
                                   float flow_lpm) {
    _last_current_ma = current_ma;
    _last_flow_lpm = flow_lpm;
    _driver_sense_active = driver_sense_high;
    _load_current_active = (current_ma >= _config.current_open_load_min_ma);
    _flow_confirmed = (flow_lpm >= _config.flow_confirmed_min_lpm && flow_lpm <= _config.flow_over_range_max_lpm);

    // If fault is already latched, stay in latched fault safe state until explicit resetFault()
    if (_latched_fault != FEEDBACK_FAULT_NONE) {
        _health_state = PUMP_HEALTH_FAULT_LATCHED;
        return;
    }

    // Detect command state edge transition
    if (commanded_on != _prev_commanded_on) {
        _prev_commanded_on = commanded_on;
        _command_transition_ms = now_ms;
        _in_overcurrent_debounce = false;
        _overcurrent_start_ms = 0;
    }

    const uint32_t elapsed_ms = (now_ms >= _command_transition_ms) ? (now_ms - _command_transition_ms) : 0;

    if (!commanded_on) {
        // --- EVALUATION WHEN COMMANDED OFF ---
        // 1. Check Driver Sense Mismatch (Gate/Opto remains energized)
        if (elapsed_ms >= _config.driver_mismatch_timeout_ms && driver_sense_high) {
            _latchFault(FEEDBACK_FAULT_DRIVER_MISMATCH);
            return;
        }

        // 2. Check Stuck-ON Relay / Shorted MOSFET (Current flows despite command OFF)
        if (elapsed_ms >= _config.open_load_timeout_ms && current_ma > _config.current_leakage_off_max_ma) {
            _latchFault(FEEDBACK_FAULT_STUCK_ON);
            return;
        }

        // 3. Check Unexpected Flow (Liquid moves through pipe despite command OFF)
        if (elapsed_ms >= _config.open_load_timeout_ms && flow_lpm > _config.flow_leakage_max_lpm) {
            _latchFault(FEEDBACK_FAULT_UNEXPECTED_FLOW);
            return;
        }

        _health_state = PUMP_HEALTH_OFF_HEALTHY;
    } else {
        // --- EVALUATION WHEN COMMANDED ON ---
        // 1. Overcurrent / Stall Detection with Inrush Blanking & Debounce
        if (current_ma >= _config.current_stall_overcurrent_ma) {
            if (elapsed_ms >= _config.inrush_blanking_ms) {
                if (!_in_overcurrent_debounce) {
                    _in_overcurrent_debounce = true;
                    _overcurrent_start_ms = now_ms;
                }
                const uint32_t overcurrent_duration = (now_ms >= _overcurrent_start_ms) ? (now_ms - _overcurrent_start_ms) : 0;
                if (overcurrent_duration >= _config.overcurrent_debounce_ms) {
                    _latchFault(FEEDBACK_FAULT_OVERCURRENT_STALL);
                    return;
                }
            }
        } else {
            _in_overcurrent_debounce = false;
        }

        // 2. Driver Sense Mismatch (Driver failed to assert high)
        if (elapsed_ms >= _config.driver_mismatch_timeout_ms && !driver_sense_high) {
            _latchFault(FEEDBACK_FAULT_DRIVER_MISMATCH);
            return;
        }

        // 3. Open Load / Disconnected Motor / Blown Fuse
        if (elapsed_ms >= _config.open_load_timeout_ms && current_ma < _config.current_open_load_min_ma) {
            _latchFault(FEEDBACK_FAULT_OPEN_LOAD);
            return;
        }

        // 4. Over-Range Flow Detection (Burst pipe or sensor burst noise)
        if (flow_lpm > _config.flow_over_range_max_lpm) {
            _latchFault(FEEDBACK_FAULT_OVER_RANGE_FLOW);
            return;
        }

        // 5. Flow Confirmation Timeout & Dry Run Differentiation
        if (elapsed_ms >= _config.flow_confirm_timeout_ms) {
            if (flow_lpm < _config.flow_confirmed_min_lpm) {
                if (current_ma <= _config.current_dry_run_max_ma) {
                    _latchFault(FEEDBACK_FAULT_DRY_RUN);
                } else {
                    _latchFault(FEEDBACK_FAULT_NO_FLOW);
                }
                return;
            }
        }

        // 6. Healthy Active State Assessment
        if (elapsed_ms < _config.inrush_blanking_ms) {
            _health_state = PUMP_HEALTH_STARTING_INRUSH;
        } else if (_flow_confirmed && _load_current_active && _driver_sense_active) {
            _health_state = PUMP_HEALTH_RUNNING_CONFIRMED;
        } else {
            _health_state = PUMP_HEALTH_STARTING_INRUSH;
        }
    }
}
