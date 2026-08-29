#include "telemetry_analytics.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

// ============================================================================
// 1. TelemetryNormalizer Implementation
// ============================================================================

bool TelemetryNormalizer::normalizeTelemetry(const RfDecodedFrame& frame,
                                             uint8_t group_id,
                                             uint32_t season_id,
                                             uint32_t sensor_calibration_id,
                                             uint64_t gateway_timestamp_ms,
                                             NormalizedFlowEvent& out_flow,
                                             NormalizedPumpFeedbackEvent& out_feedback,
                                             NormalizedPumpStateEvent& out_state) {
    // Gate 1: Message type and minimum payload length.
    if (frame.message_type != static_cast<uint8_t>(RfMessageType::TELEMETRY) || frame.payload_len < 17) {
        return false;
    }

    // Gate 2: Fail-closed — reject frames from node IDs outside production scope (1..4).
    // Node IDs 5..12 are reserved backlog; 0 is the gateway address.
    if (frame.source_node_id < RF_MIN_NODE_ID || frame.source_node_id > RF_PRODUCTION_MAX_NODE_ID) {
        return false;
    }

    // Gate 3: Fail-closed — telemetry must be addressed TO the gateway (target == 0).
    if (frame.target_node_id != RF_GATEWAY_NODE_ID) {
        return false;
    }

    const uint8_t* p = frame.payload;
    uint8_t reported_state_raw = p[0];
    uint8_t driver_fb_raw = p[1];
    uint16_t flow_lpm_x100 = static_cast<uint16_t>(p[2] | (p[3] << 8));
    uint32_t volume_ml = static_cast<uint32_t>(p[4] | (p[5] << 8) | (p[6] << 16) | (p[7] << 24));
    uint32_t pulse_count = static_cast<uint32_t>(p[8] | (p[9] << 8) | (p[10] << 16) | (p[11] << 24));
    uint8_t fault_flags = p[12];
    uint32_t last_cmd_id = static_cast<uint32_t>(p[13] | (p[14] << 8) | (p[15] << 16) | (p[16] << 24));

    // Gate 4: Fail-closed — reported_state and driver_feedback must be strictly 0 or 1.
    if (reported_state_raw > 1 || driver_fb_raw > 1) {
        return false;
    }

    // Gate 5: Fail-closed — fault_flags must only contain defined bit positions.
    // Defined bits: bit0=NO_FLOW, bit1=UNEXPECTED_FLOW, bit2=OVER_RANGE, bit3=STALE_SENSOR, bit4=GATE_MISMATCH, bit5=STALL_OVERCURRENT
    // Bits 6..7 are undefined; reject frames with undefined fault bits set.
    constexpr uint8_t DEFINED_FAULT_BITS = 0x3F; // bits 0..5 valid
    if (fault_flags & ~DEFINED_FAULT_BITS) {
        return false;
    }

    uint8_t node_id = frame.source_node_id;

    // node_timestamp_ms: The TelemetryPayload wire format (17 bytes) does NOT include
    // a node-side wall-clock timestamp field. boot_session_id is an anti-replay counter,
    // NOT a timestamp — do NOT substitute it as node_timestamp_ms.
    // We store 0 to signal UNKNOWN; the gateway_timestamp_ms is the authoritative timestamp
    // until the wire protocol is extended to carry a real node_timestamp_ms field.
    const uint64_t node_ts_ms = 0; // UNKNOWN — TelemetryPayload has no node timestamp field

    // Zero out and populate NormalizedFlowEvent
    std::memset(&out_flow, 0, sizeof(out_flow));
    out_flow.season_id = season_id;
    out_flow.node_id = node_id;
    out_flow.group_id = group_id;
    out_flow.numeric_command_id = last_cmd_id;
    if (last_cmd_id != 0) {
        std::snprintf(out_flow.command_id, sizeof(out_flow.command_id), "cmd-%u-%u", node_id, last_cmd_id);
    }
    out_flow.litres_total_x1000 = volume_ml; // 1 mL = 0.001 L => volume_ml = litres * 1000
    out_flow.pulse_count = pulse_count;
    out_flow.flow_rate_lpm_x100 = flow_lpm_x100;
    out_flow.delivered_volume_ml = volume_ml;
    out_flow.sample_window_ms = 1000;
    out_flow.sensor_calibration_id = sensor_calibration_id;
    out_flow.boot_session_id = frame.boot_session_id;
    out_flow.rf_seq = frame.sequence;
    out_flow.node_timestamp_ms = node_ts_ms;        // 0 = UNKNOWN (no wire timestamp in payload)
    out_flow.gateway_timestamp_ms = gateway_timestamp_ms;

    // Flow confirmation evaluation: flow is confirmed if pump is reported ON, driver is ON, and flow >= 0.15 L/min
    if (reported_state_raw == 1 && driver_fb_raw == 1 && flow_lpm_x100 >= 15 && flow_lpm_x100 <= 600) {
        out_flow.flow_confirmed = true;
        out_flow.flow_stability_pct_x10 = 960; // Nominal steady spray 96.0%
        out_flow.quality_flag = NormalizedFlowQuality::OK;
        out_flow.is_fault = false;
        std::strncpy(out_flow.fault_code, "NONE", sizeof(out_flow.fault_code) - 1);
    } else if (fault_flags & 0x01) { // NO_FLOW flag
        out_flow.flow_confirmed = false;
        out_flow.flow_stability_pct_x10 = 0;
        out_flow.quality_flag = NormalizedFlowQuality::INVALID;
        out_flow.is_fault = true;
        std::strncpy(out_flow.fault_code, "NO_FLOW_FAULT", sizeof(out_flow.fault_code) - 1);
    } else if (fault_flags & 0x02) { // UNEXPECTED_FLOW flag
        out_flow.flow_confirmed = false;
        out_flow.flow_stability_pct_x10 = 0;
        out_flow.quality_flag = NormalizedFlowQuality::INVALID;
        out_flow.is_fault = true;
        std::strncpy(out_flow.fault_code, "UNEXPECTED_FLOW_FAULT", sizeof(out_flow.fault_code) - 1);
    } else if (flow_lpm_x100 > 600) {
        out_flow.flow_confirmed = false;
        out_flow.flow_stability_pct_x10 = 0;
        out_flow.quality_flag = NormalizedFlowQuality::INVALID;
        out_flow.is_fault = true;
        std::strncpy(out_flow.fault_code, "OVER_RANGE_FAULT", sizeof(out_flow.fault_code) - 1);
    } else {
        out_flow.flow_confirmed = false;
        out_flow.flow_stability_pct_x10 = (reported_state_raw == 1) ? 500 : 1000;
        out_flow.quality_flag = NormalizedFlowQuality::OK;
        out_flow.is_fault = false;
        std::strncpy(out_flow.fault_code, "NONE", sizeof(out_flow.fault_code) - 1);
    }

    // Zero out and populate NormalizedPumpFeedbackEvent
    std::memset(&out_feedback, 0, sizeof(out_feedback));
    out_feedback.season_id = season_id;
    out_feedback.node_id = node_id;
    out_feedback.group_id = group_id;
    out_feedback.numeric_command_id = last_cmd_id;
    if (last_cmd_id != 0) {
        std::snprintf(out_feedback.command_id, sizeof(out_feedback.command_id), "cmd-%u-%u", node_id, last_cmd_id);
    }
    out_feedback.driver_feedback = driver_fb_raw;
    // load_feedback: UNKNOWN (2) because TelemetryPayload has no dedicated load-sensor field.
    // We MUST NOT infer load state from driver_feedback — they are different physical signals.
    // A driver being HIGH (optocoupler gate ON) does NOT mean the pump is drawing current.
    // Set to UNKNOWN; the gateway's ACS712/current-sense path updates this independently.
    out_feedback.load_feedback = 2; // 2 = UNKNOWN; do NOT derive from driver_feedback
    out_feedback.driver_feedback_mismatch = (reported_state_raw != driver_fb_raw);
    out_feedback.fault_flags = fault_flags;
    out_feedback.voltage_mv = 12150; // 12.15V nominal
    // current_ma: not inferrable from driver_feedback alone — leave at 0 (no sensing data in this frame)
    out_feedback.current_ma = 0;
    out_feedback.boot_session_id = frame.boot_session_id;
    out_feedback.rf_seq = frame.sequence;
    out_feedback.node_timestamp_ms = node_ts_ms;   // 0 = UNKNOWN (no wire timestamp in payload)
    out_feedback.gateway_timestamp_ms = gateway_timestamp_ms;

    // Zero out and populate NormalizedPumpStateEvent
    std::memset(&out_state, 0, sizeof(out_state));
    out_state.season_id = season_id;
    out_state.node_id = node_id;
    out_state.group_id = group_id;
    out_state.desired_state = (reported_state_raw == 1) ? NodePumpState::ON : NodePumpState::OFF;
    out_state.reported_state = (reported_state_raw == 1) ? NodePumpState::ON : NodePumpState::OFF;
    std::strncpy(out_state.source, "SCHEDULE", sizeof(out_state.source) - 1);
    out_state.schedule_state = (reported_state_raw == 1) ? NormalizedScheduleState::SPRAYING : NormalizedScheduleState::IDLE;
    out_state.override_state = NormalizedOverrideState::NONE;
    out_state.resume_reason = NormalizedResumeReason::NONE;
    out_state.boot_session_id = frame.boot_session_id;
    out_state.rf_seq = frame.sequence;
    out_state.node_timestamp_ms = node_ts_ms;      // 0 = UNKNOWN (no wire timestamp in payload)
    out_state.gateway_timestamp_ms = gateway_timestamp_ms;
    std::strncpy(out_state.reason, "Telemetry Sync", sizeof(out_state.reason) - 1);

    return true;
}

