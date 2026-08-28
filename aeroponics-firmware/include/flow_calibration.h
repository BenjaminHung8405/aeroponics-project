#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Flow Calibration & Measurement Engine for Aeroponics Nodes
 * 
 * Implements deterministic, zero-allocation pulse-to-flow conversion,
 * multi-point piecewise interpolation, density/temperature compensation,
 * and statistical repeatability evaluation for measurement traceability.
 * 
 * Aligned with SPEC-FLOW-CAL-001 (docs/RF_FLOW_POC_CALIBRATION.md).
 */

#define MAX_CALIBRATION_POINTS 5
#define SENSOR_SERIAL_MAX_LEN 16

struct CalibrationPoint {
    uint16_t flow_lpm_x100;     // Flow rate in L/min * 100 (e.g. 250 = 2.50 L/min)
    uint16_t pulse_freq_hz_x10; // Pulse frequency in Hz * 10 (e.g. 1850 = 185.0 Hz)
    uint32_t pulses_per_litre;  // K-factor at this point (pulses/L)
};

struct SensorCalibrationProfile {
    uint32_t calibration_id;
    uint32_t version;
    uint8_t node_id;
    char sensor_serial[SENSOR_SERIAL_MAX_LEN];
    uint32_t nominal_pulses_per_litre; // Default K-factor
    uint16_t low_flow_cutoff_lpm_x100; // e.g. 15 = 0.15 L/min
    uint16_t max_flow_limit_lpm_x100;  // e.g. 600 = 6.00 L/min
    uint8_t num_calibration_points;
    CalibrationPoint points[MAX_CALIBRATION_POINTS];
    uint32_t checksum_crc32;
};

struct CalibrationStatistics {
    uint32_t mean_pulses;
    uint32_t mean_volume_ml;
    uint32_t std_dev_pulses_x100; // Standard deviation * 100
    uint16_t repeatability_error_pct_x100; // e.g. 62 = 0.62%
    uint16_t accuracy_error_pct_x100;      // e.g. 45 = 0.45%
    bool is_repeatability_acceptable;      // E_rep <= 1.50% (150 in x100)
    bool is_accuracy_acceptable;           // E_acc <= 2.00% (200 in x100)
};

enum class FlowEvaluationStatus : uint8_t {
    FLOW_NORMAL = 0,
    FLOW_ZERO_OR_CUTOFF = 1,
    FLOW_OVER_RANGE = 2,
    FLOW_INVALID_PARAMETERS = 3,
    FLOW_STALE_OR_DISCONNECTED = 4
};

class FlowCalibrationEngine {
public:
    FlowCalibrationEngine();

    /**
     * @brief Load and validate a calibration profile
     * @return true if profile is valid and loaded, false otherwise
     */
    bool loadProfile(const SensorCalibrationProfile& profile);

    /**
     * @brief Get active calibration profile
     */
    const SensorCalibrationProfile& getProfile() const { return _profile; }

    /**
     * @brief Check if a valid profile is loaded
     */
    bool hasValidProfile() const { return _profile_valid; }

    /**
     * @brief Calculate instantaneous flow rate from pulse delta and time delta
     * 
     * @param delta_pulses Number of pulses in sample window
     * @param delta_time_ms Sample window duration in milliseconds
     * @param out_flow_lpm_x100 Output flow rate in L/min * 100
     * @return FlowEvaluationStatus
     */
    FlowEvaluationStatus calculateFlowRate(uint32_t delta_pulses, uint32_t delta_time_ms,
                                          uint16_t& out_flow_lpm_x100) const;

    /**
     * @brief Calculate total delivered volume in milliliters from pulse count
     * 
     * @param total_pulses Cumulative pulse count
     * @return uint32_t Volume in mL (e.g. 1500 = 1.500 Litres)
     */
    uint32_t calculateDeliveredVolumeMl(uint32_t total_pulses) const;

    /**
     * @brief Calculate K-factor via piecewise linear interpolation based on frequency
     * 
     * @param freq_hz_x10 Pulse frequency in Hz * 10
     * @return uint32_t Effective K-factor (pulses/L)
     */
    uint32_t interpolateKFactor(uint16_t freq_hz_x10) const;

    /**
     * @brief Calculate statistical metrics for calibration trials (ISO 17025 verification)
     * 
     * @param pulse_counts Array of pulse count measurements
     * @param trial_count Number of trials (must be >= 3)
     * @param ref_volume_ml Reference volume in mL (e.g. 1000 or 2000)
     * @param out_stats Output statistics struct
     * @return true if calculation succeeded, false if invalid input
     */
    static bool evaluateCalibrationTrials(const uint32_t* pulse_counts, size_t trial_count,
                                         uint32_t ref_volume_ml,
                                         CalibrationStatistics& out_stats);

    /**
     * @brief Calculate water density at given temperature (Tanaka equation approximation)
     * 
     * @param temp_c_x10 Temperature in Celsius * 10 (e.g. 250 = 25.0 C)
     * @return uint32_t Density in g/m3 (e.g. 997047 for 25.0 C)
     */
    static uint32_t calculateWaterDensity(int16_t temp_c_x10);

    /**
     * @brief Calculate CRC32 checksum for a calibration profile
     */
    static uint32_t calculateProfileCrc32(const SensorCalibrationProfile& profile);

private:
    SensorCalibrationProfile _profile;
    bool _profile_valid;
};
