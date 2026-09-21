#ifndef PUMP_FEEDBACK_EVALUATOR_H
#define PUMP_FEEDBACK_EVALUATOR_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

/**
 * @brief Pump Feedback Classification & Multi-Tier Safety Evaluation
 * 
 * Implements the Defence-in-Depth Pump Feedback model:
 *   Tier 1: Driver Feedback (Optocoupler / Gate Bias Sense)
 *   Tier 2: Electrical Load Feedback (Current Sensing / Shunt / Hall ACS712)
 *   Tier 3: Hydraulic / Flow Feedback (Positive Displacement / Turbine Flow Meter)
 * 
 * Invariant: Commanded State != Driver Feedback != Load Current != Liquid Flow
 */

enum FeedbackFaultCode : uint8_t {
    FEEDBACK_FAULT_NONE                = 0,
    FEEDBACK_FAULT_DRIVER_MISMATCH     = 1,
    FEEDBACK_FAULT_OPEN_LOAD           = 2,
    FEEDBACK_FAULT_OVERCURRENT_STALL   = 3,
    FEEDBACK_FAULT_STUCK_ON            = 4,
    FEEDBACK_FAULT_DRY_RUN             = 5,
    FEEDBACK_FAULT_NO_FLOW             = 6,
    FEEDBACK_FAULT_UNEXPECTED_FLOW     = 7,
    FEEDBACK_FAULT_OVER_RANGE_FLOW     = 8
};

enum PumpHealthState : uint8_t {
    PUMP_HEALTH_OFF_HEALTHY            = 0,
    PUMP_HEALTH_STARTING_INRUSH        = 1,
    PUMP_HEALTH_RUNNING_CONFIRMED      = 2,
    PUMP_HEALTH_FAULT_LATCHED          = 3
};

struct PumpFeedbackConfig {
    uint32_t inrush_blanking_ms;          // Time to mask startup inrush current spikes (typ: 80 ms)
    uint32_t driver_mismatch_timeout_ms;  // Timeout before declaring driver opto failure (typ: 30 ms)
    uint32_t open_load_timeout_ms;        // Timeout before declaring open circuit/broken wire (typ: 150 ms)
    uint32_t overcurrent_debounce_ms;     // Continuous overcurrent duration before stall trip (typ: 50 ms)
    uint32_t flow_confirm_timeout_ms;     // Time window for water to establish flow (typ: 3000 ms)
    uint32_t dry_run_timeout_ms;          // Duration of low current + no flow before dry run trip (typ: 3000 ms)
    
    uint16_t current_leakage_off_max_ma;  // Max allowable leakage current when commanded OFF (typ: 50 mA)
    uint16_t current_open_load_min_ma;    // Min current required to confirm electrical load attached (typ: 150 mA)
    uint16_t current_dry_run_max_ma;      // Max current observed when running dry without water (typ: 1200 mA)
    uint16_t current_nominal_min_ma;      // Lower bound of normal pumping current (typ: 1600 mA)
    uint16_t current_nominal_max_ma;      // Upper bound of normal pumping current (typ: 2600 mA)
    uint16_t current_stall_overcurrent_ma;// Instant/sustained stall current threshold (typ: 3800 mA)
    
    float flow_leakage_max_lpm;           // Max threshold for flow when commanded OFF (typ: 0.2 L/min)
    float flow_confirmed_min_lpm;         // Min threshold for flow confirmation when ON (typ: 0.5 L/min)
    float flow_over_range_max_lpm;        // Max plausible flow before burst pipe / sensor fault (typ: 6.5 L/min)