bool TelemetryNormalizer::normalizeCommandAck(const RfDecodedFrame& frame,
                                              const char* correlation_uuid,
                                              uint8_t group_id,
                                              uint32_t season_id,
                                              uint32_t dispatch_time_ms,
                                              uint64_t gateway_timestamp_ms,
                                              NormalizedPumpCommandEvent& out_cmd) {
    if (frame.message_type != static_cast<uint8_t>(RfMessageType::COMMAND_ACK) || frame.payload_len < 5) {
        return false;
    }

    const uint8_t* p = frame.payload;
    uint16_t ack_seq = static_cast<uint16_t>(p[0] | (p[1] << 8));
    uint8_t outcome_raw = p[2];
    uint8_t reported_state = p[3];

    std::memset(&out_cmd, 0, sizeof(out_cmd));
    if (correlation_uuid && correlation_uuid[0] != '\0') {
        std::strncpy(out_cmd.command_id, correlation_uuid, sizeof(out_cmd.command_id) - 1);
    } else {
        std::snprintf(out_cmd.command_id, sizeof(out_cmd.command_id), "cmd-%u-%u", frame.source_node_id, frame.command_id);
    }
    out_cmd.numeric_command_id = frame.command_id;
    out_cmd.season_id = season_id;
    out_cmd.node_id = frame.source_node_id;
    out_cmd.group_id = group_id;
    out_cmd.action = (reported_state == 1) ? NodePumpState::ON : NodePumpState::OFF;
    out_cmd.rf_seq = ack_seq;
    out_cmd.boot_session_id = frame.boot_session_id;
    out_cmd.node_timestamp_ms = 0; // UNKNOWN — CommandAck payload has no node timestamp field
    out_cmd.gateway_timestamp_ms = gateway_timestamp_ms;

    if (gateway_timestamp_ms >= dispatch_time_ms) {
        out_cmd.command_to_ack_latency_ms = static_cast<int32_t>(gateway_timestamp_ms - dispatch_time_ms);
    } else {
        out_cmd.command_to_ack_latency_ms = 0;
    }

    switch (outcome_raw) {
        case 0: // SUCCESS
            out_cmd.outcome = NormalizedCommandOutcome::ACKED;
            break;
        case 1: // REJECTED_INVALID_LEASE
            out_cmd.outcome = NormalizedCommandOutcome::REJECTED_INVALID_LEASE;
            std::strncpy(out_cmd.fault_reason, "REJECTED_INVALID_LEASE", sizeof(out_cmd.fault_reason) - 1);
            break;
        case 2: // REJECTED_AUTH_FAIL
            out_cmd.outcome = NormalizedCommandOutcome::REJECTED_AUTH_FAIL;
            std::strncpy(out_cmd.fault_reason, "REJECTED_AUTH_FAIL", sizeof(out_cmd.fault_reason) - 1);
            break;
        case 3: // FAULT_LOCKOUT
            out_cmd.outcome = NormalizedCommandOutcome::REJECTED_FAULT_LOCKOUT;
            std::strncpy(out_cmd.fault_reason, "REJECTED_FAULT_LOCKOUT", sizeof(out_cmd.fault_reason) - 1);
            break;
        default:
            out_cmd.outcome = NormalizedCommandOutcome::REJECTED_UNKNOWN_NODE;
            std::strncpy(out_cmd.fault_reason, "REJECTED_UNKNOWN", sizeof(out_cmd.fault_reason) - 1);
            break;
    }

    return true;
}

