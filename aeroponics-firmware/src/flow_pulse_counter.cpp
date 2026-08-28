#include "flow_pulse_counter.h"
#include <cstring>

FlowPulseCounter::FlowPulseCounter()
    : config_{},
      cal_engine_{},
      raw_pulse_count_(0),
      noise_pulse_count_(0),
      last_pulse_timestamp_us_(0),
      last_snapshot_pulses_(0),
      last_snapshot_time_ms_(0),
      last_active_pulse_time_ms_(0),
      cumulative_volume_ml_(0),
      last_snapshot_{},
      initialized_(false) {
}

FlowPulseCounter::FlowPulseCounter(const FlowPulseCounterConfig& config)
    : config_(config),
      cal_engine_{},
      raw_pulse_count_(0),
      noise_pulse_count_(0),
      last_pulse_timestamp_us_(0),
      last_snapshot_pulses_(0),
      last_snapshot_time_ms_(0),
      last_active_pulse_time_ms_(0),
      cumulative_volume_ml_(0),
      last_snapshot_{},
      initialized_(false) {
    // Configure default calibration profile according to config
    SensorCalibrationProfile prof{};
    prof.nominal_pulses_per_litre = config_.nominal_pulses_per_litre;
    prof.low_flow_cutoff_lpm_x100 = config_.low_flow_cutoff_lpm_x100;
    prof.max_flow_limit_lpm_x100 = config_.max_flow_limit_lpm_x100;
    prof.num_calibration_points = 0;
    cal_engine_.loadProfile(prof);
}

FlowPulseCounter::FlowPulseCounter(const FlowPulseCounterConfig& config, const FlowCalibrationEngine& cal_engine)
    : config_(config),
      cal_engine_(cal_engine),
      raw_pulse_count_(0),
      noise_pulse_count_(0),
      last_pulse_timestamp_us_(0),
      last_snapshot_pulses_(0),
      last_snapshot_time_ms_(0),
      last_active_pulse_time_ms_(0),
      cumulative_volume_ml_(0),
      last_snapshot_{},
      initialized_(false) {
}

void FlowPulseCounter::begin(uint32_t start_time_ms) {
    raw_pulse_count_.store(0, std::memory_order_relaxed);
    noise_pulse_count_.store(0, std::memory_order_relaxed);
    last_pulse_timestamp_us_.store(0, std::memory_order_relaxed);
    last_snapshot_pulses_ = 0;
    last_snapshot_time_ms_ = start_time_ms;
    last_active_pulse_time_ms_ = start_time_ms;
    cumulative_volume_ml_ = 0;
    last_snapshot_ = FlowSnapshot{
        0, 0, 0, 0, 0.0f, 0, 0.0f, 0,
        FlowEvaluationStatus::FLOW_ZERO_OR_CUTOFF,
        false, false, false, start_time_ms
    };
    initialized_ = true;
}

