#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include "config.h"
#include "rf_frame_codec.h"
#include "node_registry.h"
#include "flow_fault_evaluator.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#else
#include <mutex>
#endif

// ============================================================================
// Normalized Telemetry & Analytics Contract (SPEC-TELEMETRY-ANALYTICS-001)
// Baseline 2026-08-22: 4 MEGA8 Nodes (IDs 4..7), 0 Raw RF Frames in Database
// ============================================================================

enum class NormalizedCommandOutcome : uint8_t {
    PENDING = 0,
    ACKED,
    FLOW_CONFIRMED,
    FAULT_NO_ACK,
    FAULT_NO_FLOW,
    FAULT_UNEXPECTED_FLOW,
    FAULT_OVER_RANGE,
    FAULT_TIMEOUT,
    REJECTED_INVALID_LEASE,
    REJECTED_AUTH_FAIL,
    REJECTED_FAULT_LOCKOUT,
    REJECTED_UNKNOWN_NODE
};

enum class NormalizedFlowQuality : uint8_t {
    OK = 0,
    SUSPECT,
    INVALID
};

enum class NormalizedScheduleState : uint8_t {
    UNKNOWN = 0,
    SPRAYING,
    COOLING_DOWN,
    IDLE,
    PAUSED
};

enum class NormalizedOverrideState : uint8_t {
    NONE = 0,
    OVERRIDE_OFF,
    OVERRIDE_ON
};

enum class NormalizedResumeReason : uint8_t {
    NONE = 0,
    OVERRIDE_EXPIRED,
    CYCLE_BOUNDARY,
    MANUAL_RESUME,
    FAIL_SAFE_RESUME
};

struct RfDecodedFrame {
    uint8_t message_type = 0;
    uint8_t source_node_id = 0;
    uint8_t target_node_id = 0;
    uint32_t boot_session_id = 0;
    uint16_t sequence = 0;
    uint32_t command_id = 0;
    uint8_t payload_len = 0;
    uint8_t payload[RF_MAX_PAYLOAD_SIZE] = {};
};

// ----------------------------------------------------------------------------
// 1. Normalized Domain Entities (Strictly Parsed - Zero Raw RF Wire Bytes)
// ----------------------------------------------------------------------------

struct NormalizedPumpCommandEvent {
    char command_id[65] = {};
    uint32_t numeric_command_id = 0;
    uint32_t season_id = 1;
    uint8_t node_id = 0;
    uint8_t group_id = 0;
    uint32_t treatment_version_id = 0;
    NodePumpState action = NodePumpState::OFF;
    uint16_t rf_seq = 0;
    uint32_t run_lease_ms = 0;
    char source[32] = "MANUAL_OVERRIDE";
    uint32_t boot_session_id = 0;
    uint8_t retry_count = 0;
    NormalizedCommandOutcome outcome = NormalizedCommandOutcome::PENDING;
    int32_t command_to_ack_latency_ms = -1;
    int32_t flow_start_latency_ms = -1;
    int32_t execution_duration_ms = 0;
    uint64_t node_timestamp_ms = 0;
    uint64_t gateway_timestamp_ms = 0;
    char fault_reason[64] = {};
};

struct NormalizedPumpStateEvent {
    uint32_t season_id = 1;
    uint8_t node_id = 0;
    uint8_t group_id = 0;
    NodePumpState desired_state = NodePumpState::OFF;
    NodePumpState reported_state = NodePumpState::OFF;
    char source[32] = "SCHEDULE";
    NormalizedScheduleState schedule_state = NormalizedScheduleState::UNKNOWN;
    NormalizedOverrideState override_state = NormalizedOverrideState::NONE;
    NormalizedResumeReason resume_reason = NormalizedResumeReason::NONE;
    uint32_t boot_session_id = 0;
    uint16_t rf_seq = 0;
    uint64_t node_timestamp_ms = 0;
    uint64_t gateway_timestamp_ms = 0;
    char reason[64] = {};
};

