#include "flow_calibration.h"
#include "core/hmac_sha256.h"
#include <cmath>
#include <cstring>
#include <cstdio>

static uint32_t updateCrc32(uint32_t crc, const uint8_t* data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

FlowCalibrationEngine::FlowCalibrationEngine() : _profile{}, _profile_valid(false) {
    _profile.nominal_pulses_per_litre = 4450; // Standard for OF06ZAT (~4450 pulses/L)
    _profile.low_flow_cutoff_lpm_x100 = 15;   // 0.15 L/min
    _profile.max_flow_limit_lpm_x100 = 600;   // 6.00 L/min
    _profile.num_calibration_points = 0;
}

bool FlowCalibrationEngine::loadProfile(const SensorCalibrationProfile& profile) {
    if (profile.nominal_pulses_per_litre == 0 || profile.nominal_pulses_per_litre > 50000) {
        return false;
    }
    if (profile.max_flow_limit_lpm_x100 <= profile.low_flow_cutoff_lpm_x100) {
        return false;
    }
    if (profile.num_calibration_points > MAX_CALIBRATION_POINTS) {
        return false;
    }

    // Validate calibration points ordering if points exist
    for (uint8_t i = 0; i < profile.num_calibration_points; ++i) {
        if (profile.points[i].pulses_per_litre == 0 || profile.points[i].pulses_per_litre > 50000) {
            return false;
        }
        if (i > 0) {
            if (profile.points[i].pulse_freq_hz_x10 <= profile.points[i - 1].pulse_freq_hz_x10) {
                return false; // Must be strictly monotonically increasing frequency
            }
            if (profile.points[i].flow_lpm_x100 <= profile.points[i - 1].flow_lpm_x100) {
                return false; // Must be strictly monotonically increasing flow
            }
        }
    }

    uint32_t expected_crc = calculateProfileCrc32(profile);
    if (profile.checksum_crc32 != 0 && profile.checksum_crc32 != expected_crc) {
        return false;
    }

    _profile = profile;
    _profile.checksum_crc32 = expected_crc;
    _profile_valid = true;
    return true;
}

uint32_t FlowCalibrationEngine::interpolateKFactor(uint16_t freq_hz_x10) const {
    if (!_profile_valid || _profile.num_calibration_points == 0) {
        return _profile.nominal_pulses_per_litre > 0 ? _profile.nominal_pulses_per_litre : 4450;
    }

    if (_profile.num_calibration_points == 1 || freq_hz_x10 <= _profile.points[0].pulse_freq_hz_x10) {
        return _profile.points[0].pulses_per_litre;
    }

    uint8_t last_idx = _profile.num_calibration_points - 1;
    if (freq_hz_x10 >= _profile.points[last_idx].pulse_freq_hz_x10) {
        return _profile.points[last_idx].pulses_per_litre;
    }

    // Binary / linear search for bracket
    for (uint8_t i = 0; i < last_idx; ++i) {
        if (freq_hz_x10 >= _profile.points[i].pulse_freq_hz_x10 &&
            freq_hz_x10 <= _profile.points[i + 1].pulse_freq_hz_x10) {
            int32_t f0 = _profile.points[i].pulse_freq_hz_x10;
            int32_t f1 = _profile.points[i + 1].pulse_freq_hz_x10;
            int32_t k0 = _profile.points[i].pulses_per_litre;
            int32_t k1 = _profile.points[i + 1].pulses_per_litre;

            if (f1 == f0) {
                return k0;
            }

            int64_t interpolated = k0 + ((int64_t)(k1 - k0) * (freq_hz_x10 - f0)) / (f1 - f0);
            return static_cast<uint32_t>(interpolated);
        }
    }

    return _profile.nominal_pulses_per_litre;
}

FlowEvaluationStatus FlowCalibrationEngine::calculateFlowRate(uint32_t delta_pulses,
                                                            uint32_t delta_time_ms,
                                                            uint16_t& out_flow_lpm_x100) const {
    if (delta_time_ms == 0) {
        out_flow_lpm_x100 = 0;
        return FlowEvaluationStatus::FLOW_INVALID_PARAMETERS;
    }

    if (delta_pulses == 0) {
        out_flow_lpm_x100 = 0;
        return FlowEvaluationStatus::FLOW_ZERO_OR_CUTOFF;
    }

    // Calculate frequency in Hz * 10 = (pulses * 1000 * 10) / ms
    uint64_t freq_calc = ((uint64_t)delta_pulses * 10000ULL) / delta_time_ms;
    uint16_t freq_hz_x10 = (freq_calc > 65535) ? 65535 : static_cast<uint16_t>(freq_calc);

    uint32_t k_factor = interpolateKFactor(freq_hz_x10);
    if (k_factor == 0) {
        k_factor = 4450;
    }

    // Flow in L/min * 100 = (pulses * 60 * 1000 * 100) / (time_ms * k_factor)
    uint64_t flow_calc = ((uint64_t)delta_pulses * 6000000ULL) / ((uint64_t)delta_time_ms * k_factor);
    uint16_t flow_lpm_x100 = (flow_calc > 65535) ? 65535 : static_cast<uint16_t>(flow_calc);

    out_flow_lpm_x100 = flow_lpm_x100;

    if (flow_lpm_x100 < _profile.low_flow_cutoff_lpm_x100) {
        out_flow_lpm_x100 = 0;
        return FlowEvaluationStatus::FLOW_ZERO_OR_CUTOFF;
    }

    if (flow_lpm_x100 > _profile.max_flow_limit_lpm_x100) {
        return FlowEvaluationStatus::FLOW_OVER_RANGE;
    }

    return FlowEvaluationStatus::FLOW_NORMAL;
}

uint32_t FlowCalibrationEngine::calculateDeliveredVolumeMl(uint32_t total_pulses) const {
    uint32_t k_factor = _profile.nominal_pulses_per_litre > 0 ? _profile.nominal_pulses_per_litre : 4450;
    return static_cast<uint32_t>(((uint64_t)total_pulses * 1000ULL) / k_factor);
}

bool FlowCalibrationEngine::evaluateCalibrationTrials(const uint32_t* pulse_counts,
                                                    size_t trial_count,
                                                    uint32_t ref_volume_ml,
                                                    CalibrationStatistics& out_stats) {
    if (pulse_counts == nullptr || trial_count < MIN_CALIBRATION_TRIALS || ref_volume_ml == 0) {
        return false;
    }

    uint64_t sum_pulses = 0;
    for (size_t i = 0; i < trial_count; ++i) {
        sum_pulses += pulse_counts[i];
    }
    uint32_t mean_pulses = static_cast<uint32_t>(sum_pulses / trial_count);
    out_stats.mean_pulses = mean_pulses;

    // Standard deviation: s = sqrt(sum((x - mean)^2) / (N - 1))
    double variance_sum = 0.0;
    for (size_t i = 0; i < trial_count; ++i) {
        double diff = static_cast<double>(pulse_counts[i]) - static_cast<double>(mean_pulses);
        variance_sum += (diff * diff);
    }
    double std_dev = std::sqrt(variance_sum / (trial_count - 1));
    out_stats.std_dev_pulses_x100 = static_cast<uint32_t>(std_dev * 100.0 + 0.5);

    // Repeatability relative error % * 100: E_rep = (2 * s / mean) * 100%
    if (mean_pulses > 0) {
        double e_rep = (2.0 * std_dev / static_cast<double>(mean_pulses)) * 10000.0;
        out_stats.repeatability_error_pct_x100 = static_cast<uint16_t>(e_rep + 0.5);
    } else {
        out_stats.repeatability_error_pct_x100 = 9999;
    }

    // Calculated mean volume in mL (using nominal reference K = 4450 p/L)
    uint32_t nominal_k = 4450;
    uint32_t calculated_vol_ml = static_cast<uint32_t>(((uint64_t)mean_pulses * 1000ULL) / nominal_k);
    out_stats.mean_volume_ml = calculated_vol_ml;

    int32_t vol_diff = static_cast<int32_t>(calculated_vol_ml) - static_cast<int32_t>(ref_volume_ml);
    if (vol_diff < 0) vol_diff = -vol_diff;
    double e_acc = (static_cast<double>(vol_diff) / static_cast<double>(ref_volume_ml)) * 10000.0;
    out_stats.accuracy_error_pct_x100 = static_cast<uint16_t>(e_acc + 0.5);

    // Acceptance thresholds: Repeatability <= 1.50% (150 in x100), Accuracy <= 2.00% (200 in x100)
    out_stats.is_repeatability_acceptable = (out_stats.repeatability_error_pct_x100 <= MAX_ACCEPTABLE_REPEATABILITY_PCT_X100);
    out_stats.is_accuracy_acceptable = (out_stats.accuracy_error_pct_x100 <= MAX_ACCEPTABLE_ACCURACY_ERROR_PCT_X100);

    return true;
}

bool FlowCalibrationEngine::performGrubbsOutlierTest(const uint32_t* trials, size_t count,
                                                   size_t& out_outlier_idx, double& out_g_value,
                                                   bool& out_is_outlier) {
    if (trials == nullptr || count < 3 || count > MAX_CALIBRATION_TRIALS) {
        out_is_outlier = false;
        return false;
    }

    // Critical values for Grubbs' test at alpha = 0.05 (two-sided)
    static const double g_critical_table[8] = {
        1.153, // N = 3
        1.463, // N = 4
        1.672, // N = 5
        1.822, // N = 6
        1.938, // N = 7
        2.032, // N = 8
        2.110, // N = 9
        2.176  // N = 10
    };

    double g_crit = g_critical_table[count - 3];

    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        sum += static_cast<double>(trials[i]);
    }
    double mean = sum / count;

    double variance_sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        double diff = static_cast<double>(trials[i]) - mean;
        variance_sum += (diff * diff);
    }
    double s = std::sqrt(variance_sum / (count - 1));

    if (s < 1e-6) {
        // Zero variance, no outlier
        out_outlier_idx = 0;
        out_g_value = 0.0;
        out_is_outlier = false;
        return true;
    }

    // Find maximum deviation
    double max_dev = 0.0;
    size_t max_idx = 0;
    for (size_t i = 0; i < count; ++i) {
        double dev = std::fabs(static_cast<double>(trials[i]) - mean);
        if (dev > max_dev) {
            max_dev = dev;
            max_idx = i;
        }
    }

    double g_calc = max_dev / s;
    out_outlier_idx = max_idx;
    out_g_value = g_calc;
    out_is_outlier = (g_calc > g_crit);

    return true;
}

