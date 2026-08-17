#include "node_command_processor.h"
#include <cstring>
#include <cstdio>

NodeCommandProcessor::NodeCommandProcessor()
    : node_id_(0),
      transport_(nullptr),
      driver_(nullptr),
      audit_sink_(nullptr),
      boot_session_id_(1),
      tx_sequence_(0),
      initialized_(false),
      psk_valid_(false),
      last_gw_boot_session_id_(0),
      last_gw_sequence_(0),
      gw_session_initialized_(false),
      cached_ack_valid_(false),
      cached_gw_boot_session_id_(0),
      cached_gw_sequence_(0),
      cached_gw_command_id_(0),
      cached_ack_payload_{},
      reported_pump_state_(0),
      driver_feedback_(0),
      lease_active_(false),
      lease_start_ms_(0),
      lease_duration_ms_(0),
      max_on_duration_ms_(0),
      current_command_id_(0),
      fault_latched_(false),
      fault_code_(0),
      fault_flags_(0),
      last_telemetry_ms_(0),
      last_heartbeat_ms_(0) {
    std::memset(psk_key_, 0, sizeof(psk_key_));
}

NodeCommandProcessor::~NodeCommandProcessor() {}

bool NodeCommandProcessor::begin(uint8_t node_id, IRfTransport* transport, IPumpActuatorDriver* driver,
                                 const uint8_t* psk, size_t psk_len, uint32_t boot_session_id) {
    if (node_id < RF_MIN_NODE_ID || node_id > RF_MAX_NODE_ID ||
        transport == nullptr || driver == nullptr ||
        psk == nullptr || psk_len != sizeof(psk_key_) || boot_session_id == 0) {
        return false;
    }

    // Enforce boot-safe output: pump actuator MUST be driven OFF before RF or application initialization
    driver_ = driver;
    driver_->setPumpOutput(false);

    node_id_ = node_id;
    transport_ = transport;
    boot_session_id_ = boot_session_id;
    tx_sequence_ = 0;

    std::memcpy(psk_key_, psk, sizeof(psk_key_));
    psk_valid_ = true;

    // Reset anti-replay tracking
    last_gw_boot_session_id_ = 0;
    last_gw_sequence_ = 0;
    gw_session_initialized_ = false;

    // Reset duplicate cache
    cached_ack_valid_ = false;
    cached_gw_boot_session_id_ = 0;
    cached_gw_sequence_ = 0;
    cached_gw_command_id_ = 0;
    std::memset(&cached_ack_payload_, 0, sizeof(cached_ack_payload_));

    // Reset state
    reported_pump_state_ = 0;
    driver_feedback_ = driver_->readDriverSense() ? 1 : 0;
    lease_active_ = false;
    lease_start_ms_ = 0;
    lease_duration_ms_ = 0;
    max_on_duration_ms_ = 0;
    current_command_id_ = 0;
    fault_latched_ = false;
    fault_code_ = 0;
    fault_flags_ = 0;
    last_telemetry_ms_ = 0;
    last_heartbeat_ms_ = 0;

    initialized_ = true;
    return true;
}

bool NodeCommandProcessor::transmitFrame(RfMessageType msg_type, uint32_t command_id,
                                         const void* payload, size_t payload_len) {
    if (!initialized_ || !psk_valid_ || transport_ == nullptr) return false;

    ++tx_sequence_;
    RfFrameMetadata metadata(node_id_, RF_GATEWAY_NODE_ID, boot_session_id_, tx_sequence_, command_id);

    uint8_t out_frame[RF_MAX_FRAME_SIZE] = {};
    const size_t frame_len = RfFrameCodec::encodeFrame(metadata, msg_type, payload, payload_len,
                                                       psk_key_, sizeof(psk_key_),
                                                       out_frame, sizeof(out_frame));
    if (frame_len == 0) return false;

    return transport_->send(out_frame, frame_len);
}