bool TelemetryNormalizer::normalizeFaultReport(const RfDecodedFrame& frame,
                                              uint8_t group_id,
                                              uint32_t season_id,
                                              uint64_t gateway_timestamp_ms,
                                              NormalizedFlowEvent& out_flow,
                                              NormalizedPumpFeedbackEvent& out_feedback) {
    if (frame.message_type != static_cast<uint8_t>(RfMessageType::FAULT_REPORT) || frame.payload_len < 6) {
        return false;
    }

    const uint8_t* p = frame.payload;
    uint8_t fault_code_raw = p[0];
    uint32_t timestamp_ms = static_cast<uint32_t>(p[1] | (p[2] << 8) | (p[3] << 16) | (p[4] << 24));
    uint32_t cmd_id = (frame.payload_len >= 10) ?
        static_cast<uint32_t>(p[6] | (p[7] << 8) | (p[8] << 16) | (p[9] << 24)) : frame.command_id;

    uint8_t node_id = frame.source_node_id;

    std::memset(&out_flow, 0, sizeof(out_flow));
    out_flow.season_id = season_id;
    out_flow.node_id = node_id;
    out_flow.group_id = group_id;
    out_flow.numeric_command_id = cmd_id;
    out_flow.is_fault = true;
    out_flow.flow_confirmed = false;
    out_flow.quality_flag = NormalizedFlowQuality::INVALID;
    out_flow.boot_session_id = frame.boot_session_id;
    out_flow.rf_seq = frame.sequence;
    out_flow.node_timestamp_ms = timestamp_ms;
    out_flow.gateway_timestamp_ms = gateway_timestamp_ms;

    switch (fault_code_raw) {
        case 1:
            std::strncpy(out_flow.fault_code, "NO_FLOW_FAULT", sizeof(out_flow.fault_code) - 1);
            break;
        case 2:
            std::strncpy(out_flow.fault_code, "UNEXPECTED_FLOW_FAULT", sizeof(out_flow.fault_code) - 1);
            break;
        case 3:
            std::strncpy(out_flow.fault_code, "OVER_RANGE_FAULT", sizeof(out_flow.fault_code) - 1);
            break;
        default:
            std::strncpy(out_flow.fault_code, "SENSOR_FAULT", sizeof(out_flow.fault_code) - 1);
            break;
    }

    std::memset(&out_feedback, 0, sizeof(out_feedback));
    out_feedback.season_id = season_id;
    out_feedback.node_id = node_id;
    out_feedback.group_id = group_id;
    out_feedback.numeric_command_id = cmd_id;
    out_feedback.driver_feedback = 0; // safe-off
    out_feedback.load_feedback = 0;
    out_feedback.fault_flags = fault_code_raw;
    out_feedback.boot_session_id = frame.boot_session_id;
    out_feedback.rf_seq = frame.sequence;
    out_feedback.node_timestamp_ms = timestamp_ms;
    out_feedback.gateway_timestamp_ms = gateway_timestamp_ms;

    return true;
}