bool FlowCalibrationEngine::calculateLinearityR2(const CalibrationPoint* points, uint8_t num_points,
                                                uint32_t& out_r2_x10000) {
    if (points == nullptr || num_points < 2 || num_points > MAX_CALIBRATION_POINTS) {
        out_r2_x10000 = 0;
        return false;
    }

    double sum_x = 0.0;
    double sum_y = 0.0;
    double sum_xx = 0.0;
    double sum_yy = 0.0;
    double sum_xy = 0.0;
    double n = static_cast<double>(num_points);

    for (uint8_t i = 0; i < num_points; ++i) {
        double x = static_cast<double>(points[i].flow_lpm_x100);
        double y = static_cast<double>(points[i].pulse_freq_hz_x10);
        sum_x += x;
        sum_y += y;
        sum_xx += (x * x);
        sum_yy += (y * y);
        sum_xy += (x * y);
    }

    double numerator = (n * sum_xy) - (sum_x * sum_y);
    double denominator = ((n * sum_xx) - (sum_x * sum_x)) * ((n * sum_yy) - (sum_y * sum_y));

    if (denominator <= 0.0) {
        out_r2_x10000 = 0;
        return false;
    }

    double r2 = (numerator * numerator) / denominator;
    if (r2 > 1.0) r2 = 1.0;
    if (r2 < 0.0) r2 = 0.0;

    out_r2_x10000 = static_cast<uint32_t>(r2 * 10000.0 + 0.5);
    return true;
}

