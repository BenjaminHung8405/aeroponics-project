#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "config.h"

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
    uint32_t std_dev_pulses_x100;          // Standard deviation * 100
    uint16_t repeatability_error_pct_x100; // e.g. 62 = 0.62%
    uint16_t accuracy_error_pct_x100;      // e.g. 45 = 0.45%
    bool is_repeatability_acceptable;      // E_rep <= 1.50% (150 in x100)
    bool is_accuracy_acceptable;           // E_acc <= 2.00% (200 in x100)
};

/**
 * @brief Raw measurement trial point for a specific operating flow rate
 */
struct CalibrationTrialPoint {
    uint16_t flow_target_lpm_x100;                     // Target flow in L/min * 100
    uint32_t ref_volume_ml;                            // Reference volume in mL (e.g. 1000, 2000)
    uint8_t trial_count;                               // Number of trials (>= 3, <= 10)
    uint32_t raw_pulses[MAX_CALIBRATION_TRIALS];       // Recorded pulse count for each trial
    uint32_t ref_mass_g_x10;                           // Gravimetric mass in g * 10 (e.g. 9971 = 997.1g)
    int16_t fluid_temp_c_x10;                          // Water temperature in C * 10 (e.g. 252 = 25.2 C)
    uint16_t operating_pressure_bar_x100;              // Operating pressure in bar * 100 (e.g. 350 = 3.50 bar)
    uint32_t calculated_k_factor;                      // Calculated K-factor (pulses/L)
    uint32_t mean_pulses;                              // Mean pulse count
    uint32_t std_dev_pulses_x100;                      // Standard deviation * 100
    uint16_t repeatability_error_pct_x100;             // E_rep in % * 100
    uint16_t accuracy_error_pct_x100;                  // E_acc in % * 100
    bool is_point_valid;                               // Validated against acceptance thresholds
};

/**
 * @brief Complete multi-point calibration trial dataset across the operating range
 */
struct CalibrationDataset {
    char sensor_serial[SENSOR_SERIAL_MAX_LEN];
    uint8_t node_id;
    uint32_t calibrated_at_timestamp;                  // Epoch / Unix timestamp
    char operator_id[OPERATOR_ID_MAX_LEN];
    uint8_t num_points;                                // Number of operating points (e.g. 5)
    CalibrationTrialPoint points[MAX_CALIBRATION_POINTS];
    uint32_t overall_nominal_k_factor;                 // Weighted mean K-factor
    uint16_t max_repeatability_pct_x100;               // Maximum repeatability error across all points
    uint16_t max_accuracy_error_pct_x100;              // Maximum accuracy error across all points
    uint32_t linearity_r2_x10000;                      // Linearity R^2 * 10000 (e.g. 9985 = 0.9985)
    uint8_t zero_leak_pulses_60s;                      // Pulses recorded during 60s zero-flow test
    bool is_approved;                                  // True if passed all thresholds
    char audit_hash[AUDIT_HASH_HEX_LEN];               // SHA-256 audit hash string
};

enum class FlowEvaluationStatus : uint8_t {
    FLOW_NORMAL = 0,
    FLOW_ZERO_OR_CUTOFF = 1,
    FLOW_OVER_RANGE = 2,
    FLOW_INVALID_PARAMETERS = 3,
    FLOW_STALE_OR_DISCONNECTED = 4
};

enum class CalibrationRejectionReason : uint8_t {
    REJECT_NONE = 0,
    REJECT_INSUFFICIENT_TRIALS = 1,      // Less than 3 trials per operating point
    REJECT_EXCESSIVE_REPEATABILITY = 2,  // Repeatability error E_rep > 1.50%
    REJECT_EXCESSIVE_ACCURACY = 3,       // Accuracy error E_acc > 2.00%
    REJECT_POOR_LINEARITY = 4,           // Linearity R^2 < 0.9900
    REJECT_ZERO_LEAK_FAIL = 5,           // Zero-flow leak pulses > 1 in 60s
    REJECT_NON_MONOTONIC_POINTS = 6,     // Calibration points not strictly increasing
    REJECT_INVALID_PARAMETERS = 7,       // Invalid node ID, zero K-factor, or null buffer
    REJECT_VERSION_NOT_INCREMENTED = 8,  // New version <= active version (immutable history)
    REJECT_CRC_OR_HASH_MISMATCH = 9,     // Checksum CRC32 or audit hash verification failed
    REJECT_UNAUTHENTICATED = 10          // Signature / authentication mismatch
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
     * @brief Outlier detection on trial measurements using Grubbs' test (alpha = 0.05)
     * 
     * @param trials Array of trial measurements
     * @param count Number of trials (3..10)
     * @param out_outlier_idx Output index of detected outlier (if found)
     * @param out_g_value Output calculated G statistic
     * @param out_is_outlier True if an outlier was detected with 95% confidence
     * @return true if test executed successfully, false if invalid input
     */
    static bool performGrubbsOutlierTest(const uint32_t* trials, size_t count,
                                        size_t& out_outlier_idx, double& out_g_value,
                                        bool& out_is_outlier);

    /**
     * @brief Calculate coefficient of determination (R^2) for flow vs frequency linearity
     * 
     * @param points Array of calibration points
     * @param num_points Number of points (>= 2)
     * @param out_r2_x10000 Output R^2 * 10000 (e.g. 9985 for R^2 = 0.9985)
     * @return true if calculation succeeded, false otherwise
     */
    static bool calculateLinearityR2(const CalibrationPoint* points, uint8_t num_points,
                                    uint32_t& out_r2_x10000);

