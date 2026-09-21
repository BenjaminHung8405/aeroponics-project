#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <atomic>
#include "flow_calibration.h"

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
#include <esp_attr.h>
#else
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#endif

/**
 * @brief Configuration struct for FlowPulseCounter.
 */
struct FlowPulseCounterConfig {
    uint32_t nominal_pulses_per_litre = FLOW_PULSES_PER_LITRE_NOMINAL; // Standard K-factor (e.g. 4450 for OF06ZAT)
    uint16_t low_flow_cutoff_lpm_x100 = FLOW_LOW_CUTOFF_LPM_X100;     // Cutoff threshold: 0.15 L/min (15 in x100)
    uint16_t max_flow_limit_lpm_x100 = FLOW_MAX_LIMIT_LPM_X100;       // Over-range threshold: 6.00 L/min (600 in x100)
    uint32_t min_pulse_interval_us = FLOW_MIN_PULSE_INTERVAL_US;       // Hardware debounce refractory window
    uint32_t stale_timeout_ms = FLOW_STALE_TIMEOUT_MS;                 // Timeout for zero pulses when commanded ON
    uint32_t max_sample_window_ms = FLOW_MAX_SAMPLE_WINDOW_MS;         // Upper limit for snapshot window
};

/**
 * @brief Snapshot data structure capturing flow metrics deterministically.
 */
struct FlowSnapshot {
    uint32_t pulse_count;             // Cumulative total pulses counted
    uint32_t delta_pulses;            // Pulses counted in this sample window
    uint32_t sample_window_ms;        // Duration of this sample window in ms
    uint16_t flow_lpm_x100;           // Flow rate in L/min * 100 (e.g. 250 = 2.50 L/min)
    float flow_lpm;                   // Flow rate in L/min as float (e.g. 2.50f)
    uint32_t delivered_volume_ml;     // Cumulative delivered volume in mL
    float delivered_volume_l;         // Cumulative delivered volume in Liters
    uint32_t pulse_freq_hz_x10;       // Frequency of pulses in Hz * 10 (e.g. 1854 = 185.4 Hz)
    FlowEvaluationStatus status;      // Flow evaluation status
    bool is_flow_detected;            // True if flow_lpm_x100 >= low_flow_cutoff_lpm_x100
    bool is_over_range;               // True if flow > 6.00 L/min (or max_flow_limit)
    bool is_stale_or_disconnected;    // True if pump ON but no pulses for >= stale_timeout_ms
    uint32_t timestamp_ms;            // Timestamp of snapshot creation in ms
};

/**
 * @brief Thread-safe, zero-allocation, ISR-safe Flow Pulse Counter & Flow Rate Conversion Engine.
 * 
 * Safety & Architecture Contract:
 * - ISR is strictly O(1), zero-allocation, zero-I/O, zero-logging, zero-blocking.
 * - Hardware debounce / glitch filter discards pulses arriving faster than min_pulse_interval_us.
 * - Atomic snapshots ensure data consistency between high-frequency interrupts and async RTOS/main tasks.
 * - Full integration with FlowCalibrationEngine (piecewise 5-point calibration, K-factor interpolation).
 * - Comprehensive boundary handling: 0 delta time, counter reset, 32-bit overflow rollover,
 *   low-flow cutoff (0.15 L/min clamp), abnormal burst / over-range (>6.00 L/min), and stale disconnect detection.
 */
class FlowPulseCounter {
public:
    FlowPulseCounter();
    explicit FlowPulseCounter(const FlowPulseCounterConfig& config);
    explicit FlowPulseCounter(const FlowPulseCounterConfig& config, const FlowCalibrationEngine& cal_engine);

    /**
     * @brief Initialize counter with starting timestamp.
     */
    void begin(uint32_t start_time_ms = 0);

    /**
     * @brief Hardware ISR pulse notification.
     * MUST be safe to call directly from GPIO Interrupt Service Routine (ISR).
     * Strictly NO heap allocation, NO I/O, NO logging, NO mutex locks.
     * 
     * @param timestamp_us Monotonic microsecond timestamp of the interrupt event.
     */
    void IRAM_ATTR handlePulseFromIsr(uint32_t timestamp_us = 0);

    /**
     * @brief Software simulation hook to inject pulse count (for testing/mocking).
     */
    void injectPulses(uint32_t pulses);

    /**
     * @brief Reset pulse counter and volume accumulator (e.g., at the start of a spray cycle).
     * @param initial_count Starting pulse count (default 0).
     * @param reset_time_ms Timestamp of reset in ms.
     */
    void resetCounter(uint32_t initial_count = 0, uint32_t reset_time_ms = 0);

    /**
     * @brief Take an atomic snapshot and compute flow rate, volume, frequency, and status.
     * 
     * @param now_ms Current monotonic millisecond timestamp.
     * @param pump_commanded_on True if pump is currently commanded ON (for stale/disconnect check).
     * @return FlowSnapshot Computed snapshot of flow metrics.
     */
    FlowSnapshot takeSnapshot(uint32_t now_ms, bool pump_commanded_on = false);

    /**
     * @brief Get the last computed snapshot.
     */
    FlowSnapshot getLastSnapshot() const;

    /**
     * @brief Atomically read the raw cumulative pulse count.
     */
    uint32_t getRawPulseCount() const { return raw_pulse_count_.load(std::memory_order_relaxed); }

    /**
     * @brief Get the count of filtered noise/glitch pulses rejected by debounce.
     */
    uint32_t getFilteredNoiseCount() const { return noise_pulse_count_.load(std::memory_order_relaxed); }

    /**
     * @brief Update or set calibration profile/engine.
     */
    bool setCalibrationProfile(const SensorCalibrationProfile& profile);
    void setCalibrationEngine(const FlowCalibrationEngine& engine);
    const FlowCalibrationEngine& getCalibrationEngine() const { return cal_engine_; }

    /**
     * @brief Get / Update configuration.
     */
    const FlowPulseCounterConfig& getConfig() const { return config_; }
    void setConfig(const FlowPulseCounterConfig& config);

    /**
     * @brief Helper to convert pulse delta and delta ms directly into L/min * 100 without mutating state.
     */
    uint16_t calculateFlowLpmX100(uint32_t delta_pulses, uint32_t delta_time_ms) const;

    /**
     * @brief Helper to convert pulse count directly into delivered volume in mL.
     */
    uint32_t calculateVolumeMl(uint32_t total_pulses) const;

private:
    FlowPulseCounterConfig config_;
    FlowCalibrationEngine cal_engine_;

    // Atomic pulse counters and timestamps for lock-free ISR safety
    std::atomic<uint32_t> raw_pulse_count_;
    std::atomic<uint32_t> noise_pulse_count_;
    std::atomic<uint32_t> last_pulse_timestamp_us_;

    // Snapshot tracking variables (updated in takeSnapshot)
    uint32_t last_snapshot_pulses_;
    uint32_t last_snapshot_time_ms_;
    uint32_t last_active_pulse_time_ms_;
    uint32_t cumulative_volume_ml_;
    FlowSnapshot last_snapshot_;
    bool initialized_;
};