CalibrationRejectionReason FlowCalibrationEngine::validateDataset(const CalibrationDataset& dataset) {
    if (!isProductionNodeId(dataset.node_id)) {
        return CalibrationRejectionReason::REJECT_INVALID_PARAMETERS;
    }
    if (dataset.num_points < 2 || dataset.num_points > MAX_CALIBRATION_POINTS) {
        return CalibrationRejectionReason::REJECT_INVALID_PARAMETERS;
    }
    if (dataset.sensor_serial[0] == '\0') {
        return CalibrationRejectionReason::REJECT_INVALID_PARAMETERS;
    }

    // Zero-flow leak check (<= 1 pulse in 60s)
    if (dataset.zero_leak_pulses_60s > MAX_ACCEPTABLE_ZERO_LEAK_PULSES_60S) {
        return CalibrationRejectionReason::REJECT_ZERO_LEAK_FAIL;
    }

    CalibrationPoint cal_points[MAX_CALIBRATION_POINTS];

    for (uint8_t i = 0; i < dataset.num_points; ++i) {
        const CalibrationTrialPoint& tp = dataset.points[i];

        if (tp.trial_count < MIN_CALIBRATION_TRIALS) {
            return CalibrationRejectionReason::REJECT_INSUFFICIENT_TRIALS;
        }

        if (i > 0) {
            if (tp.flow_target_lpm_x100 <= dataset.points[i - 1].flow_target_lpm_x100) {
                return CalibrationRejectionReason::REJECT_NON_MONOTONIC_POINTS;
            }
        }

        // Statistical evaluation of trials
        CalibrationStatistics stats{};
        if (!evaluateCalibrationTrials(tp.raw_pulses, tp.trial_count, tp.ref_volume_ml, stats)) {
            return CalibrationRejectionReason::REJECT_INVALID_PARAMETERS;
        }

        if (!stats.is_repeatability_acceptable ||
            tp.repeatability_error_pct_x100 > MAX_ACCEPTABLE_REPEATABILITY_PCT_X100) {
            return CalibrationRejectionReason::REJECT_EXCESSIVE_REPEATABILITY;
        }

        if (!stats.is_accuracy_acceptable ||
            tp.accuracy_error_pct_x100 > MAX_ACCEPTABLE_ACCURACY_ERROR_PCT_X100) {
            return CalibrationRejectionReason::REJECT_EXCESSIVE_ACCURACY;
        }

        if (tp.calculated_k_factor < 1000 || tp.calculated_k_factor > 20000) {
            return CalibrationRejectionReason::REJECT_INVALID_PARAMETERS;
        }

        // Populate calibration point for linearity check
        cal_points[i].flow_lpm_x100 = tp.flow_target_lpm_x100;
        // Frequency in Hz * 10 = (flow_lpm * K * 10) / 60
        uint64_t freq_calc = ((uint64_t)tp.flow_target_lpm_x100 * tp.calculated_k_factor) / 600ULL;
        cal_points[i].pulse_freq_hz_x10 = (freq_calc > 65535) ? 65535 : static_cast<uint16_t>(freq_calc);
        cal_points[i].pulses_per_litre = tp.calculated_k_factor;
    }

    // Linearity check
    uint32_t r2_x10000 = 0;
    if (!calculateLinearityR2(cal_points, dataset.num_points, r2_x10000)) {
        return CalibrationRejectionReason::REJECT_INVALID_PARAMETERS;
    }

    if (r2_x10000 < MIN_ACCEPTABLE_LINEARITY_R2_X10000) {
        return CalibrationRejectionReason::REJECT_POOR_LINEARITY;
    }

    return CalibrationRejectionReason::REJECT_NONE;
}