    /**
     * @brief Validate a complete calibration dataset against all quantitative quality gates
     * 
     * @param dataset The calibration trial dataset to validate
     * @return CalibrationRejectionReason (REJECT_NONE if acceptable)
     */
    static CalibrationRejectionReason validateDataset(const CalibrationDataset& dataset);

    /**
     * @brief Generate and sign a SensorCalibrationProfile from an approved CalibrationDataset
     * 
     * @param dataset Input validated dataset
     * @param version Profile version number
     * @param out_profile Output signed profile
     * @param out_reason Output rejection reason if failed
     * @return true if profile generated successfully, false if rejected
     */
    static bool generateProfileFromDataset(const CalibrationDataset& dataset, uint32_t version,
                                          SensorCalibrationProfile& out_profile,
                                          CalibrationRejectionReason& out_reason);

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

    /**
     * @brief Calculate SHA-256 audit hash string for a calibration profile
     * 
     * @param profile The calibration profile to hash
     * @param out_hash_hex Output buffer for 64-character hex string + null terminator
     * @return true on success, false on error
     */
    static bool calculateAuditSha256(const SensorCalibrationProfile& profile,
                                    char out_hash_hex[AUDIT_HASH_HEX_LEN]);

    /**
     * @brief Get human-readable description of a calibration rejection reason
     */
    static const char* getRejectionReasonString(CalibrationRejectionReason reason);

private:
    SensorCalibrationProfile _profile;
    bool _profile_valid;
};

/**
 * @brief Multi-Node Versioned Calibration Registry & Audit Store
 * 
 * Manages active and historical calibration configurations for 4 MEGA8 nodes (node_id 1..4).
 * Enforces versioned immutability (cannot overwrite active profile without incremented version),
 * CRC32/SHA256 checksum verification, and node isolation.
 */
class FlowCalibrationRegistry {
public:
    FlowCalibrationRegistry();

    /**
     * @brief Reset registry to empty unconfigured state
     */
    void reset();

    /**
     * @brief Register or update a calibration profile for a node
     * 
     * Enforces:
     * - Node ID in valid range (1..4)
     * - CRC32 checksum match
     * - SHA-256 audit hash match (if expected_audit_hash provided)
     * - Strict version increment (version > active_version) when updating an existing active profile
     * - Automatic archival of previous active profile to version history
     * 
     * @param profile The new calibration profile to register
     * @param expected_audit_hash Optional expected SHA-256 hex string for authentication
     * @return CalibrationRejectionReason (REJECT_NONE on success)
     */
    CalibrationRejectionReason registerProfile(const SensorCalibrationProfile& profile,
                                               const char* expected_audit_hash = nullptr);

    /**
     * @brief Convenience method to validate a dataset, generate profile, and register in one step
     * 
     * @param dataset Input calibration trial dataset
     * @param version Target version number
     * @return CalibrationRejectionReason
     */
    CalibrationRejectionReason registerFromDataset(const CalibrationDataset& dataset, uint32_t version);

    /**
     * @brief Check if a node has a valid active calibration profile loaded
     */
    bool isNodeCalibrated(uint8_t node_id) const;

    /**
     * @brief Get pointer to active calibration profile for a node (returns nullptr if uncalibrated)
     */
    const SensorCalibrationProfile* getActiveProfile(uint8_t node_id) const;

    /**
     * @brief Get active SHA-256 audit hash for a node (returns empty string if uncalibrated)
     */
    const char* getActiveAuditHash(uint8_t node_id) const;

    /**
     * @brief Get pointer to FlowCalibrationEngine for a node (returns nullptr if node_id invalid)
     */
    FlowCalibrationEngine* getEngine(uint8_t node_id);
    const FlowCalibrationEngine* getEngine(uint8_t node_id) const;

    /**
     * @brief Get number of archived historical profiles for a node
     */
    uint8_t getHistoryCount(uint8_t node_id) const;

    /**
     * @brief Get a historical version profile for a node
     * 
     * @param node_id Target node ID (1..4)
     * @param history_index 0 = most recent previous version, 1 = older, etc.
     * @return const SensorCalibrationProfile* or nullptr if index out of range
     */
    const SensorCalibrationProfile* getHistoricalProfile(uint8_t node_id, uint8_t history_index) const;

    /**
     * @brief Rollback to a historical profile by creating a new incremented version with the target parameters
     * 
     * @param node_id Target node ID (1..4)
     * @param target_version Historical version number to restore parameters from
     * @param new_version New version number for the restored profile (must be > active version)
     * @return true if rollback succeeded, false otherwise
     */
    bool rollbackToHistoricalVersion(uint8_t node_id, uint32_t target_version, uint32_t new_version);

    /**
     * @brief Verify memory integrity of active profile on a node by recomputing CRC32 and SHA256
     */
    bool verifyNodeIntegrity(uint8_t node_id) const;

private:
    SensorCalibrationProfile _active_profiles[MAX_SUPPORTED_NODES];
    char _active_audit_hashes[MAX_SUPPORTED_NODES][AUDIT_HASH_HEX_LEN];
    bool _node_configured[MAX_SUPPORTED_NODES];
    SensorCalibrationProfile _history_profiles[MAX_SUPPORTED_NODES][MAX_CALIBRATION_HISTORY_PER_NODE];
    uint8_t _history_count[MAX_SUPPORTED_NODES];
    FlowCalibrationEngine _engines[MAX_SUPPORTED_NODES];
};