// ============================================================================
// 2. NodeAnalyticsTracker Implementation
// ============================================================================

NodeAnalyticsTracker::NodeAnalyticsTracker() {
    reset();
}

void NodeAnalyticsTracker::reset() {
    std::memset(&metrics_, 0, sizeof(metrics_));
    metrics_.flow_stability_avg_pct_x10 = 1000; // 100.0% default
    active_command_id_ = 0;
    active_dispatch_time_ms_ = 0;
    active_action_ = NodePumpState::OFF;
    active_lease_ms_ = 0;
    active_acked_ = false;
    active_flow_confirmed_ = false;

    flow_samples_count_ = 0;
    flow_stability_sum_ = 0;
    cmd_to_ack_sum_ms_ = 0;
    ack_samples_count_ = 0;
    flow_start_sum_ms_ = 0;
    flow_start_samples_count_ = 0;
}

void NodeAnalyticsTracker::recordCommandDispatched(uint32_t command_id, NodePumpState action, uint32_t lease_ms, uint32_t dispatch_time_ms) {
    active_command_id_ = command_id;
    active_dispatch_time_ms_ = dispatch_time_ms;
    active_action_ = action;
    active_lease_ms_ = lease_ms;
    active_acked_ = false;
    active_flow_confirmed_ = false;

    ++metrics_.total_commands_sent;
    if (action == NodePumpState::ON) {
        ++metrics_.total_on_commands;
    }
    updateRates();
}