bool FlowCalibrationEngine::generateProfileFromDataset(const CalibrationDataset& dataset, uint32_t version,
                                                      SensorCalibrationProfile& out_profile,
                                                      CalibrationRejectionReason& out_reason) {
    if (version == 0) {
        out_reason = CalibrationRejectionReason::REJECT_INVALID_PARAMETERS;
        return false;
    }

    CalibrationRejectionReason validation = validateDataset(dataset);
    if (validation != CalibrationRejectionReason::REJECT_NONE) {
        out_reason = validation;
        return false;
    }

    std::memset(&out_profile, 0, sizeof(SensorCalibrationProfile));
    out_profile.calibration_id = dataset.calibrated_at_timestamp > 0 ? dataset.calibrated_at_timestamp : 1;
    out_profile.version = version;
    out_profile.node_id = dataset.node_id;
    std::strncpy(out_profile.sensor_serial, dataset.sensor_serial, SENSOR_SERIAL_MAX_LEN - 1);
    out_profile.nominal_pulses_per_litre = dataset.overall_nominal_k_factor > 0 ? dataset.overall_nominal_k_factor : 4450;
    out_profile.low_flow_cutoff_lpm_x100 = 15; // 0.15 L/min
    out_profile.max_flow_limit_lpm_x100 = 600; // 6.00 L/min
    out_profile.num_calibration_points = dataset.num_points;

    for (uint8_t i = 0; i < dataset.num_points; ++i) {
        const CalibrationTrialPoint& tp = dataset.points[i];
        out_profile.points[i].flow_lpm_x100 = tp.flow_target_lpm_x100;
        uint64_t freq_calc = ((uint64_t)tp.flow_target_lpm_x100 * tp.calculated_k_factor) / 600ULL;
        out_profile.points[i].pulse_freq_hz_x10 = (freq_calc > 65535) ? 65535 : static_cast<uint16_t>(freq_calc);
        out_profile.points[i].pulses_per_litre = tp.calculated_k_factor;
    }

    out_profile.checksum_crc32 = calculateProfileCrc32(out_profile);
    out_reason = CalibrationRejectionReason::REJECT_NONE;
    return true;
}

