#include "rf_frame_codec.h"

#include <cstring>

bool RfFrameCodec::isValidProductionRemoteNodeId(uint8_t node_id) {
    return node_id >= RF_MIN_NODE_ID && node_id <= RF_PRODUCTION_MAX_NODE_ID;
}

bool RfFrameCodec::isValidAddress(uint8_t node_id) {
    return node_id == RF_GATEWAY_NODE_ID || isValidProductionRemoteNodeId(node_id);
}

bool RfFrameCodec::isValidMessageType(RfMessageType type) {
    return type >= RfMessageType::PING && type <= RfMessageType::FAULT_REPORT;
}

uint16_t RfFrameCodec::calculateSequenceDistance(uint16_t new_seq, uint16_t last_seq) {
    return static_cast<uint16_t>(new_seq - last_seq);
}

bool RfFrameCodec::isSequenceAdvanceValid(uint16_t new_seq, uint16_t last_seq) {
    const uint16_t dist = calculateSequenceDistance(new_seq, last_seq);
    return dist > 0 && dist <= RF_SEQUENCE_WRAP_WINDOW;
}

namespace {
bool validMetadata(const RfFrameMetadata& metadata) {
    return RfFrameCodec::isValidAddress(metadata.source_node_id) &&
           RfFrameCodec::isValidAddress(metadata.target_node_id) &&
           (metadata.source_node_id == RF_GATEWAY_NODE_ID ||
            RfFrameCodec::isValidProductionRemoteNodeId(metadata.source_node_id)) &&
           metadata.source_node_id != metadata.target_node_id;
}
}

size_t RfFrameCodec::payloadSize(RfMessageType type) {
    switch (type) {
        case RfMessageType::PING: return 4;
        case RfMessageType::PONG: return 4;
        case RfMessageType::SET_PUMP: return 9;
        case RfMessageType::COMMAND_ACK: return 8;
        case RfMessageType::TELEMETRY: return 17;
        case RfMessageType::HEARTBEAT: return 6;
        case RfMessageType::FAULT_REPORT: return 10;
    }
    return 0;
}

size_t RfFrameCodec::nativePayloadSize(RfMessageType type) {
    switch (type) {
        case RfMessageType::PING: return sizeof(PingPayload);
        case RfMessageType::PONG: return sizeof(PongPayload);
        case RfMessageType::SET_PUMP: return sizeof(SetPumpPayload);
        case RfMessageType::COMMAND_ACK: return sizeof(CommandAckPayload);
        case RfMessageType::TELEMETRY: return sizeof(TelemetryPayload);
        case RfMessageType::HEARTBEAT: return sizeof(HeartbeatPayload);
        case RfMessageType::FAULT_REPORT: return sizeof(FaultReportPayload);
    }
    return 0;
}

uint16_t RfFrameCodec::calculateCrc16(const uint8_t* data, size_t len) {
    if (data == nullptr && len != 0) return 0;
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U) ? static_cast<uint16_t>((crc << 1) ^ 0x1021U)
                                  : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

bool RfFrameCodec::encodeHeader(const RfHeader& header, uint8_t* out_wire, size_t out_len) {
    if (out_wire == nullptr || out_len < RF_HEADER_SIZE) return false;
    out_wire[0] = header.sof[0]; out_wire[1] = header.sof[1]; out_wire[2] = header.version;
    out_wire[3] = header.message_type; out_wire[4] = header.target_node_id; out_wire[5] = header.source_node_id;
    writeU32Le(out_wire + 6, header.boot_session_id); writeU16Le(out_wire + 10, header.sequence);
    writeU32Le(out_wire + 12, header.command_id); out_wire[16] = header.payload_len;
    return true;
}

bool RfFrameCodec::decodeHeader(const uint8_t* wire, size_t wire_len, RfHeader& out_header) {
    if (wire == nullptr || wire_len < RF_HEADER_SIZE) return false;
    out_header.sof[0] = wire[0]; out_header.sof[1] = wire[1]; out_header.version = wire[2];
    out_header.message_type = wire[3]; out_header.target_node_id = wire[4]; out_header.source_node_id = wire[5];
    out_header.boot_session_id = readU32Le(wire + 6); out_header.sequence = readU16Le(wire + 10);
    out_header.command_id = readU32Le(wire + 12); out_header.payload_len = wire[16];
    return true;
}