    static PumpFeedbackConfig defaultDcConfig() {
        PumpFeedbackConfig cfg;
        cfg.inrush_blanking_ms           = FEEDBACK_INRUSH_BLANKING_DEFAULT_MS;
        cfg.driver_mismatch_timeout_ms   = FEEDBACK_DRIVER_MISMATCH_TIMEOUT_DEFAULT_MS;
        cfg.open_load_timeout_ms         = FEEDBACK_OPEN_LOAD_TIMEOUT_DEFAULT_MS;
        cfg.overcurrent_debounce_ms      = FEEDBACK_OVERCURRENT_DEBOUNCE_DEFAULT_MS;
        cfg.flow_confirm_timeout_ms      = FEEDBACK_FLOW_CONFIRM_TIMEOUT_DEFAULT_MS;
        cfg.dry_run_timeout_ms           = FEEDBACK_DRY_RUN_TIMEOUT_DEFAULT_MS;
        cfg.current_leakage_off_max_ma   = FEEDBACK_CURRENT_LEAKAGE_OFF_MAX_MA;
        cfg.current_open_load_min_ma     = FEEDBACK_CURRENT_OPEN_LOAD_MIN_MA;
        cfg.current_dry_run_max_ma       = FEEDBACK_CURRENT_DRY_RUN_MAX_MA;
        cfg.current_nominal_min_ma       = FEEDBACK_CURRENT_NOMINAL_MIN_MA;
        cfg.current_nominal_max_ma       = FEEDBACK_CURRENT_NOMINAL_MAX_MA;
        cfg.current_stall_overcurrent_ma = FEEDBACK_CURRENT_STALL_OVERCURRENT_MA;
        cfg.flow_leakage_max_lpm         = FEEDBACK_FLOW_LEAKAGE_MAX_LPM;
        cfg.flow_confirmed_min_lpm       = FEEDBACK_FLOW_CONFIRMED_MIN_LPM;
        cfg.flow_over_range_max_lpm      = FEEDBACK_FLOW_OVER_RANGE_MAX_LPM;
        return cfg;
    }
};

class PumpFeedbackEvaluator {
public:
    PumpFeedbackEvaluator();
    explicit PumpFeedbackEvaluator(const PumpFeedbackConfig& config);

    void init(const PumpFeedbackConfig& config);
    void resetFault();

    /**
     * @brief Evaluates all 3 tiers of pump feedback deterministically.
     * 
     * @param now_ms Monotonic timestamp in milliseconds.
     * @param commanded_on Desired GPIO control output state (true = ON, false = OFF).
     * @param driver_sense_high Hardware feedback pin from optocoupler / driver gate.
     * @param current_ma Measured load current in milliamperes (from Hall ACS712 / Shunt).
     * @param flow_lpm Measured volumetric flow rate in Liters/minute (from Flow Sensor).
     */
    void update(uint32_t now_ms,
                bool commanded_on,
                bool driver_sense_high,
                uint16_t current_ma,
                float flow_lpm);

    bool isFaultLatched() const { return _latched_fault != FEEDBACK_FAULT_NONE; }
    FeedbackFaultCode getFaultCode() const { return _latched_fault; }
    PumpHealthState getHealthState() const { return _health_state; }

    bool isDriverFeedbackActive() const { return _driver_sense_active; }
    bool isLoadCurrentActive() const { return _load_current_active; }
    bool isFlowConfirmed() const { return _flow_confirmed; }
    bool isPumpConfirmedRunning() const { return _health_state == PUMP_HEALTH_RUNNING_CONFIRMED; }
    bool isSafeOff() const { return _health_state == PUMP_HEALTH_OFF_HEALTHY || _health_state == PUMP_HEALTH_FAULT_LATCHED; }

    uint16_t getLastMeasuredCurrentMa() const { return _last_current_ma; }
    float getLastMeasuredFlowLpm() const { return _last_flow_lpm; }

private:
    PumpFeedbackConfig _config;
    FeedbackFaultCode _latched_fault;
    PumpHealthState _health_state;

    bool _prev_commanded_on;
    uint32_t _command_transition_ms;
    uint32_t _overcurrent_start_ms;
    bool _in_overcurrent_debounce;

    bool _driver_sense_active;
    bool _load_current_active;
    bool _flow_confirmed;

    uint16_t _last_current_ma;
    float _last_flow_lpm;

    void _latchFault(FeedbackFaultCode code);
};

#endif // PUMP_FEEDBACK_EVALUATOR_H