uint32_t FlowCalibrationEngine::calculateWaterDensity(int16_t temp_c_x10) {
    double temp = static_cast<double>(temp_c_x10) / 10.0;
    if (temp < 0.0) temp = 0.0;
    if (temp > 60.0) temp = 60.0;

    // Tanaka equation simplification for water density in g/m3
    double rho_g_cm3 = 1.0 - ((temp - 3.98) * (temp - 3.98) / 508929.2) * ((temp + 288.9414) / (temp + 68.12963));
    return static_cast<uint32_t>(rho_g_cm3 * 1000000.0 + 0.5);
}

uint32_t FlowCalibrationEngine::calculateProfileCrc32(const SensorCalibrationProfile& profile) {
    size_t data_len = offsetof(SensorCalibrationProfile, checksum_crc32);
    return updateCrc32(0, reinterpret_cast<const uint8_t*>(&profile), data_len);
}

bool FlowCalibrationEngine::calculateAuditSha256(const SensorCalibrationProfile& profile,
                                                char out_hash_hex[AUDIT_HASH_HEX_LEN]) {
    if (out_hash_hex == nullptr) return false;

    size_t data_len = offsetof(SensorCalibrationProfile, checksum_crc32);
    Sha256 sha;
    sha.init();
    sha.update(reinterpret_cast<const uint8_t*>(&profile), data_len);
    uint8_t hash_bytes[SHA256_HASH_SIZE];
    sha.final(hash_bytes);

    for (size_t i = 0; i < SHA256_HASH_SIZE; ++i) {
        std::snprintf(&out_hash_hex[i * 2], 3, "%02x", hash_bytes[i]);
    }
    out_hash_hex[64] = '\0';
    return true;
}

const char* FlowCalibrationEngine::getRejectionReasonString(CalibrationRejectionReason reason) {
    switch (reason) {
        case CalibrationRejectionReason::REJECT_NONE:
            return "SUCCESS / PASS";
        case CalibrationRejectionReason::REJECT_INSUFFICIENT_TRIALS:
            return "REJECT: Insufficient trials (< 3 trials per point)";
        case CalibrationRejectionReason::REJECT_EXCESSIVE_REPEATABILITY:
            return "REJECT: Repeatability error E_rep exceeds 1.50%";
        case CalibrationRejectionReason::REJECT_EXCESSIVE_ACCURACY:
            return "REJECT: Accuracy error E_acc exceeds 2.00%";
        case CalibrationRejectionReason::REJECT_POOR_LINEARITY:
            return "REJECT: Linearity R^2 below 0.9900 threshold";
        case CalibrationRejectionReason::REJECT_ZERO_LEAK_FAIL:
            return "REJECT: Zero-flow leak pulses exceed 1 pulse / 60s";
        case CalibrationRejectionReason::REJECT_NON_MONOTONIC_POINTS:
            return "REJECT: Flow calibration points not strictly increasing";
        case CalibrationRejectionReason::REJECT_INVALID_PARAMETERS:
            return "REJECT: Invalid parameters, node ID or buffer";
        case CalibrationRejectionReason::REJECT_VERSION_NOT_INCREMENTED:
            return "REJECT: Version must be strictly greater than active profile";
        case CalibrationRejectionReason::REJECT_CRC_OR_HASH_MISMATCH:
            return "REJECT: Checksum CRC32 or audit hash mismatch";
        case CalibrationRejectionReason::REJECT_UNAUTHENTICATED:
            return "REJECT: Authentication / signature mismatch";
        default:
            return "REJECT: Unknown error";
    }
}