bool RfFrameCodec::encodePayload(RfMessageType type, const void* payload, size_t payload_len,
                                 uint8_t* out_wire, uint8_t& out_wire_len) {
    const size_t expected = payloadSize(type);
    if (!isValidMessageType(type) || payload == nullptr || out_wire == nullptr ||
        payload_len != nativePayloadSize(type)) return false;
    out_wire_len = static_cast<uint8_t>(expected);
    switch (type) {
        case RfMessageType::PING: writeU32Le(out_wire, static_cast<const PingPayload*>(payload)->ping_timestamp_ms); break;
        case RfMessageType::PONG: writeU32Le(out_wire, static_cast<const PongPayload*>(payload)->echo_timestamp_ms); break;
        case RfMessageType::SET_PUMP: { const auto& p = *static_cast<const SetPumpPayload*>(payload); out_wire[0] = p.desired_state; writeU32Le(out_wire + 1, p.run_lease_ms); writeU32Le(out_wire + 5, p.max_on_duration_ms); break; }
        case RfMessageType::COMMAND_ACK: { const auto& p = *static_cast<const CommandAckPayload*>(payload); writeU16Le(out_wire, p.ack_sequence); out_wire[2] = p.ack_outcome; out_wire[3] = p.reported_pump_state; out_wire[4] = p.driver_feedback; std::memcpy(out_wire + 5, p.reserved, 3); break; }
        case RfMessageType::TELEMETRY: { const auto& p = *static_cast<const TelemetryPayload*>(payload); out_wire[0] = p.reported_pump_state; out_wire[1] = p.driver_feedback; writeU16Le(out_wire + 2, p.flow_lpm_x100); writeU32Le(out_wire + 4, p.delivered_volume_ml); writeU32Le(out_wire + 8, p.pulse_count); out_wire[12] = p.fault_flags; writeU32Le(out_wire + 13, p.last_command_id); break; }
        case RfMessageType::HEARTBEAT: { const auto& p = *static_cast<const HeartbeatPayload*>(payload); writeU32Le(out_wire, p.uptime_s); out_wire[4] = static_cast<uint8_t>(p.rssi_dbm); out_wire[5] = p.battery_percent; break; }
        case RfMessageType::FAULT_REPORT: { const auto& p = *static_cast<const FaultReportPayload*>(payload); out_wire[0] = p.fault_code; writeU32Le(out_wire + 1, p.timestamp_ms); out_wire[5] = p.reserved; writeU32Le(out_wire + 6, p.command_id); break; }
    }
    return true;
}

bool RfFrameCodec::decodePayload(RfMessageType type, const uint8_t* wire, size_t wire_len,
                                 void* out_payload, size_t out_payload_len) {
    const size_t expected = payloadSize(type);
    if (!isValidMessageType(type) || wire == nullptr || out_payload == nullptr || wire_len != expected ||
        out_payload_len < nativePayloadSize(type)) return false;
    switch (type) {
        case RfMessageType::PING: static_cast<PingPayload*>(out_payload)->ping_timestamp_ms = readU32Le(wire); break;
        case RfMessageType::PONG: static_cast<PongPayload*>(out_payload)->echo_timestamp_ms = readU32Le(wire); break;
        case RfMessageType::SET_PUMP: *static_cast<SetPumpPayload*>(out_payload) = {wire[0], readU32Le(wire + 1), readU32Le(wire + 5)}; break;
        case RfMessageType::COMMAND_ACK: *static_cast<CommandAckPayload*>(out_payload) = {readU16Le(wire), wire[2], wire[3], wire[4], {wire[5], wire[6], wire[7]}}; break;
        case RfMessageType::TELEMETRY: *static_cast<TelemetryPayload*>(out_payload) = {wire[0], wire[1], readU16Le(wire + 2), readU32Le(wire + 4), readU32Le(wire + 8), wire[12], readU32Le(wire + 13)}; break;
        case RfMessageType::HEARTBEAT: *static_cast<HeartbeatPayload*>(out_payload) = {readU32Le(wire), static_cast<int8_t>(wire[4]), wire[5]}; break;
        case RfMessageType::FAULT_REPORT: *static_cast<FaultReportPayload*>(out_payload) = {wire[0], readU32Le(wire + 1), wire[5], readU32Le(wire + 6)}; break;
    }
    return true;
}

