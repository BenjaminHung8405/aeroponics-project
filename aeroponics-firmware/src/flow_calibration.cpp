#include "flow_calibration.h"
#include <cmath>
#include <cstring>

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
    if (pulse_counts == nullptr || trial_count < 3 || ref_volume_ml == 0) {
        return false;
    }

    uint64_t sum_pulses = 0;
    for (size_t i = 0; i < trial_count; ++i) {
        sum_pulses += pulse_counts[i];
    }
    uint32_t mean_pulses = static_cast<uint32_t>(sum_pulses / trial_count);
    out_stats.mean_pulses = mean_pulses;

    // Standard deviation
    double variance_sum = 0.0;
    for (size_t i = 0; i < trial_count; ++i) {
        double diff = static_cast<double>(pulse_counts[i]) - static_cast<double>(mean_pulses);
        variance_sum += (diff * diff);
    }
    double std_dev = std::sqrt(variance_sum / (trial_count - 1));
    out_stats.std_dev_pulses_x100 = static_cast<uint32_t>(std_dev * 100.0 + 0.5);

    // Repeatability error % * 100: E_rep = (2 * s / mean) * 100%
    if (mean_pulses > 0) {
        double e_rep = (2.0 * std_dev / static_cast<double>(mean_pulses)) * 10000.0;
        out_stats.repeatability_error_pct_x100 = static_cast<uint16_t>(e_rep + 0.5);
    } else {
        out_stats.repeatability_error_pct_x100 = 9999;
    }

    // Calculated mean volume in mL (using nominal reference K = mean_pulses / (ref_volume_ml / 1000.0))
    // For evaluating accuracy error against nominal K-factor (4450 p/L)
    uint32_t nominal_k = 4450;
    uint32_t calculated_vol_ml = static_cast<uint32_t>(((uint64_t)mean_pulses * 1000ULL) / nominal_k);
    out_stats.mean_volume_ml = calculated_vol_ml;

    int32_t vol_diff = static_cast<int32_t>(calculated_vol_ml) - static_cast<int32_t>(ref_volume_ml);
    if (vol_diff < 0) vol_diff = -vol_diff;
    double e_acc = (static_cast<double>(vol_diff) / static_cast<double>(ref_volume_ml)) * 10000.0;
    out_stats.accuracy_error_pct_x100 = static_cast<uint16_t>(e_acc + 0.5);

    // Acceptance thresholds: Repeatability <= 1.50% (150 in x100), Accuracy <= 2.00% (200 in x100)
    out_stats.is_repeatability_acceptable = (out_stats.repeatability_error_pct_x100 <= 150);
    out_stats.is_accuracy_acceptable = (out_stats.accuracy_error_pct_x100 <= 200);

    return true;
}

uint32_t FlowCalibrationEngine::calculateWaterDensity(int16_t temp_c_x10) {
    // Density approximation in g/m3 for water between 15°C and 35°C
    // Tanaka formula simplification: rho(25°C) = 997047 g/m3
    double temp = static_cast<double>(temp_c_x10) / 10.0;
    if (temp < 0.0) temp = 0.0;
    if (temp > 60.0) temp = 60.0;

    // Quadratic approximation: rho(T) = 999.83952 + 16.945176*T - 7.9870401e-3*T^2
    // Normalized to g/m3
    double rho_g_cm3 = 1.0 - ((temp - 3.98) * (temp - 3.98) / 508929.2) * ((temp + 288.9414) / (temp + 68.12963));
    return static_cast<uint32_t>(rho_g_cm3 * 1000000.0 + 0.5);
}

uint32_t FlowCalibrationEngine::calculateProfileCrc32(const SensorCalibrationProfile& profile) {
    // Compute CRC32 over the entire struct except the checksum field at the end
    size_t data_len = offsetof(SensorCalibrationProfile, checksum_crc32);
    return updateCrc32(0, reinterpret_cast<const uint8_t*>(&profile), data_len);
}