// ----------------------------------------------------------------------------
// FlowCalibrationRegistry Implementation
// ----------------------------------------------------------------------------

FlowCalibrationRegistry::FlowCalibrationRegistry() {
    reset();
}

void FlowCalibrationRegistry::reset() {
    for (size_t i = 0; i < MAX_SUPPORTED_NODES; ++i) {
        std::memset(&_active_profiles[i], 0, sizeof(SensorCalibrationProfile));
        std::memset(_active_audit_hashes[i], 0, AUDIT_HASH_HEX_LEN);
        _node_configured[i] = false;
        _history_count[i] = 0;
        for (size_t j = 0; j < MAX_CALIBRATION_HISTORY_PER_NODE; ++j) {
            std::memset(&_history_profiles[i][j], 0, sizeof(SensorCalibrationProfile));
        }
        _engines[i] = FlowCalibrationEngine();
    }
}

CalibrationRejectionReason FlowCalibrationRegistry::registerProfile(const SensorCalibrationProfile& profile,
                                                                   const char* expected_audit_hash) {
    if (!isProductionNodeId(profile.node_id)) {
        return CalibrationRejectionReason::REJECT_INVALID_PARAMETERS;
    }

    uint8_t idx = profile.node_id - 1;

    // Validate CRC32
    uint32_t expected_crc = FlowCalibrationEngine::calculateProfileCrc32(profile);
    if (profile.checksum_crc32 != expected_crc) {
        return CalibrationRejectionReason::REJECT_CRC_OR_HASH_MISMATCH;
    }

    // Validate SHA-256 audit hash if provided
    char calculated_hash[AUDIT_HASH_HEX_LEN];
    FlowCalibrationEngine::calculateAuditSha256(profile, calculated_hash);
    if (expected_audit_hash != nullptr && expected_audit_hash[0] != '\0') {
        if (std::strcmp(calculated_hash, expected_audit_hash) != 0) {
            return CalibrationRejectionReason::REJECT_UNAUTHENTICATED;
        }
    }

    // Validate version progression (immutable history — no active overwrite without version bump)
    if (_node_configured[idx]) {
        if (profile.version <= _active_profiles[idx].version) {
            return CalibrationRejectionReason::REJECT_VERSION_NOT_INCREMENTED;
        }

        // Archive current active profile to history
        if (_history_count[idx] < MAX_CALIBRATION_HISTORY_PER_NODE) {
            _history_profiles[idx][_history_count[idx]] = _active_profiles[idx];
            _history_count[idx]++;
        } else {
            // Shift older history
            for (size_t j = 0; j < MAX_CALIBRATION_HISTORY_PER_NODE - 1; ++j) {
                _history_profiles[idx][j] = _history_profiles[idx][j + 1];
            }
            _history_profiles[idx][MAX_CALIBRATION_HISTORY_PER_NODE - 1] = _active_profiles[idx];
        }
    }

    // Load profile into engine
    if (!_engines[idx].loadProfile(profile)) {
        return CalibrationRejectionReason::REJECT_INVALID_PARAMETERS;
    }

    _active_profiles[idx] = profile;
    std::strncpy(_active_audit_hashes[idx], calculated_hash, AUDIT_HASH_HEX_LEN - 1);
    _active_audit_hashes[idx][AUDIT_HASH_HEX_LEN - 1] = '\0';
    _node_configured[idx] = true;

    return CalibrationRejectionReason::REJECT_NONE;
}