bool NodeCommandProcessor::processIncomingFrame(const uint8_t* frame_data, size_t frame_len,
                                                uint32_t current_time_ms) {
    if (!initialized_ || !psk_valid_ || frame_data == nullptr || frame_len < RF_HEADER_SIZE) {
        return false;
    }

    RfHeader header{};
    uint8_t raw_payload[RF_MAX_PAYLOAD_SIZE] = {};
    if (!RfFrameCodec::decodeFrame(frame_data, frame_len, psk_key_, sizeof(psk_key_),
                                   header, raw_payload, sizeof(raw_payload))) {
        return false; // Authentication, CRC, or framing failed -> fail-closed
    }

    // Addressing check: target must be this node, source must be gateway (0)
    if (header.target_node_id != node_id_ || header.source_node_id != RF_GATEWAY_NODE_ID) {
        return false;
    }

    // Anti-replay & Boot Session Verification
    if (!gw_session_initialized_ || header.boot_session_id > last_gw_boot_session_id_) {
        // Gateway rebooted or fresh session
        last_gw_boot_session_id_ = header.boot_session_id;
        last_gw_sequence_ = header.sequence;
        gw_session_initialized_ = true;
        cached_ack_valid_ = false; // Invalidate duplicate cache across session changes
    } else if (header.boot_session_id < last_gw_boot_session_id_) {
        // Replay from older session -> drop
        return false;
    } else {
        // Same boot session
        // Check for exact duplicate command (idempotency)
        if (cached_ack_valid_ &&
            header.sequence == cached_gw_sequence_ &&
            header.command_id == cached_gw_command_id_) {
            // Re-transmit cached outcome without re-actuating hardware or resetting lease!
            transmitFrame(RfMessageType::COMMAND_ACK, header.command_id,
                          &cached_ack_payload_, sizeof(cached_ack_payload_));
            return true;
        }

        // Monotonic sequence distance check
        const uint16_t distance = RfFrameCodec::calculateSequenceDistance(header.sequence, last_gw_sequence_);
        if (distance >= 1 && distance <= RF_SEQUENCE_WRAP_WINDOW) {
            last_gw_sequence_ = header.sequence;
        } else {
            // Duplicate sequence (distance == 0) or out-of-order/replay -> drop fail-closed
            return false;
        }
    }

    // Dispatch message types
    switch (static_cast<RfMessageType>(header.message_type)) {
        case RfMessageType::PING: {
            PingPayload ping{};
            if (!RfFrameCodec::decodePayload(RfMessageType::PING, raw_payload, header.payload_len,
                                             &ping, sizeof(ping))) {
                return false;
            }
            return handlePing(header, ping);
        }
        case RfMessageType::SET_PUMP: {
            SetPumpPayload set_pump{};
            if (!RfFrameCodec::decodePayload(RfMessageType::SET_PUMP, raw_payload, header.payload_len,
                                             &set_pump, sizeof(set_pump))) {
                return false;
            }
            return handleSetPump(header, set_pump, current_time_ms);
        }
        default:
            return false;
    }
}

bool NodeCommandProcessor::handlePing(const RfHeader& header, const PingPayload& payload) {
    PongPayload pong{payload.ping_timestamp_ms};
    return transmitFrame(RfMessageType::PONG, header.command_id, &pong, sizeof(pong));
}