void NodeAnalyticsTracker::recordCommandAcked(uint32_t command_id, uint32_t ack_time_ms, bool success) {
    if (success) {
        ++metrics_.total_commands_acked;
        active_acked_ = true;

        if (command_id == active_command_id_ && ack_time_ms >= active_dispatch_time_ms_) {
            int32_t lat = static_cast<int32_t>(ack_time_ms - active_dispatch_time_ms_);
            metrics_.last_command_to_ack_latency_ms = lat;
            if (metrics_.min_command_to_ack_latency_ms == 0 || lat < metrics_.min_command_to_ack_latency_ms) {
                metrics_.min_command_to_ack_latency_ms = lat;
            }
            if (lat > metrics_.max_command_to_ack_latency_ms) {
                metrics_.max_command_to_ack_latency_ms = lat;
            }
            cmd_to_ack_sum_ms_ += lat;
            ++ack_samples_count_;
            metrics_.avg_command_to_ack_latency_ms = static_cast<int32_t>(cmd_to_ack_sum_ms_ / ack_samples_count_);
        }
    } else {
        ++metrics_.total_commands_nacked;
    }
    updateRates();
}

void NodeAnalyticsTracker::recordFlowConfirmed(uint32_t command_id, uint32_t confirm_time_ms) {
    ++metrics_.total_flow_confirmed;
    active_flow_confirmed_ = true;

    if (command_id == active_command_id_ && confirm_time_ms >= active_dispatch_time_ms_) {
        int32_t lat = static_cast<int32_t>(confirm_time_ms - active_dispatch_time_ms_);
        metrics_.last_flow_start_latency_ms = lat;
        if (metrics_.min_flow_start_latency_ms == 0 || lat < metrics_.min_flow_start_latency_ms) {
            metrics_.min_flow_start_latency_ms = lat;
        }
        if (lat > metrics_.max_flow_start_latency_ms) {
            metrics_.max_flow_start_latency_ms = lat;
        }
        flow_start_sum_ms_ += lat;
        ++flow_start_samples_count_;
        metrics_.avg_flow_start_latency_ms = static_cast<int32_t>(flow_start_sum_ms_ / flow_start_samples_count_);
    }

    if (active_action_ == NodePumpState::ON && active_lease_ms_ > 0) {
        metrics_.total_actual_runtime_ms += active_lease_ms_;
    }
    updateRates();
}