struct NormalizedPumpFeedbackEvent {
    uint32_t season_id = 1;
    uint8_t node_id = 0;
    uint8_t group_id = 0;
    char command_id[65] = {};
    uint32_t numeric_command_id = 0;
    uint8_t driver_feedback = 0; // 0 = LOW, 1 = HIGH
    uint8_t load_feedback = 2;   // 0 = OFF, 1 = ON, 2 = UNKNOWN
    bool driver_feedback_mismatch = false;
    uint8_t fault_flags = 0;
    uint16_t voltage_mv = 12000; // 12.00V
    uint16_t current_ma = 0;
    uint32_t boot_session_id = 0;
    uint16_t rf_seq = 0;
    uint64_t node_timestamp_ms = 0;
    uint64_t gateway_timestamp_ms = 0;
};

struct NormalizedFlowEvent {
    uint32_t season_id = 1;
    uint8_t node_id = 0;
    uint8_t group_id = 0;
    char command_id[65] = {};
    uint32_t numeric_command_id = 0;
    uint32_t litres_total_x1000 = 0; // 142350 = 142.350 L
    uint32_t pulse_count = 0;
    uint16_t flow_rate_lpm_x100 = 0; // 245 = 2.45 L/min
    uint32_t delivered_volume_ml = 0;
    uint16_t sample_window_ms = 1000;
    uint32_t sensor_calibration_id = 1;
    bool flow_confirmed = false;
    uint16_t flow_stability_pct_x10 = 1000; // 1000 = 100.0%
    NormalizedFlowQuality quality_flag = NormalizedFlowQuality::OK;
    bool is_fault = false;
    char fault_code[32] = "NONE";
    uint32_t boot_session_id = 0;
    uint16_t rf_seq = 0;
    uint64_t node_timestamp_ms = 0;
    uint64_t gateway_timestamp_ms = 0;
};

// ----------------------------------------------------------------------------
// 2. Quantitative Analytics Metrics Data Structure
// ----------------------------------------------------------------------------

struct NodeAnalyticsMetrics {
    uint32_t total_commands_sent = 0;
    uint32_t total_commands_acked = 0;
    uint32_t total_commands_nacked = 0;
    uint32_t total_commands_timed_out = 0;
    uint32_t total_on_commands = 0;
    uint32_t total_flow_confirmed = 0;
    uint16_t confirmation_rate_pct_x100 = 0; // 9867 = 98.67%
    uint32_t total_actual_runtime_ms = 0;
    uint32_t total_delivered_volume_ml = 0;

    int32_t last_command_to_ack_latency_ms = 0;
    int32_t min_command_to_ack_latency_ms = 0;
    int32_t max_command_to_ack_latency_ms = 0;
    int32_t avg_command_to_ack_latency_ms = 0;

    int32_t last_flow_start_latency_ms = 0;
    int32_t min_flow_start_latency_ms = 0;
    int32_t max_flow_start_latency_ms = 0;
    int32_t avg_flow_start_latency_ms = 0;

    uint16_t flow_stability_avg_pct_x10 = 1000; // 958 = 95.8%
    uint16_t retry_rate_pct_x100 = 0;           // 381 = 3.81%
    uint16_t packet_loss_pct_x100 = 0;          // 95 = 0.95%

    uint32_t total_rf_frames_sent = 0;
    uint32_t total_rf_retries = 0;
    uint32_t total_rf_crc_errors = 0;
    uint32_t total_rf_auth_errors = 0;

    uint32_t total_schedule_override_mismatches = 0;
    uint32_t total_override_duration_ms = 0;
    uint32_t total_stale_events = 0;
    uint32_t total_stale_duration_ms = 0;
    uint32_t total_fault_lockouts = 0;

    uint32_t last_seen_node_uptime_s = 0;
    uint64_t last_seen_gateway_timestamp_ms = 0;
};

// ----------------------------------------------------------------------------
// 3. Telemetry Normalizer Class (Zero Raw RF Storage Enforcement)
// ----------------------------------------------------------------------------

class TelemetryNormalizer {
public:
    static bool normalizeTelemetry(const RfDecodedFrame& frame,
                                   uint8_t group_id,
                                   uint32_t season_id,
                                   uint32_t sensor_calibration_id,
                                   uint64_t gateway_timestamp_ms,
                                   NormalizedFlowEvent& out_flow,
                                   NormalizedPumpFeedbackEvent& out_feedback,
                                   NormalizedPumpStateEvent& out_state);