bool NodeCommandProcessor::handleSetPump(const RfHeader& header, const SetPumpPayload& payload,
                                         uint32_t current_time_ms) {
    if (payload.desired_state > 1) {
        return false;
    }

    if (payload.desired_state == 1) {
        // ON request
        if (payload.run_lease_ms == 0 || payload.max_on_duration_ms < payload.run_lease_ms) {
            // Invalid lease rejected
            cached_ack_payload_.ack_sequence = header.sequence;
            cached_ack_payload_.ack_outcome = static_cast<uint8_t>(AckOutcome::REJECTED_INVALID_LEASE);
            cached_ack_payload_.reported_pump_state = reported_pump_state_;
            cached_ack_payload_.driver_feedback = driver_feedback_;
            std::memset(cached_ack_payload_.reserved, 0, sizeof(cached_ack_payload_.reserved));

            cached_gw_boot_session_id_ = header.boot_session_id;
            cached_gw_sequence_ = header.sequence;
            cached_gw_command_id_ = header.command_id;
            cached_ack_valid_ = true;

            transmitFrame(RfMessageType::COMMAND_ACK, header.command_id,
                          &cached_ack_payload_, sizeof(cached_ack_payload_));
            return true;
        }

        if (fault_latched_) {
            // Node in fault lockout
            cached_ack_payload_.ack_sequence = header.sequence;
            cached_ack_payload_.ack_outcome = static_cast<uint8_t>(AckOutcome::FAULT_LOCKOUT);
            cached_ack_payload_.reported_pump_state = reported_pump_state_;
            cached_ack_payload_.driver_feedback = driver_feedback_;
            std::memset(cached_ack_payload_.reserved, 0, sizeof(cached_ack_payload_.reserved));

            cached_gw_boot_session_id_ = header.boot_session_id;
            cached_gw_sequence_ = header.sequence;
            cached_gw_command_id_ = header.command_id;
            cached_ack_valid_ = true;

            transmitFrame(RfMessageType::COMMAND_ACK, header.command_id,
                          &cached_ack_payload_, sizeof(cached_ack_payload_));
            return true;
        }

        // Execute pump ON
        driver_->setPumpOutput(true);
        driver_feedback_ = driver_->readDriverSense() ? 1 : 0;
        reported_pump_state_ = 1;

        // Initialize lease deadman safety timers
        lease_active_ = true;
        lease_start_ms_ = current_time_ms;
        lease_duration_ms_ = payload.run_lease_ms;
        max_on_duration_ms_ = payload.max_on_duration_ms;
        current_command_id_ = header.command_id;

        // Form SUCCESS ACK
        cached_ack_payload_.ack_sequence = header.sequence;
        cached_ack_payload_.ack_outcome = static_cast<uint8_t>(AckOutcome::SUCCESS);
        cached_ack_payload_.reported_pump_state = reported_pump_state_;
        cached_ack_payload_.driver_feedback = driver_feedback_;
        std::memset(cached_ack_payload_.reserved, 0, sizeof(cached_ack_payload_.reserved));

        cached_gw_boot_session_id_ = header.boot_session_id;
        cached_gw_sequence_ = header.sequence;
        cached_gw_command_id_ = header.command_id;
        cached_ack_valid_ = true;

        transmitFrame(RfMessageType::COMMAND_ACK, header.command_id,
                      &cached_ack_payload_, sizeof(cached_ack_payload_));
        return true;
    } else {
        // OFF request (safe-off is always accepted regardless of fault status)
        driver_->setPumpOutput(false);
        driver_feedback_ = driver_->readDriverSense() ? 1 : 0;
        reported_pump_state_ = 0;
        lease_active_ = false;
        current_command_id_ = header.command_id;

        cached_ack_payload_.ack_sequence = header.sequence;
        cached_ack_payload_.ack_outcome = static_cast<uint8_t>(AckOutcome::SUCCESS);
        cached_ack_payload_.reported_pump_state = reported_pump_state_;
        cached_ack_payload_.driver_feedback = driver_feedback_;
        std::memset(cached_ack_payload_.reserved, 0, sizeof(cached_ack_payload_.reserved));

        cached_gw_boot_session_id_ = header.boot_session_id;
        cached_gw_sequence_ = header.sequence;
        cached_gw_command_id_ = header.command_id;
        cached_ack_valid_ = true;

        transmitFrame(RfMessageType::COMMAND_ACK, header.command_id,
                      &cached_ack_payload_, sizeof(cached_ack_payload_));
        return true;
    }
}

void NodeCommandProcessor::forceSafeOff(const char* reason) {
    if (driver_ != nullptr) {
        driver_->setPumpOutput(false);
        driver_feedback_ = driver_->readDriverSense() ? 1 : 0;
    }
    reported_pump_state_ = 0;
    lease_active_ = false;

    if (audit_sink_ != nullptr && reason != nullptr) {
        audit_sink_->logSafetyEvent(reason, "Pump forced safe-off");
    }
}