size_t RfFrameCodec::encodeFrame(const RfFrameMetadata& metadata, RfMessageType type,
                                 const void* payload, size_t payload_len,
                                 const uint8_t* psk, size_t psk_len,
                                 uint8_t* out_frame, size_t out_size) {
    const size_t expected_payload_size = payloadSize(type);
    if (!validMetadata(metadata) || !isValidMessageType(type) ||
        payload == nullptr || payload_len != nativePayloadSize(type) ||
        expected_payload_size == 0 ||
        psk == nullptr || psk_len == 0 || out_frame == nullptr || out_size < RF_HEADER_SIZE + payload_len + HMAC_TAG_SIZE + 2) return 0;
    uint8_t wire_payload[RF_MAX_PAYLOAD_SIZE] = {};
    uint8_t wire_payload_len = 0;
    if (!encodePayload(type, payload, payload_len, wire_payload, wire_payload_len) ||
        wire_payload_len != expected_payload_size) {
        return 0;
    }
    RfHeader header{};
    header.message_type = static_cast<uint8_t>(type);
    header.target_node_id = metadata.target_node_id;
    header.source_node_id = metadata.source_node_id;
    header.boot_session_id = metadata.boot_session_id;
    header.sequence = metadata.sequence;
    header.command_id = metadata.command_id;
    header.payload_len = wire_payload_len;
    if (!encodeHeader(header, out_frame, out_size)) return 0;
    std::memcpy(out_frame + RF_HEADER_SIZE, wire_payload, wire_payload_len);
    const size_t signed_len = RF_HEADER_SIZE + wire_payload_len;
    if (!HmacSha256::calculateTruncated(psk, psk_len, out_frame, signed_len, out_frame + signed_len)) return 0;
    writeU16Le(out_frame + signed_len + HMAC_TAG_SIZE, calculateCrc16(out_frame, signed_len + HMAC_TAG_SIZE));
    return signed_len + HMAC_TAG_SIZE + 2;
}

const char* parseErrorToString(ParseError err) {
    switch (err) {
        case ParseError::OK: return "OK";
        case ParseError::NULL_BUFFER: return "NULL_BUFFER";
        case ParseError::FRAME_TOO_SHORT: return "FRAME_TOO_SHORT";
        case ParseError::INVALID_SOF: return "INVALID_SOF";
        case ParseError::UNSUPPORTED_VERSION: return "UNSUPPORTED_VERSION";
        case ParseError::INVALID_MESSAGE_TYPE: return "INVALID_MESSAGE_TYPE";
        case ParseError::INVALID_ADDRESS: return "INVALID_ADDRESS";
        case ParseError::PAYLOAD_LEN_MISMATCH: return "PAYLOAD_LEN_MISMATCH";
        case ParseError::PAYLOAD_EXCEEDS_MAX: return "PAYLOAD_EXCEEDS_MAX";
        case ParseError::CRC_MISMATCH: return "CRC_MISMATCH";
        case ParseError::HMAC_AUTH_FAIL: return "HMAC_AUTH_FAIL";
    }
    return "UNKNOWN_ERROR";
}