void IRAM_ATTR FlowPulseCounter::handlePulseFromIsr(uint32_t timestamp_us) {
    // Zero heap allocation, zero I/O, zero logging, zero blocking mutexes
    if (config_.min_pulse_interval_us > 0 && timestamp_us != 0) {
        uint32_t last_us = last_pulse_timestamp_us_.load(std::memory_order_relaxed);
        if (last_us != 0) {
            uint32_t delta_us = timestamp_us - last_us;
            if (delta_us < config_.min_pulse_interval_us) {
                // Reject high-frequency noise / contact bounce glitch
                noise_pulse_count_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        last_pulse_timestamp_us_.store(timestamp_us, std::memory_order_relaxed);
    }

    raw_pulse_count_.fetch_add(1, std::memory_order_relaxed);
}

void FlowPulseCounter::injectPulses(uint32_t pulses) {
    raw_pulse_count_.fetch_add(pulses, std::memory_order_relaxed);
}

void FlowPulseCounter::resetCounter(uint32_t initial_count, uint32_t reset_time_ms) {
    raw_pulse_count_.store(initial_count, std::memory_order_relaxed);
    noise_pulse_count_.store(0, std::memory_order_relaxed);
    last_pulse_timestamp_us_.store(0, std::memory_order_relaxed);
    last_snapshot_pulses_ = initial_count;
    last_snapshot_time_ms_ = reset_time_ms;
    last_active_pulse_time_ms_ = reset_time_ms;
    cumulative_volume_ml_ = cal_engine_.calculateDeliveredVolumeMl(initial_count);
    last_snapshot_ = FlowSnapshot{
        initial_count, 0, 0, 0, 0.0f,
        cumulative_volume_ml_,
        static_cast<float>(cumulative_volume_ml_) / 1000.0f,
        0, FlowEvaluationStatus::FLOW_ZERO_OR_CUTOFF,
        false, false, false, reset_time_ms
    };
}

FlowSnapshot FlowPulseCounter::takeSnapshot(uint32_t now_ms, bool pump_commanded_on) {
    uint32_t current_raw = raw_pulse_count_.load(std::memory_order_relaxed);

    if (!initialized_) {
        last_snapshot_time_ms_ = now_ms;
        last_snapshot_pulses_ = current_raw;
        last_active_pulse_time_ms_ = now_ms;
        initialized_ = true;
        cumulative_volume_ml_ = cal_engine_.calculateDeliveredVolumeMl(current_raw);
        last_snapshot_ = FlowSnapshot{
            current_raw, 0, 0, 0, 0.0f,
            cumulative_volume_ml_,
            static_cast<float>(cumulative_volume_ml_) / 1000.0f,
            0, FlowEvaluationStatus::FLOW_ZERO_OR_CUTOFF,
            false, false, false, now_ms
        };
        return last_snapshot_;
    }

    uint32_t delta_time_ms = now_ms - last_snapshot_time_ms_;
    if (delta_time_ms == 0) {
        FlowSnapshot invalid_snap = last_snapshot_;
        invalid_snap.status = FlowEvaluationStatus::FLOW_INVALID_PARAMETERS;
        return invalid_snap;
    }

    // Unsigned 32-bit delta handles rollover cleanly
    uint32_t delta_pulses = current_raw - last_snapshot_pulses_;

    if (delta_pulses > 0) {
        last_active_pulse_time_ms_ = now_ms;
    }

    // Calculate pulse frequency in Hz * 10 = (pulses * 1000 * 10) / ms
    uint64_t freq_calc = ((uint64_t)delta_pulses * 10000ULL) / delta_time_ms;
    uint32_t freq_hz_x10 = static_cast<uint32_t>(freq_calc);

    uint16_t flow_lpm_x100 = 0;
    FlowEvaluationStatus status = cal_engine_.calculateFlowRate(delta_pulses, delta_time_ms, flow_lpm_x100);

    // Apply low flow cutoff clamp
    if (flow_lpm_x100 < config_.low_flow_cutoff_lpm_x100) {
        flow_lpm_x100 = 0;
        status = FlowEvaluationStatus::FLOW_ZERO_OR_CUTOFF;
    }

    // Apply max limit / over-range threshold
    if (flow_lpm_x100 > config_.max_flow_limit_lpm_x100 || status == FlowEvaluationStatus::FLOW_OVER_RANGE) {
        status = FlowEvaluationStatus::FLOW_OVER_RANGE;
    }

    // Check for stale flow / disconnected sensor when pump is commanded ON
    if (pump_commanded_on && flow_lpm_x100 == 0) {
        uint32_t time_since_last_pulse = now_ms - last_active_pulse_time_ms_;
        if (time_since_last_pulse >= config_.stale_timeout_ms) {
            status = FlowEvaluationStatus::FLOW_STALE_OR_DISCONNECTED;
        }
    }

    cumulative_volume_ml_ = cal_engine_.calculateDeliveredVolumeMl(current_raw);

    FlowSnapshot snapshot;
    snapshot.pulse_count = current_raw;
    snapshot.delta_pulses = delta_pulses;
    snapshot.sample_window_ms = delta_time_ms;
    snapshot.flow_lpm_x100 = flow_lpm_x100;
    snapshot.flow_lpm = static_cast<float>(flow_lpm_x100) / 100.0f;
    snapshot.delivered_volume_ml = cumulative_volume_ml_;
    snapshot.delivered_volume_l = static_cast<float>(cumulative_volume_ml_) / 1000.0f;
    snapshot.pulse_freq_hz_x10 = freq_hz_x10;
    snapshot.status = status;
    snapshot.is_flow_detected = (flow_lpm_x100 >= config_.low_flow_cutoff_lpm_x100 && flow_lpm_x100 > 0);
    snapshot.is_over_range = (status == FlowEvaluationStatus::FLOW_OVER_RANGE);
    snapshot.is_stale_or_disconnected = (status == FlowEvaluationStatus::FLOW_STALE_OR_DISCONNECTED);
    snapshot.timestamp_ms = now_ms;

    last_snapshot_pulses_ = current_raw;
    last_snapshot_time_ms_ = now_ms;
    last_snapshot_ = snapshot;

    return snapshot;
}

FlowSnapshot FlowPulseCounter::getLastSnapshot() const {
    return last_snapshot_;
}

bool FlowPulseCounter::setCalibrationProfile(const SensorCalibrationProfile& profile) {
    bool ok = cal_engine_.loadProfile(profile);
    if (ok) {
        config_.nominal_pulses_per_litre = profile.nominal_pulses_per_litre;
        config_.low_flow_cutoff_lpm_x100 = profile.low_flow_cutoff_lpm_x100;
        config_.max_flow_limit_lpm_x100 = profile.max_flow_limit_lpm_x100;
    }
    return ok;
}

void FlowPulseCounter::setCalibrationEngine(const FlowCalibrationEngine& engine) {
    cal_engine_ = engine;
    if (engine.hasValidProfile()) {
        const SensorCalibrationProfile& prof = engine.getProfile();
        config_.nominal_pulses_per_litre = prof.nominal_pulses_per_litre;
        config_.low_flow_cutoff_lpm_x100 = prof.low_flow_cutoff_lpm_x100;
        config_.max_flow_limit_lpm_x100 = prof.max_flow_limit_lpm_x100;
    }
}

void FlowPulseCounter::setConfig(const FlowPulseCounterConfig& config) {
    config_ = config;
    if (!cal_engine_.hasValidProfile()) {
        SensorCalibrationProfile prof{};
        prof.nominal_pulses_per_litre = config_.nominal_pulses_per_litre;
        prof.low_flow_cutoff_lpm_x100 = config_.low_flow_cutoff_lpm_x100;
        prof.max_flow_limit_lpm_x100 = config_.max_flow_limit_lpm_x100;
        prof.num_calibration_points = 0;
        cal_engine_.loadProfile(prof);
    }
}

uint16_t FlowPulseCounter::calculateFlowLpmX100(uint32_t delta_pulses, uint32_t delta_time_ms) const {
    uint16_t flow_x100 = 0;
    cal_engine_.calculateFlowRate(delta_pulses, delta_time_ms, flow_x100);
    if (flow_x100 < config_.low_flow_cutoff_lpm_x100) {
        return 0;
    }
    return flow_x100;
}

uint32_t FlowPulseCounter::calculateVolumeMl(uint32_t total_pulses) const {
    return cal_engine_.calculateDeliveredVolumeMl(total_pulses);
}