bool NodeCommandProcessor::service(uint32_t current_time_ms) {
    if (!initialized_) return false;

    // 1. Lease Deadman Safety Check
    if (reported_pump_state_ == 1 && lease_active_) {
        const uint32_t elapsed = current_time_ms - lease_start_ms_;
        if (elapsed >= lease_duration_ms_ || elapsed >= max_on_duration_ms_) {
            // Deadman timeout triggered! Force safe-off independently of gateway
            forceSafeOff("LEASE_EXPIRED_SAFE_OFF");
            fault_latched_ = true;
            fault_code_ = 3; // LEASE_EXPIRED
            fault_flags_ |= 0x04; // Bit 2: LEASE_EXPIRED

            // Send asynchronous fault report
            sendFaultReport(fault_code_, current_time_ms, current_command_id_);
            sendTelemetry(current_time_ms);
            return true;
        }
    }

    // 2. Periodic Telemetry
    const uint32_t telemetry_interval = (reported_pump_state_ == 1) ? 1000 : 10000;
    if (last_telemetry_ms_ == 0 || (current_time_ms - last_telemetry_ms_) >= telemetry_interval) {
        sendTelemetry(current_time_ms);
        last_telemetry_ms_ = current_time_ms;
    }

    // 3. Periodic Heartbeat (when pump is OFF)
    if (reported_pump_state_ == 0 &&
        (last_heartbeat_ms_ == 0 || (current_time_ms - last_heartbeat_ms_) >= RF_HEARTBEAT_INTERVAL_MS)) {
        sendHeartbeat(current_time_ms);
        last_heartbeat_ms_ = current_time_ms;
    }

    return true;
}

bool NodeCommandProcessor::sendTelemetry(uint32_t current_time_ms) {
    if (!initialized_ || !psk_valid_) return false;

    TelemetryPayload payload{};
    payload.reported_pump_state = reported_pump_state_;
    payload.driver_feedback = driver_feedback_;
    payload.flow_lpm_x100 = driver_ ? driver_->readFlowLpmX100() : 0;
    payload.delivered_volume_ml = driver_ ? driver_->readDeliveredVolumeMl() : 0;
    payload.pulse_count = driver_ ? driver_->readPulseCount() : 0;
    payload.fault_flags = fault_flags_;
    payload.last_command_id = current_command_id_;

    return transmitFrame(RfMessageType::TELEMETRY, current_command_id_, &payload, sizeof(payload));
}

bool NodeCommandProcessor::sendHeartbeat(uint32_t current_time_ms) {
    if (!initialized_ || !psk_valid_) return false;

    HeartbeatPayload payload{};
    payload.uptime_s = current_time_ms / 1000;
    payload.rssi_dbm = -60;
    payload.battery_percent = 255; // Mains / AC powered

    return transmitFrame(RfMessageType::HEARTBEAT, 0, &payload, sizeof(payload));
}

bool NodeCommandProcessor::sendFaultReport(uint8_t fault_code, uint32_t current_time_ms, uint32_t command_id) {
    if (!initialized_ || !psk_valid_) return false;

    FaultReportPayload payload{};
    payload.fault_code = fault_code;
    payload.timestamp_ms = current_time_ms;
    payload.reserved = 0;
    payload.command_id = command_id;

    return transmitFrame(RfMessageType::FAULT_REPORT, command_id, &payload, sizeof(payload));
}

bool NodeCommandProcessor::resetFault() {
    fault_latched_ = false;
    fault_code_ = 0;
    fault_flags_ = 0;
    return true;
}

void NodeCommandProcessor::latchFault(uint8_t fault_code, uint32_t current_time_ms, const char* reason) {
    fault_latched_ = true;
    fault_code_ = fault_code;
    fault_flags_ |= (1U << (fault_code - 1));
    forceSafeOff(reason ? reason : "MANUAL_FAULT_LATCH");
    sendFaultReport(fault_code, current_time_ms, current_command_id_);
}

uint32_t NodeCommandProcessor::getLeaseRemainingMs(uint32_t current_time_ms) const {
    if (!lease_active_ || reported_pump_state_ == 0) return 0;
    const uint32_t elapsed = current_time_ms - lease_start_ms_;
    if (elapsed >= lease_duration_ms_) return 0;
    return lease_duration_ms_ - elapsed;
}