void NodeAnalyticsTracker::recordCommandTimeout(uint32_t command_id) {
    (void)command_id;
    ++metrics_.total_commands_timed_out;
    updateRates();
}

void NodeAnalyticsTracker::recordRfTransmission(uint8_t retry_count) {
    metrics_.total_rf_frames_sent += (1 + retry_count);
    metrics_.total_rf_retries += retry_count;
    updateRates();
}

void NodeAnalyticsTracker::recordRfError(bool crc_error, bool auth_error) {
    if (crc_error) ++metrics_.total_rf_crc_errors;
    if (auth_error) ++metrics_.total_rf_auth_errors;
}

void NodeAnalyticsTracker::recordFlowSample(uint16_t flow_lpm_x100, uint32_t delta_volume_ml, uint16_t stability_pct_x10) {
    (void)flow_lpm_x100;
    metrics_.total_delivered_volume_ml += delta_volume_ml;
    flow_stability_sum_ += stability_pct_x10;
    ++flow_samples_count_;
    metrics_.flow_stability_avg_pct_x10 = static_cast<uint16_t>(flow_stability_sum_ / flow_samples_count_);
}

void NodeAnalyticsTracker::recordStateTransition(NormalizedScheduleState sched, NormalizedOverrideState ovr, uint32_t duration_ms) {
    (void)sched;
    if (ovr != NormalizedOverrideState::NONE) {
        ++metrics_.total_schedule_override_mismatches;
        metrics_.total_override_duration_ms += duration_ms;
    }
}

void NodeAnalyticsTracker::recordStaleEvent(uint32_t stale_duration_ms) {
    ++metrics_.total_stale_events;
    metrics_.total_stale_duration_ms += stale_duration_ms;
}

void NodeAnalyticsTracker::recordFaultLockout() {
    ++metrics_.total_fault_lockouts;
}

void NodeAnalyticsTracker::updateRates() {
    if (metrics_.total_on_commands > 0) {
        metrics_.confirmation_rate_pct_x100 = static_cast<uint16_t>(
            (metrics_.total_flow_confirmed * 10000ULL) / metrics_.total_on_commands);
    } else {
        metrics_.confirmation_rate_pct_x100 = 10000;
    }

    if (metrics_.total_rf_frames_sent > 0) {
        metrics_.retry_rate_pct_x100 = static_cast<uint16_t>(
            (metrics_.total_rf_retries * 10000ULL) / metrics_.total_rf_frames_sent);
    } else {
        metrics_.retry_rate_pct_x100 = 0;
    }

    if (metrics_.total_commands_sent > 0) {
        metrics_.packet_loss_pct_x100 = static_cast<uint16_t>(
            (metrics_.total_commands_timed_out * 10000ULL) / metrics_.total_commands_sent);
    } else {
        metrics_.packet_loss_pct_x100 = 0;
    }
}

void NodeAnalyticsTracker::getMetrics(NodeAnalyticsMetrics& out_metrics) const {
    out_metrics = metrics_;
}

// ============================================================================
// 3. AnalyticsRegistry Implementation
// ============================================================================

AnalyticsRegistry::AnalyticsRegistry() : initialized_(false) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    mutex_ = xSemaphoreCreateMutex();
#endif
    init();
}

AnalyticsRegistry::~AnalyticsRegistry() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
#endif
}

bool AnalyticsRegistry::init() {
    resetAll();
    initialized_ = true;
    return true;
}

NodeAnalyticsTracker* AnalyticsRegistry::getNodeTracker(uint8_t node_id) {
    if (node_id < 1 || node_id > MAX_NODES) {
        return nullptr;
    }
    return &trackers_[node_id - 1];
}

bool AnalyticsRegistry::getNodeMetrics(uint8_t node_id, NodeAnalyticsMetrics& out_metrics) const {
    if (node_id < 1 || node_id > MAX_NODES) {
        return false;
    }
    trackers_[node_id - 1].getMetrics(out_metrics);
    return true;
}

void AnalyticsRegistry::resetAll() {
    for (size_t i = 0; i < MAX_NODES; ++i) {
        trackers_[i].reset();
    }
}

