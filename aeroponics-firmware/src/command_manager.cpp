#include "command_manager.h"
#include <cstring>

CommandManager::CommandManager()
    : registry_(nullptr), transport_(nullptr), boot_session_id_(1), sequence_num_(0),
      next_command_id_(1000), initialized_(false) {
    for (size_t i = 0; i <= MAX_NODES; ++i) {
        node_policies_[i] = NodeLeasePolicy{DEFAULT_RUN_LEASE_MS, DEFAULT_MAX_ON_DURATION_MS};
    }
}

CommandManager::~CommandManager() {}

bool CommandManager::begin(NodeRegistry* registry, IRfTransport* transport) {
    if (registry == nullptr || transport == nullptr) {
        return false;
    }
    registry_ = registry;
    transport_ = transport;
    initialized_ = true;
    return true;
}

bool CommandManager::setNodeLeasePolicy(uint8_t node_id, uint32_t run_lease_ms, uint32_t max_on_duration_ms) {
    if (node_id < 1 || node_id > MAX_NODES) return false;
    if (run_lease_ms == 0 || max_on_duration_ms < run_lease_ms) return false;
    node_policies_[node_id].run_lease_ms = run_lease_ms;
    node_policies_[node_id].max_on_duration_ms = max_on_duration_ms;
    return true;
}

bool CommandManager::getNodeLeasePolicy(uint8_t node_id, uint32_t &out_run_lease_ms, uint32_t &out_max_on_duration_ms) const {
    if (node_id < 1 || node_id > MAX_NODES) return false;
    out_run_lease_ms = node_policies_[node_id].run_lease_ms;
    out_max_on_duration_ms = node_policies_[node_id].max_on_duration_ms;
    return true;
}

uint16_t CommandManager::calculateCrc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (static_cast<uint16_t>(data[i]) << 8);
        for (uint8_t j = 0; j < 8; ++j) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

size_t CommandManager::buildFrame(RfMessageType msg_type, uint8_t target_node_id, uint32_t command_id,
                                  const uint8_t* payload, uint8_t payload_len, uint8_t* out_buffer, size_t buffer_size) {
    size_t header_len = sizeof(RfHeader);
    size_t total_len = header_len + payload_len + 2; // + 2 for CRC-16
    if (buffer_size < total_len || payload_len > 64) {
        return 0;
    }

    RfHeader header;
    header.sof[0] = RF_SOF_BYTE_1;
    header.sof[1] = RF_SOF_BYTE_2;
    header.version = RF_PROTOCOL_VERSION;
    header.message_type = static_cast<uint8_t>(msg_type);
    header.target_node_id = target_node_id;
    header.source_node_id = 0; // Gateway is node 0
    header.boot_session_id = boot_session_id_;
    header.sequence = sequence_num_++;
    header.command_id = command_id;
    header.payload_len = payload_len;

    std::memcpy(out_buffer, &header, header_len);
    if (payload_len > 0 && payload != nullptr) {
        std::memcpy(out_buffer + header_len, payload, payload_len);
    }

    uint16_t crc = calculateCrc16(out_buffer, header_len + payload_len);
    out_buffer[header_len + payload_len] = static_cast<uint8_t>(crc & 0xFF);
    out_buffer[header_len + payload_len + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);

    return total_len;
}

bool CommandManager::parseFrame(const uint8_t* frame_data, size_t frame_len, RfHeader &out_header,
                                 uint8_t* out_payload, uint8_t &out_payload_len) {
    size_t header_len = sizeof(RfHeader);
    if (frame_len < header_len + 2) {
        return false;
    }

    std::memcpy(&out_header, frame_data, header_len);
    if (out_header.sof[0] != RF_SOF_BYTE_1 || out_header.sof[1] != RF_SOF_BYTE_2 ||
        out_header.version != RF_PROTOCOL_VERSION) {
        return false;
    }

    if (frame_len != header_len + out_header.payload_len + 2) {
        return false;
    }

    uint16_t expected_crc = calculateCrc16(frame_data, header_len + out_header.payload_len);
    uint16_t actual_crc = static_cast<uint16_t>(frame_data[header_len + out_header.payload_len]) |
                         (static_cast<uint16_t>(frame_data[header_len + out_header.payload_len + 1]) << 8);

    if (expected_crc != actual_crc) {
        return false;
    }

    out_payload_len = out_header.payload_len;
    if (out_payload_len > 0 && out_payload != nullptr) {
        std::memcpy(out_payload, frame_data + header_len, out_payload_len);
    }

    return true;
}

bool CommandManager::serviceCommandFanout(uint32_t current_time_ms) {
    if (!initialized_ || registry_ == nullptr || transport_ == nullptr) {
        return false;
    }

    bool all_dispatched_successfully = true;
    uint8_t tx_buf[128];
    for (uint8_t node_id = 1; node_id <= MAX_NODES; ++node_id) {
        NodeState state;
        if (!registry_->getNodeState(node_id, state)) continue;

        if (state.group_id == UNASSIGNED_GROUP_ID) continue;

        // Check if command state update is required
        if (state.desired_state != state.reported_state) {
            uint32_t run_lease_ms = DEFAULT_RUN_LEASE_MS;
            uint32_t max_on_ms = DEFAULT_MAX_ON_DURATION_MS;
            getNodeLeasePolicy(node_id, run_lease_ms, max_on_ms);

            SetPumpPayload p;
            p.desired_state = static_cast<uint8_t>(state.desired_state);
            p.run_lease_ms = run_lease_ms;
            p.max_on_duration_ms = max_on_ms;

            uint32_t cid = next_command_id_++;
            size_t frame_len = buildFrame(RfMessageType::SET_PUMP, node_id, cid,
                                          reinterpret_cast<const uint8_t*>(&p), sizeof(p),
                                          tx_buf, sizeof(tx_buf));
            if (frame_len > 0) {
                size_t sent_bytes = transport_->send(tx_buf, frame_len);
                if (sent_bytes != frame_len) {
                    all_dispatched_successfully = false;
                }
            } else {
                all_dispatched_successfully = false;
            }
        }
    }
    return all_dispatched_successfully;
}

bool CommandManager::handleIncomingFrame(const uint8_t* frame, size_t len, uint32_t current_time_ms) {
    if (!initialized_ || registry_ == nullptr) return false;

    RfHeader header;
    uint8_t payload[64];
    uint8_t payload_len = 0;

    if (!parseFrame(frame, len, header, payload, payload_len)) {
        return false;
    }

    uint8_t src_node = header.source_node_id;
    RfMessageType type = static_cast<RfMessageType>(header.message_type);

    if (type == RfMessageType::COMMAND_ACK) {
        if (payload_len >= sizeof(CommandAckPayload)) {
            CommandAckPayload* ack = reinterpret_cast<CommandAckPayload*>(payload);
            NodePumpState rep = (ack->reported_pump_state == 1) ? NodePumpState::ON : NodePumpState::OFF;
            registry_->updateTelemetry(src_node, rep, ack->driver_feedback, 0, 0, current_time_ms);
            return true;
        }
    } else if (type == RfMessageType::TELEMETRY) {
        if (payload_len >= sizeof(TelemetryPayload)) {
            TelemetryPayload* telem = reinterpret_cast<TelemetryPayload*>(payload);
            NodePumpState rep = (telem->reported_pump_state == 1) ? NodePumpState::ON : NodePumpState::OFF;
            registry_->updateTelemetry(src_node, rep, telem->driver_feedback,
                                       telem->flow_lpm_x100, telem->delivered_volume_ml, current_time_ms);
            return true;
        }
    }
    return false;
}