ParseError RfFrameCodec::decodeFrameDetailed(const uint8_t* frame, size_t frame_len,
                                             const uint8_t* psk, size_t psk_len,
                                             RfHeader& out_header, void* out_payload,
                                             size_t out_payload_size) {
    if (frame == nullptr || psk == nullptr || psk_len == 0) {
        return ParseError::NULL_BUFFER;
    }
    if (frame_len < RF_HEADER_SIZE + HMAC_TAG_SIZE + 2) {
        return ParseError::FRAME_TOO_SHORT;
    }
    if (!decodeHeader(frame, frame_len, out_header)) {
        return ParseError::FRAME_TOO_SHORT;
    }
    if (out_header.sof[0] != RF_SOF_BYTE_1 || out_header.sof[1] != RF_SOF_BYTE_2) {
        return ParseError::INVALID_SOF;
    }
    if (out_header.version != RF_PROTOCOL_VERSION) {
        return ParseError::UNSUPPORTED_VERSION;
    }
    if (!isValidMessageType(static_cast<RfMessageType>(out_header.message_type))) {
        return ParseError::INVALID_MESSAGE_TYPE;
    }
    if (!isValidAddress(out_header.source_node_id) || !isValidAddress(out_header.target_node_id) ||
        (out_header.source_node_id != RF_GATEWAY_NODE_ID &&
         !isValidProductionRemoteNodeId(out_header.source_node_id)) ||
        out_header.source_node_id == out_header.target_node_id) {
        return ParseError::INVALID_ADDRESS;
    }
    if (out_header.payload_len > RF_MAX_PAYLOAD_SIZE) {
        return ParseError::PAYLOAD_EXCEEDS_MAX;
    }
    const size_t expected_payload_len = payloadSize(static_cast<RfMessageType>(out_header.message_type));
    if (out_header.payload_len != expected_payload_len) {
        return ParseError::PAYLOAD_LEN_MISMATCH;
    }
    const size_t total_expected_len = RF_HEADER_SIZE + out_header.payload_len + HMAC_TAG_SIZE + 2;
    if (frame_len != total_expected_len) {
        return ParseError::FRAME_TOO_SHORT;
    }
    const size_t signed_len = RF_HEADER_SIZE + out_header.payload_len;
    if (calculateCrc16(frame, signed_len + HMAC_TAG_SIZE) != readU16Le(frame + signed_len + HMAC_TAG_SIZE)) {
        return ParseError::CRC_MISMATCH;
    }
    uint8_t expected_mac[HMAC_TAG_SIZE] = {};
    if (!HmacSha256::calculateTruncated(psk, psk_len, frame, signed_len, expected_mac) ||
        !constantTimeCompare(expected_mac, frame + signed_len, HMAC_TAG_SIZE)) {
        return ParseError::HMAC_AUTH_FAIL;
    }
    if (!decodePayload(static_cast<RfMessageType>(out_header.message_type), frame + RF_HEADER_SIZE,
                       out_header.payload_len, out_payload, out_payload_size)) {
        return ParseError::PAYLOAD_LEN_MISMATCH;
    }
    return ParseError::OK;
}

bool RfFrameCodec::decodeFrame(const uint8_t* frame, size_t frame_len,
                               const uint8_t* psk, size_t psk_len,
                               RfHeader& out_header, void* out_payload,
                               size_t out_payload_size) {
    return decodeFrameDetailed(frame, frame_len, psk, psk_len, out_header, out_payload, out_payload_size) == ParseError::OK;
}

#if !defined(ATMEGA8_NODE_BUILD)

DuplicateResponseCache::DuplicateResponseCache() : head_(0), count_(0) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    mutex_ = xSemaphoreCreateMutex();
#endif
    clear();
}

DuplicateResponseCache::~DuplicateResponseCache() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
#endif
}

bool DuplicateResponseCache::put(uint8_t node_id, uint32_t boot_session_id, uint16_t sequence,
                                 uint32_t command_id, uint8_t message_type,
                                 const uint8_t* payload, uint8_t payload_len, uint32_t timestamp_ms) {
    if (payload_len > RF_MAX_PAYLOAD_SIZE) return false;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) return false;
#else
    std::lock_guard<std::mutex> lock(mutex_);