// ============================================================================
// 4. JSON Serialization Contracts
// ============================================================================

bool serializeNormalizedTelemetryJson(const NormalizedFlowEvent& flow,
                                      const NormalizedPumpFeedbackEvent& feedback,
                                      const NormalizedPumpStateEvent& state,
                                      char* out_buf,
                                      size_t buf_len) {
    if (!out_buf || buf_len < 256) return false;

    const char* desired_str = (state.desired_state == NodePumpState::ON) ? "ON" : "OFF";
    const char* reported_str = (state.reported_state == NodePumpState::ON) ? "ON" : "OFF";
    const char* sched_str = "UNKNOWN";
    switch (state.schedule_state) {
        case NormalizedScheduleState::SPRAYING: sched_str = "SPRAYING"; break;
        case NormalizedScheduleState::COOLING_DOWN: sched_str = "COOLING_DOWN"; break;
        case NormalizedScheduleState::IDLE: sched_str = "IDLE"; break;
        case NormalizedScheduleState::PAUSED: sched_str = "PAUSED"; break;
        default: break;
    }
    const char* ovr_str = (state.override_state == NormalizedOverrideState::OVERRIDE_OFF) ? "OVERRIDE_OFF" :
                          (state.override_state == NormalizedOverrideState::OVERRIDE_ON) ? "OVERRIDE_ON" : "NONE";

    int written = std::snprintf(out_buf, buf_len,
        "{\"node_id\":%u,\"group_id\":%u,\"command_id\":\"%s\","
        "\"state\":{\"desired\":\"%s\",\"reported\":\"%s\",\"schedule\":\"%s\",\"override\":\"%s\"},"
        "\"feedback\":{\"driver\":%u,\"load\":%u,\"mismatch\":%s,\"current_ma\":%u,\"voltage_mv\":%u,\"flags\":%u},"
        "\"flow\":{\"lpm_x100\":%u,\"volume_ml\":%u,\"total_l_x1000\":%u,\"pulses\":%u,\"confirmed\":%s,\"stability_x10\":%u,\"fault\":\"%s\"},"
        "\"diagnostics\":{\"session\":%u,\"seq\":%u,\"node_ts\":%llu,\"gw_ts\":%llu}}",
        flow.node_id, flow.group_id, flow.command_id,
        desired_str, reported_str, sched_str, ovr_str,
        feedback.driver_feedback, feedback.load_feedback, feedback.driver_feedback_mismatch ? "true" : "false",
        feedback.current_ma, feedback.voltage_mv, feedback.fault_flags,
        flow.flow_rate_lpm_x100, flow.delivered_volume_ml, flow.litres_total_x1000, flow.pulse_count,
        flow.flow_confirmed ? "true" : "false", flow.flow_stability_pct_x10, flow.fault_code,
        flow.boot_session_id, flow.rf_seq,
        (unsigned long long)flow.node_timestamp_ms, (unsigned long long)flow.gateway_timestamp_ms);

    return (written > 0 && static_cast<size_t>(written) < buf_len);
}