CalibrationRejectionReason FlowCalibrationRegistry::registerFromDataset(const CalibrationDataset& dataset,
                                                                       uint32_t version) {
    SensorCalibrationProfile profile{};
    CalibrationRejectionReason reason = CalibrationRejectionReason::REJECT_NONE;
    if (!FlowCalibrationEngine::generateProfileFromDataset(dataset, version, profile, reason)) {
        return reason;
    }

    const char* expected_hash = dataset.audit_hash[0] != '\0' ? dataset.audit_hash : nullptr;
    return registerProfile(profile, expected_hash);
}

bool FlowCalibrationRegistry::isNodeCalibrated(uint8_t node_id) const {
    if (!isProductionNodeId(node_id)) return false;
    return _node_configured[node_id - 1];
}

const SensorCalibrationProfile* FlowCalibrationRegistry::getActiveProfile(uint8_t node_id) const {
    if (!isProductionNodeId(node_id)) return nullptr;
    uint8_t idx = node_id - 1;
    return _node_configured[idx] ? &_active_profiles[idx] : nullptr;
}

const char* FlowCalibrationRegistry::getActiveAuditHash(uint8_t node_id) const {
    if (!isProductionNodeId(node_id)) return "";
    uint8_t idx = node_id - 1;
    return _node_configured[idx] ? _active_audit_hashes[idx] : "";
}

FlowCalibrationEngine* FlowCalibrationRegistry::getEngine(uint8_t node_id) {
    if (!isProductionNodeId(node_id)) return nullptr;
    return &_engines[node_id - 1];
}

const FlowCalibrationEngine* FlowCalibrationRegistry::getEngine(uint8_t node_id) const {
    if (!isProductionNodeId(node_id)) return nullptr;
    return &_engines[node_id - 1];
}

uint8_t FlowCalibrationRegistry::getHistoryCount(uint8_t node_id) const {
    if (!isProductionNodeId(node_id)) return 0;
    return _history_count[node_id - 1];
}

const SensorCalibrationProfile* FlowCalibrationRegistry::getHistoricalProfile(uint8_t node_id, uint8_t history_index) const {
    if (!isProductionNodeId(node_id)) return nullptr;
    uint8_t idx = node_id - 1;
    if (history_index >= _history_count[idx]) return nullptr;
    return &_history_profiles[idx][history_index];
}

bool FlowCalibrationRegistry::rollbackToHistoricalVersion(uint8_t node_id, uint32_t target_version, uint32_t new_version) {
    if (!isProductionNodeId(node_id)) return false;
    uint8_t idx = node_id - 1;
    if (!_node_configured[idx]) return false;
    if (new_version <= _active_profiles[idx].version) return false;

    const SensorCalibrationProfile* target_profile = nullptr;
    for (uint8_t j = 0; j < _history_count[idx]; ++j) {
        if (_history_profiles[idx][j].version == target_version) {
            target_profile = &_history_profiles[idx][j];
            break;
        }
    }

    if (target_profile == nullptr) return false;

    // Create a new version based on historical parameters
    SensorCalibrationProfile rollback_profile = *target_profile;
    rollback_profile.version = new_version;
    rollback_profile.checksum_crc32 = FlowCalibrationEngine::calculateProfileCrc32(rollback_profile);

    return registerProfile(rollback_profile) == CalibrationRejectionReason::REJECT_NONE;
}

bool FlowCalibrationRegistry::verifyNodeIntegrity(uint8_t node_id) const {
    if (!isProductionNodeId(node_id)) return false;
    uint8_t idx = node_id - 1;
    if (!_node_configured[idx]) return false;

    const SensorCalibrationProfile& profile = _active_profiles[idx];
    uint32_t expected_crc = FlowCalibrationEngine::calculateProfileCrc32(profile);
    if (profile.checksum_crc32 != expected_crc) return false;

    char hash[AUDIT_HASH_HEX_LEN];
    FlowCalibrationEngine::calculateAuditSha256(profile, hash);
    if (std::strcmp(hash, _active_audit_hashes[idx]) != 0) return false;

    return true;
}