    static bool normalizeCommandAck(const RfDecodedFrame& frame,
                                    const char* correlation_uuid,
                                    uint8_t group_id,
                                    uint32_t season_id,
                                    uint32_t dispatch_time_ms,
                                    uint64_t gateway_timestamp_ms,
                                    NormalizedPumpCommandEvent& out_cmd);

    static bool normalizeFaultReport(const RfDecodedFrame& frame,
                                     uint8_t group_id,
                                     uint32_t season_id,
                                     uint64_t gateway_timestamp_ms,
                                     NormalizedFlowEvent& out_flow,
                                     NormalizedPumpFeedbackEvent& out_feedback);
};

// ----------------------------------------------------------------------------
// 4. Node Analytics Tracker Class (Per-Node Quantitative Math Engine)
// ----------------------------------------------------------------------------

class NodeAnalyticsTracker {
public:
    NodeAnalyticsTracker();

    void recordCommandDispatched(uint32_t command_id, NodePumpState action, uint32_t lease_ms, uint32_t dispatch_time_ms);
    void recordCommandAcked(uint32_t command_id, uint32_t ack_time_ms, bool success);
    void recordFlowConfirmed(uint32_t command_id, uint32_t confirm_time_ms);
    void recordCommandTimeout(uint32_t command_id);
    void recordRfTransmission(uint8_t retry_count);
    void recordRfError(bool crc_error, bool auth_error);
    void recordFlowSample(uint16_t flow_lpm_x100, uint32_t delta_volume_ml, uint16_t stability_pct_x10);
    void recordStateTransition(NormalizedScheduleState sched, NormalizedOverrideState ovr, uint32_t duration_ms);
    void recordStaleEvent(uint32_t stale_duration_ms);
    void recordFaultLockout();

    void getMetrics(NodeAnalyticsMetrics& out_metrics) const;
    void reset();

private:
    NodeAnalyticsMetrics metrics_;
    uint32_t active_command_id_ = 0;
    uint32_t active_dispatch_time_ms_ = 0;
    NodePumpState active_action_ = NodePumpState::OFF;
    uint32_t active_lease_ms_ = 0;
    bool active_acked_ = false;
    bool active_flow_confirmed_ = false;

    // Stability moving average accumulator
    uint32_t flow_samples_count_ = 0;
    uint64_t flow_stability_sum_ = 0;

    // Latency accumulation
    uint64_t cmd_to_ack_sum_ms_ = 0;
    uint32_t ack_samples_count_ = 0;
    uint64_t flow_start_sum_ms_ = 0;
    uint32_t flow_start_samples_count_ = 0;

    void updateRates();
};

// ----------------------------------------------------------------------------
// 5. Analytics Registry for Multi-Node Cluster (4 MEGA8 Nodes)
// ----------------------------------------------------------------------------

class AnalyticsRegistry {
public:
    AnalyticsRegistry();
    ~AnalyticsRegistry();

    bool init();
    NodeAnalyticsTracker* getNodeTracker(uint8_t node_id);
    bool getNodeMetrics(uint8_t node_id, NodeAnalyticsMetrics& out_metrics) const;
    void resetAll();

private:
    NodeAnalyticsTracker trackers_[MAX_NODES];
    bool initialized_;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    mutable SemaphoreHandle_t mutex_;
#else
    mutable std::mutex mutex_;
#endif
};

// ----------------------------------------------------------------------------
// 6. JSON Serialization Contracts (MQTT & TimescaleDB Compliant)
// ----------------------------------------------------------------------------

bool serializeNormalizedTelemetryJson(const NormalizedFlowEvent& flow,
                                      const NormalizedPumpFeedbackEvent& feedback,
                                      const NormalizedPumpStateEvent& state,
                                      char* out_buf,
                                      size_t buf_len);

bool serializeAnalyticsSummaryJson(uint8_t node_id,
                                   uint32_t season_id,
                                   const NodeAnalyticsMetrics& metrics,
                                   char* out_buf,
                                   size_t buf_len);

bool serializeCommandLifecycleEventJson(const NormalizedPumpCommandEvent& cmd,
                                        char* out_buf,
                                        size_t buf_len);