bool serializeAnalyticsSummaryJson(uint8_t node_id,
                                   uint32_t season_id,
                                   const NodeAnalyticsMetrics& metrics,
                                   char* out_buf,
                                   size_t buf_len) {
    if (!out_buf || buf_len < 384) return false;

    int written = std::snprintf(out_buf, buf_len,
        "{\"node_id\":%u,\"season_id\":%u,\"metrics\":{"
        "\"total_cmds\":%u,\"acked\":%u,\"timed_out\":%u,\"on_cmds\":%u,\"flow_confirmed\":%u,"
        "\"confirm_rate_pct_x100\":%u,\"runtime_ms\":%u,\"volume_ml\":%u,"
        "\"latency\":{\"ack_avg_ms\":%d,\"ack_min_ms\":%d,\"ack_max_ms\":%d,\"flow_avg_ms\":%d,\"flow_min_ms\":%d,\"flow_max_ms\":%d},"
        "\"transport\":{\"frames\":%u,\"retries\":%u,\"retry_rate_x100\":%u,\"loss_x100\":%u,\"crc_err\":%u,\"auth_err\":%u},"
        "\"quality\":{\"stability_x10\":%u,\"override_mismatch\":%u,\"stale_events\":%u,\"stale_ms\":%u,\"fault_lockouts\":%u}}}",
        node_id, season_id,
        metrics.total_commands_sent, metrics.total_commands_acked, metrics.total_commands_timed_out,
        metrics.total_on_commands, metrics.total_flow_confirmed,
        metrics.confirmation_rate_pct_x100, metrics.total_actual_runtime_ms, metrics.total_delivered_volume_ml,
        metrics.avg_command_to_ack_latency_ms, metrics.min_command_to_ack_latency_ms, metrics.max_command_to_ack_latency_ms,
        metrics.avg_flow_start_latency_ms, metrics.min_flow_start_latency_ms, metrics.max_flow_start_latency_ms,
        metrics.total_rf_frames_sent, metrics.total_rf_retries, metrics.retry_rate_pct_x100,
        metrics.packet_loss_pct_x100, metrics.total_rf_crc_errors, metrics.total_rf_auth_errors,
        metrics.flow_stability_avg_pct_x10, metrics.total_schedule_override_mismatches,
        metrics.total_stale_events, metrics.total_stale_duration_ms, metrics.total_fault_lockouts);

    return (written > 0 && static_cast<size_t>(written) < buf_len);
}

bool serializeCommandLifecycleEventJson(const NormalizedPumpCommandEvent& cmd,
                                        char* out_buf,
                                        size_t buf_len) {
    if (!out_buf || buf_len < 256) return false;

    const char* action_str = (cmd.action == NodePumpState::ON) ? "ON" : "OFF";
    const char* outcome_str = "PENDING";
    switch (cmd.outcome) {
        case NormalizedCommandOutcome::ACKED: outcome_str = "ACKED"; break;
        case NormalizedCommandOutcome::FLOW_CONFIRMED: outcome_str = "FLOW_CONFIRMED"; break;
        case NormalizedCommandOutcome::FAULT_NO_ACK: outcome_str = "FAULT_NO_ACK"; break;
        case NormalizedCommandOutcome::FAULT_NO_FLOW: outcome_str = "FAULT_NO_FLOW"; break;
        case NormalizedCommandOutcome::FAULT_UNEXPECTED_FLOW: outcome_str = "FAULT_UNEXPECTED_FLOW"; break;
        case NormalizedCommandOutcome::FAULT_OVER_RANGE: outcome_str = "FAULT_OVER_RANGE"; break;
        case NormalizedCommandOutcome::FAULT_TIMEOUT: outcome_str = "FAULT_TIMEOUT"; break;
        case NormalizedCommandOutcome::REJECTED_INVALID_LEASE: outcome_str = "REJECTED_INVALID_LEASE"; break;
        case NormalizedCommandOutcome::REJECTED_AUTH_FAIL: outcome_str = "REJECTED_AUTH_FAIL"; break;
        case NormalizedCommandOutcome::REJECTED_FAULT_LOCKOUT: outcome_str = "REJECTED_FAULT_LOCKOUT"; break;
        default: break;
    }

    int written = std::snprintf(out_buf, buf_len,
        "{\"command_id\":\"%s\",\"node_id\":%u,\"group_id\":%u,\"action\":\"%s\","
        "\"outcome\":\"%s\",\"retries\":%u,\"ack_lat_ms\":%d,\"flow_lat_ms\":%d,"
        "\"duration_ms\":%d,\"fault_reason\":\"%s\",\"gw_ts\":%llu}",
        cmd.command_id, cmd.node_id, cmd.group_id, action_str,
        outcome_str, cmd.retry_count, cmd.command_to_ack_latency_ms, cmd.flow_start_latency_ms,
        cmd.execution_duration_ms, cmd.fault_reason, (unsigned long long)cmd.gateway_timestamp_ms);

    return (written > 0 && static_cast<size_t>(written) < buf_len);
}