#endif

    // Check if key already exists, update if so
    for (size_t i = 0; i < count_; ++i) {
        const size_t idx = (head_ + DUPLICATE_CACHE_DEFAULT_CAPACITY - count_ + i) % DUPLICATE_CACHE_DEFAULT_CAPACITY;
        if (entries_[idx].valid &&
            entries_[idx].node_id == node_id &&
            entries_[idx].boot_session_id == boot_session_id &&
            entries_[idx].sequence == sequence &&
            entries_[idx].command_id == command_id) {
            entries_[idx].message_type = message_type;
            entries_[idx].payload_len = payload_len;
            if (payload != nullptr && payload_len > 0) {
                std::memcpy(entries_[idx].payload, payload, payload_len);
            }
            entries_[idx].timestamp_ms = timestamp_ms;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
            xSemaphoreGive(mutex_);
#endif
            return true;
        }
    }

    // Insert new entry at head_ (FIFO overwrite)
    CachedResponseEntry& entry = entries_[head_];
    entry.node_id = node_id;
    entry.boot_session_id = boot_session_id;
    entry.sequence = sequence;
    entry.command_id = command_id;
    entry.message_type = message_type;
    entry.payload_len = payload_len;
    if (payload != nullptr && payload_len > 0) {
        std::memcpy(entry.payload, payload, payload_len);
    } else {
        std::memset(entry.payload, 0, sizeof(entry.payload));
    }
    entry.timestamp_ms = timestamp_ms;
    entry.valid = true;

    head_ = (head_ + 1) % DUPLICATE_CACHE_DEFAULT_CAPACITY;
    if (count_ < DUPLICATE_CACHE_DEFAULT_CAPACITY) {
        count_++;
    }

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(mutex_);
#endif
    return true;
}

bool DuplicateResponseCache::get(uint8_t node_id, uint32_t boot_session_id, uint16_t sequence,
                                 uint32_t command_id, CachedResponseEntry& out_entry) const {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) return false;
#else
    std::lock_guard<std::mutex> lock(mutex_);
#endif

    for (size_t i = 0; i < count_; ++i) {
        const size_t idx = (head_ + DUPLICATE_CACHE_DEFAULT_CAPACITY - 1 - i) % DUPLICATE_CACHE_DEFAULT_CAPACITY;
        if (entries_[idx].valid &&
            entries_[idx].node_id == node_id &&
            entries_[idx].boot_session_id == boot_session_id &&
            entries_[idx].sequence == sequence &&
            entries_[idx].command_id == command_id) {
            out_entry = entries_[idx];
#if defined(ESP_PLATFORM) || defined(ARDUINO)
            xSemaphoreGive(mutex_);
#endif
            return true;
        }
    }

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(mutex_);
#endif
    return false;
}

bool DuplicateResponseCache::contains(uint8_t node_id, uint32_t boot_session_id, uint16_t sequence,
                                      uint32_t command_id) const {
    CachedResponseEntry dummy;
    return get(node_id, boot_session_id, sequence, command_id, dummy);
}

void DuplicateResponseCache::clear() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ != nullptr) (void)xSemaphoreTake(mutex_, pdMS_TO_TICKS(50));
#else
    std::lock_guard<std::mutex> lock(mutex_);
#endif

    for (size_t i = 0; i < DUPLICATE_CACHE_DEFAULT_CAPACITY; ++i) {
        entries_[i] = CachedResponseEntry{};
    }
    head_ = 0;
    count_ = 0;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ != nullptr) xSemaphoreGive(mutex_);
#endif
}

void DuplicateResponseCache::invalidateNode(uint8_t node_id) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ != nullptr) (void)xSemaphoreTake(mutex_, pdMS_TO_TICKS(50));
#else
    std::lock_guard<std::mutex> lock(mutex_);
#endif

    for (size_t i = 0; i < DUPLICATE_CACHE_DEFAULT_CAPACITY; ++i) {
        if (entries_[i].node_id == node_id) {
            entries_[i].valid = false;
        }
    }

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ != nullptr) xSemaphoreGive(mutex_);
#endif
}

size_t DuplicateResponseCache::size() const {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (mutex_ == nullptr || xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) return count_;
#else
    std::lock_guard<std::mutex> lock(mutex_);
#endif
    const size_t c = count_;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xSemaphoreGive(mutex_);
#endif
    return c;
}

#endif // !defined(ATMEGA8_NODE_BUILD)
