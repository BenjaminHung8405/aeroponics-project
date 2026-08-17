#include <unity.h>
#include <chrono>
#include <thread>
#include <cstring>
#include "config.h"
#include "nvs_storage.h"
#include "fakes/FakeClock.h"
#include "mqtt_task_policy.h"
#include "mqtt_lifecycle.h"
#include "fakes/FakeWatchdog.h"
#include "fakes/FakeNvsBackend.h"
#include "mqtt_client.h"
#include "mqtt_config_provider.h"
#include "fakes/FakeRfTransport.h"
#include "node_registry.h"
#include "group_schedule_manager.h"
#include "command_manager.h"
#include "rf_provisioning.h"
#include "pump_feedback_evaluator.h"
#include "flow_calibration.h"
#include "uart_rf_transport.h"
#include "node_command_processor.h"

void setUp(void) {}
void tearDown(void) {}

bool provisionTestPsk(CommandManager& manager) {
    const uint8_t test_psk[16] = {0xA5};
    return manager.setPskKey(test_psk, sizeof(test_psk));
}

bool provisionTestNodePolicy(CommandManager& manager, uint8_t node_id, uint16_t min_flow = 50,
                             uint16_t max_off_flow = 20, uint16_t max_flow = 600,
                             uint32_t flow_timeout_ms = 3000) {
    const FlowPolicyProvenance provenance{1, static_cast<uint32_t>(100 + node_id),
                                          static_cast<uint32_t>(1000 + node_id)};
    return manager.provisionNodeControlPolicy(node_id, 60000, 300000, min_flow, max_off_flow,
                                              max_flow, flow_timeout_ms, provenance);
}

size_t buildAuthenticatedNodeAck(CommandManager& manager, const RfHeader& request, uint8_t reported_state,
                                 uint8_t driver_feedback, uint8_t* out_frame, size_t out_size) {
    (void) manager;
    CommandAckPayload ack{request.sequence, static_cast<uint8_t>(AckOutcome::SUCCESS), reported_state,
                          driver_feedback, {0, 0, 0}};
    const uint8_t key[16] = {0xA5};
    const RfFrameMetadata metadata{request.target_node_id, 0, request.boot_session_id, request.sequence,
                                   request.command_id};
    return RfFrameCodec::encodeFrame(metadata, RfMessageType::COMMAND_ACK, &ack, sizeof(ack),
                                     key, sizeof(key), out_frame, out_size);
}

size_t buildAuthenticatedNodeFrame(CommandManager& manager, RfMessageType type, uint32_t boot_session_id,
                                   uint16_t sequence, uint32_t command_id, const void* payload,
    uint8_t payload_len, uint8_t* out_frame, size_t out_size) {
    (void) manager;
    const uint8_t key[16] = {0xA5};
    const RfFrameMetadata metadata{1, 0, boot_session_id, sequence, command_id};
    return RfFrameCodec::encodeFrame(metadata, type, payload, payload_len, key, sizeof(key),
                                     out_frame, out_size);
}

class FakeOutcomeSink final : public ICommandOutcomeSink {
public:
    void publishCommandOutcome(const char*, const char* status, uint8_t, const char*) override {
        std::strncpy(last_status, status, sizeof(last_status) - 1);
        last_status[sizeof(last_status) - 1] = '\0';
    }
    void publishSafetyAudit(const char* event, const char*) override {
        std::strncpy(last_audit, event, sizeof(last_audit) - 1);
        last_audit[sizeof(last_audit) - 1] = '\0';
    }

    char last_status[32] = {};
    char last_audit[64] = {};
};

bool acknowledgePumpCommand(CommandManager& manager, const RfHeader& request, uint32_t timestamp_ms) {
    uint8_t ack_frame[128] = {};
    const size_t ack_len = buildAuthenticatedNodeAck(manager, request, 1, 1, ack_frame, sizeof(ack_frame));
    return manager.handleIncomingFrame(ack_frame, ack_len, timestamp_ms);
}

void test_fake_clock_night_mode(void) {
    FakeClock clock_day(12, true);
    TEST_ASSERT_TRUE(clock_day.isDayMode());
    TEST_ASSERT_TRUE(clock_day.getTime().is_valid);

    FakeClock clock_night(22, true);
    TEST_ASSERT_FALSE(clock_night.isDayMode());
    TEST_ASSERT_TRUE(clock_night.getTime().is_valid);
}

void test_nvs_storage_basic_init_and_reset(void) {
    FakeNvsBackend backend;
    NvsStorage storage(&backend);
    TEST_ASSERT_TRUE(storage.begin());
    TEST_ASSERT_TRUE(storage.factoryReset());
}

void test_mqtt_client_connect_and_lwt(void) {
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "dev-1", "pass", "dev-1"};
    FakeClock clock(12, true);
    clock.setUnixTime(1700000000);

    TEST_ASSERT_TRUE(mqtt.begin(cfg, &clock));
    TEST_ASSERT_TRUE(mqtt.connect());
    TEST_ASSERT_TRUE(mqtt.isConnected());

    TEST_ASSERT_EQUAL_STRING("aeroponics/device/dev-1/status", mqtt.mockLastPublishedTopic());
    TEST_ASSERT_TRUE(strstr(mqtt.mockLastPublishedPayload(), "\"status\":\"online\"") != nullptr);
}

void test_mqtt_client_publish_heartbeat(void) {
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "dev-1", "pass", "dev-1"};
    FakeClock clock(12, true);
    clock.setUnixTime(1700000000);

    TEST_ASSERT_TRUE(mqtt.begin(cfg, &clock));
    TEST_ASSERT_TRUE(mqtt.connect());
    TEST_ASSERT_TRUE(mqtt.publishHeartbeat());

    TEST_ASSERT_EQUAL_STRING("aeroponics/device/dev-1/status", mqtt.mockLastPublishedTopic());
    TEST_ASSERT_TRUE(strstr(mqtt.mockLastPublishedPayload(), "\"status\":\"online\"") != nullptr);
}

void test_mqtt_begin_invalid_config_resets_previous_connection(void) {
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "dev-1", "pass", "dev-1"};
    TEST_ASSERT_TRUE(mqtt.begin(cfg));
    TEST_ASSERT_TRUE(mqtt.connect());

    MqttConfig invalid_cfg{"mqtt.local", 1883, "user", "pass", ""};
    TEST_ASSERT_FALSE(mqtt.begin(invalid_cfg));
    TEST_ASSERT_FALSE(mqtt.isInitialized());
    TEST_ASSERT_FALSE(mqtt.isConnected());
}

void test_mqtt_command_validation_rejects_untrusted_input(void) {
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "dev-1", "pass", "dev-1"};
    NodeRegistry registry;
    TEST_ASSERT_TRUE(mqtt.begin(cfg, nullptr, &registry));
    TEST_ASSERT_TRUE(mqtt.connect());

    char topic[] = "aeroponics/device/dev-1/command/config/assignment";
    char malformed_payload[] = "{invalid_json}";
    mqtt.simulateIncomingMessage(topic, (uint8_t*)malformed_payload, strlen(malformed_payload));

    TEST_ASSERT_EQUAL_UINT8(0, registry.getNodeGroup(1));
}

void test_mqtt_callback_enforces_payload_length_contract(void) {
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "dev-1", "pass", "dev-1"};
    TEST_ASSERT_TRUE(mqtt.begin(cfg));
    TEST_ASSERT_TRUE(mqtt.connect());

    char topic[] = "aeroponics/device/dev-1/command/config/assignment";
    char dummy_payload[10] = "{}";
    mqtt.simulateIncomingMessage(topic, (uint8_t*)dummy_payload, 50000);
}

void test_mqtt_topic_full_match_and_missing_command_id_nack(void) {
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "dev-1", "pass", "dev-1"};
    NodeRegistry registry;
    TEST_ASSERT_TRUE(mqtt.begin(cfg, nullptr, &registry));
    TEST_ASSERT_TRUE(mqtt.connect());

    // 1. Partial/Suffix topic must be IGNORED
    char bad_topic[] = "aeroponics/device/dev-1/command/config/assignment_extra";
    char payload1[] = "{\"command_id\":\"cmd-1\",\"node_id\":1,\"group_id\":1}";
    mqtt.simulateIncomingMessage(bad_topic, (uint8_t*)payload1, strlen(payload1));
    TEST_ASSERT_EQUAL_UINT8(0, registry.getNodeGroup(1));

    // 2. Missing command_id sends REJECTED ACK
    char good_topic[] = "aeroponics/device/dev-1/command/config/assignment";
    char no_cmd_id_payload[] = "{\"node_id\":1,\"group_id\":1}";
    mqtt.simulateIncomingMessage(good_topic, (uint8_t*)no_cmd_id_payload, strlen(no_cmd_id_payload));
    TEST_ASSERT_EQUAL_UINT8(0, registry.getNodeGroup(1));
    TEST_ASSERT_TRUE(strstr(mqtt.mockLastPublishedPayload(), "\"status\":\"REJECTED\"") != nullptr);
}

void test_mqtt_config_provider_load(void) {
    MqttConfig cfg = MqttConfigProvider::load();
    TEST_ASSERT_NOT_NULL(cfg.broker_host);
    TEST_ASSERT_NOT_NULL(cfg.device_id);
}

void test_mqtt_config_rejects_unsafe_device_id(void) {
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "dev-invalid", "pass", "dev/invalid#id"};
    TEST_ASSERT_FALSE(mqtt.begin(cfg));
}

void test_mqtt_connect_is_atomic_on_publish_or_subscribe_failure(void) {
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "dev-1", "pass", "dev-1"};
    TEST_ASSERT_TRUE(mqtt.begin(cfg));
    mqtt.setMockPublishResult(false);
    TEST_ASSERT_FALSE(mqtt.connect());
    TEST_ASSERT_FALSE(mqtt.isConnected());
}

void test_mqtt_subscribes_flow_policy_and_rolls_back_on_subscription_failure(void) {
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "dev-1", "pass", "dev-1"};
    TEST_ASSERT_TRUE(mqtt.begin(cfg));
    TEST_ASSERT_TRUE(mqtt.connect());
    TEST_ASSERT_TRUE(mqtt.mockWasSubscribedTo(
        "aeroponics/device/dev-1/command/config/flow-policy"));

    MqttClient failed_mqtt;
    TEST_ASSERT_TRUE(failed_mqtt.begin(cfg));
    failed_mqtt.setMockSubscribeResult(false);
    TEST_ASSERT_FALSE(failed_mqtt.connect());
    TEST_ASSERT_FALSE(failed_mqtt.isConnected());
}

void test_mqtt_task_create_failure_rolls_back_facade_state(void) {
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "dev-1", "pass", "dev-1"};
    TEST_ASSERT_TRUE(mqtt.begin(cfg));
    mqtt.reset();
    TEST_ASSERT_FALSE(mqtt.isInitialized());
}

void test_mqtt_reconnect_backoff_logic(void) {
    MqttTaskState state{};
    TEST_ASSERT_EQUAL_UINT32(MQTT_RECONNECT_BASE_S, state.backoff_s);
    mqttRecordReconnectFailure(state);
    TEST_ASSERT_EQUAL_UINT32(2, state.backoff_s);
}

void test_mqtt_heartbeat_publish_result_controls_deadline(void) {
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "dev-1", "pass", "dev-1"};
    TEST_ASSERT_TRUE(mqtt.begin(cfg));
    TEST_ASSERT_TRUE(mqtt.connect());
    mqtt.setMockPublishResult(false);
    TEST_ASSERT_FALSE(mqtt.publishHeartbeat());
}

void test_rf_transport_interface_and_fake(void) {
    FakeRfTransport rf;
    TEST_ASSERT_TRUE(rf.begin());
    uint8_t tx_data[] = {0xAA, 0x55, 0x01};
    TEST_ASSERT_EQUAL_UINT32(3, rf.send(tx_data, sizeof(tx_data)));

    rf.injectRxData(tx_data, sizeof(tx_data));
    TEST_ASSERT_EQUAL_UINT32(3, rf.available());
    uint8_t rx_buf[16] = {};
    size_t rx_len = rf.receive(rx_buf, sizeof(rx_buf));
    TEST_ASSERT_EQUAL_UINT32(3, rx_len);
    TEST_ASSERT_EQUAL_UINT8(0xAA, rx_buf[0]);
}

void test_mqtt_client_gateway_init_without_relays(void) {
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "dev-gw1", "pass", "dev-gw1"};
    NodeRegistry registry;
    FakeClock clock(12, true);

    TEST_ASSERT_TRUE(mqtt.begin(cfg, &clock, &registry));
    TEST_ASSERT_TRUE(mqtt.connect());
    TEST_ASSERT_TRUE(mqtt.isConnected());
}

void test_node_registry_assignment_and_fanout(void) {
    NodeRegistry registry;
    registry.assignNodeToGroup(1, 1);
    registry.assignNodeToGroup(2, 1);
    registry.assignNodeToGroup(3, 2);
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(2, NodePumpState::OFF, 0, 0, 0, 1));

    TEST_ASSERT_EQUAL_UINT8(1, registry.getNodeGroup(1));
    TEST_ASSERT_EQUAL_UINT8(1, registry.getNodeGroup(2));
    TEST_ASSERT_EQUAL_UINT8(2, registry.getNodeGroup(3));

    registry.updateDesiredStateForGroup(1, NodePumpState::ON);
    NodeState st1{}, st2{}, st3{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, st1));
    TEST_ASSERT_TRUE(registry.getNodeState(2, st2));
    TEST_ASSERT_TRUE(registry.getNodeState(3, st3));
    TEST_ASSERT_EQUAL(NodePumpState::ON, st1.desired_state);
    TEST_ASSERT_EQUAL(NodePumpState::ON, st2.desired_state);
    TEST_ASSERT_EQUAL(NodePumpState::OFF, st3.desired_state);
}

void test_group_schedule_manager_ticks_and_fanout(void) {
    NodeRegistry registry;
    FakeClock clock(12, true);
    GroupScheduleManager group_mgr;
    TEST_ASSERT_TRUE(group_mgr.begin(&clock, &registry));

    registry.assignNodeToGroup(1, 1);
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));
    PublishedTreatmentAssignment assignment{1, 101, 1, GroupProfile{30, 300, 30, 600}};
    TEST_ASSERT_TRUE(group_mgr.applyPublishedTreatment(1, assignment));

    GroupProfile profile{10, 50, 10, 50};
    TEST_ASSERT_TRUE(group_mgr.setGroupProfile(1, profile));

    TEST_ASSERT_TRUE(group_mgr.stepGroupSchedule());
    NodeState st{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, st));
    TEST_ASSERT_EQUAL(NodePumpState::ON, st.desired_state);
}

void test_group_schedule_manager_invalid_rtc_forces_safe_off(void) {
    NodeRegistry registry;
    FakeClock clock(12, false);
    GroupScheduleManager group_mgr;
    TEST_ASSERT_TRUE(group_mgr.begin(&clock, &registry));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    PublishedTreatmentAssignment assignment{1, 101, 1, GroupProfile{30, 300, 30, 600}};
    TEST_ASSERT_TRUE(group_mgr.applyPublishedTreatment(1, assignment));
    TEST_ASSERT_FALSE(group_mgr.stepGroupSchedule());

    NodeState state{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL(NodePumpState::OFF, state.desired_state);
    GroupRuntimeState group{};
    TEST_ASSERT_TRUE(group_mgr.getGroupRuntimeState(1, group));
    TEST_ASSERT_EQUAL(GroupAssignmentState::UNASSIGNED, group.assignment_state);
}

void test_group_schedule_manager_services_watchdog_and_unassigned_safe_off(void) {
    NodeRegistry registry;
    FakeClock clock(12, true);
    FakeWatchdog watchdog;
    GroupScheduleManager group_mgr;
    TEST_ASSERT_TRUE(watchdog.registerWatchdog(0));
    TEST_ASSERT_TRUE(group_mgr.begin(&clock, &registry, &watchdog));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));
    TEST_ASSERT_TRUE(registry.setDesiredState(1, NodePumpState::ON));

    TEST_ASSERT_TRUE(group_mgr.stepGroupSchedule());
    NodeState state{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL(NodePumpState::OFF, state.desired_state);
    TEST_ASSERT_EQUAL_UINT32(1, watchdog.getResetCount(0));
}

void test_command_manager_hmac_and_crc_and_frame_codec(void) {
    FakeRfTransport rf;
    rf.begin();
    NodeRegistry registry;
    CommandManager cmd_mgr;
    TEST_ASSERT_TRUE(cmd_mgr.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(cmd_mgr));

    SetPumpPayload payload{1, 5000, 30000};
    uint8_t frame_buf[128];
    size_t frame_len = cmd_mgr.buildFrame(RfMessageType::SET_PUMP, 1, 1001,
                                          reinterpret_cast<const uint8_t*>(&payload),
                                          sizeof(payload), frame_buf, sizeof(frame_buf));
    TEST_ASSERT_TRUE(frame_len > sizeof(RfHeader));

    RfHeader header{};
    uint8_t parsed_payload[64];
    uint8_t parsed_len = 0;
    TEST_ASSERT_TRUE(cmd_mgr.parseFrame(frame_buf, frame_len, header, parsed_payload, parsed_len));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RfMessageType::SET_PUMP), header.message_type);
    TEST_ASSERT_EQUAL_UINT8(1, header.target_node_id);

    // Corrupt HMAC byte -> should fail
    frame_buf[frame_len - 5] ^= 0xFF;
    TEST_ASSERT_FALSE(cmd_mgr.parseFrame(frame_buf, frame_len, header, parsed_payload, parsed_len));
}

void test_rf_protocol_little_endian_byte_vectors(void) {
    FakeRfTransport rf;
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));

    const RfHeader header{{0xAA, 0x55}, 0x01, static_cast<uint8_t>(RfMessageType::SET_PUMP),
                          0x02, 0x00, 0x11223344U, 0x5566U, 0x778899AAU, 9};
    const uint8_t expected_header[] = {0xAA, 0x55, 0x01, 0x03, 0x02, 0x00, 0x44, 0x33, 0x22,
                                       0x11, 0x66, 0x55, 0xAA, 0x99, 0x88, 0x77, 0x09};
    uint8_t encoded_header[RF_HEADER_SIZE] = {};
    CommandManager::encodeHeader(header, encoded_header);
    TEST_ASSERT_EQUAL_MEMORY(expected_header, encoded_header, sizeof(expected_header));
    RfHeader decoded_header{};
    TEST_ASSERT_TRUE(CommandManager::decodeHeader(encoded_header, sizeof(encoded_header), decoded_header));
    TEST_ASSERT_EQUAL_UINT32(header.boot_session_id, decoded_header.boot_session_id);
    TEST_ASSERT_EQUAL_UINT16(header.sequence, decoded_header.sequence);
    TEST_ASSERT_EQUAL_UINT32(header.command_id, decoded_header.command_id);

    uint8_t wire[64] = {};
    uint8_t wire_len = 0;
    SetPumpPayload set_pump{1, 0x11223344U, 0x55667788U};
    const uint8_t expected_set_pump[] = {1, 0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55};
    TEST_ASSERT_TRUE(CommandManager::encodePayload(RfMessageType::SET_PUMP,
                     reinterpret_cast<const uint8_t*>(&set_pump), sizeof(set_pump), wire, wire_len));
    TEST_ASSERT_EQUAL_UINT8(sizeof(expected_set_pump), wire_len);
    TEST_ASSERT_EQUAL_MEMORY(expected_set_pump, wire, sizeof(expected_set_pump));

    CommandAckPayload ack{0x1234, static_cast<uint8_t>(AckOutcome::SUCCESS), 1, 1, {0, 0, 0}};
    const uint8_t expected_ack[] = {0x34, 0x12, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00};
    TEST_ASSERT_TRUE(CommandManager::encodePayload(RfMessageType::COMMAND_ACK,
                     reinterpret_cast<const uint8_t*>(&ack), sizeof(ack), wire, wire_len));
    TEST_ASSERT_EQUAL_MEMORY(expected_ack, wire, sizeof(expected_ack));

    TelemetryPayload telemetry{1, 0, 0x1234, 0x55667788U, 0x99AABBCCU, 0x05, 0xDDEEFF00U};
    const uint8_t expected_telemetry[] = {1, 0, 0x34, 0x12, 0x88, 0x77, 0x66, 0x55, 0xCC,
                                          0xBB, 0xAA, 0x99, 0x05, 0x00, 0xFF, 0xEE, 0xDD};
    TEST_ASSERT_TRUE(CommandManager::encodePayload(RfMessageType::TELEMETRY,
                     reinterpret_cast<const uint8_t*>(&telemetry), sizeof(telemetry), wire, wire_len));
    TEST_ASSERT_EQUAL_MEMORY(expected_telemetry, wire, sizeof(expected_telemetry));

    FaultReportPayload fault{3, 0x01020304U, 0, 0xA1B2C3D4U};
    const uint8_t expected_fault[] = {3, 0x04, 0x03, 0x02, 0x01, 0, 0xD4, 0xC3, 0xB2, 0xA1};
    TEST_ASSERT_TRUE(CommandManager::encodePayload(RfMessageType::FAULT_REPORT,
                     reinterpret_cast<const uint8_t*>(&fault), sizeof(fault), wire, wire_len));
    TEST_ASSERT_EQUAL_MEMORY(expected_fault, wire, sizeof(expected_fault));

    const size_t frame_len = manager.buildFrame(RfMessageType::SET_PUMP, 2, 0x778899AAU,
                                                 reinterpret_cast<const uint8_t*>(&set_pump), sizeof(set_pump),
                                                 wire, sizeof(wire));
    TEST_ASSERT_TRUE(frame_len > RF_HEADER_SIZE);
    TEST_ASSERT_EQUAL_MEMORY(expected_set_pump, wire + RF_HEADER_SIZE, sizeof(expected_set_pump));
    TEST_ASSERT_EQUAL_UINT16(CommandManager::calculateCrc16(wire, frame_len - 2), readU16Le(wire + frame_len - 2));
}

void test_rf_frame_codec_interoperates_for_gateway_and_node_messages(void) {
    const uint8_t key[16] = {0xA5};
    uint8_t frame[RF_MAX_FRAME_SIZE] = {};
    uint8_t decoded[RF_MAX_PAYLOAD_SIZE] = {};
    RfHeader header{};

    CommandAckPayload ack{0x1234, static_cast<uint8_t>(AckOutcome::SUCCESS), 1, 1, {0, 0, 0}};
    const RfFrameMetadata node_metadata{1, 0, 7, 9, 0xAABBCCDD};
    const size_t ack_len = RfFrameCodec::encodeFrame(node_metadata, RfMessageType::COMMAND_ACK,
        &ack, sizeof(ack), key, sizeof(key), frame, sizeof(frame));
    TEST_ASSERT_TRUE(ack_len > 0);
    TEST_ASSERT_TRUE(RfFrameCodec::decodeFrame(frame, ack_len, key, sizeof(key), header, decoded, sizeof(decoded)));
    TEST_ASSERT_EQUAL_UINT8(1, header.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(0, header.target_node_id);
    CommandAckPayload decoded_ack{};
    TEST_ASSERT_TRUE(RfFrameCodec::decodePayload(RfMessageType::COMMAND_ACK, decoded, header.payload_len,
                                                  &decoded_ack, sizeof(decoded_ack)));
    TEST_ASSERT_EQUAL_UINT16(ack.ack_sequence, decoded_ack.ack_sequence);

    TelemetryPayload telemetry{1, 1, 250, 1200, 450, 0, node_metadata.command_id};
    HeartbeatPayload heartbeat{33, -72, 88};
    FaultReportPayload fault{3, 123456, 0, node_metadata.command_id};
    const RfMessageType node_types[] = {RfMessageType::TELEMETRY, RfMessageType::HEARTBEAT, RfMessageType::FAULT_REPORT};
    const void* node_payloads[] = {&telemetry, &heartbeat, &fault};
    const size_t node_payload_sizes[] = {sizeof(telemetry), sizeof(heartbeat), sizeof(fault)};
    for (size_t i = 0; i < 3; ++i) {
        const RfFrameMetadata metadata{1, 0, 7, static_cast<uint16_t>(10 + i), node_metadata.command_id};
        const size_t length = RfFrameCodec::encodeFrame(metadata, node_types[i], node_payloads[i], node_payload_sizes[i],
                                                        key, sizeof(key), frame, sizeof(frame));
        TEST_ASSERT_TRUE(length > 0);
        TEST_ASSERT_TRUE(RfFrameCodec::decodeFrame(frame, length, key, sizeof(key), header, decoded, sizeof(decoded)));
        TEST_ASSERT_EQUAL_UINT8(1, header.source_node_id);
        TEST_ASSERT_EQUAL_UINT8(0, header.target_node_id);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(node_types[i]), header.message_type);
    }
}

void test_rf_frame_codec_rejects_empty_payload_for_all_current_schemas(void) {
    const uint8_t key[16] = {0xA5};
    const RfFrameMetadata metadata{0, 1, 7, 9, 0xAABBCCDD};
    const RfMessageType message_types[] = {
        RfMessageType::PING, RfMessageType::PONG, RfMessageType::SET_PUMP,
        RfMessageType::COMMAND_ACK, RfMessageType::TELEMETRY,
        RfMessageType::HEARTBEAT, RfMessageType::FAULT_REPORT
    };
    uint8_t frame[RF_MAX_FRAME_SIZE] = {};
    for (RfMessageType type : message_types) {
        TEST_ASSERT_EQUAL_UINT(0, RfFrameCodec::encodeFrame(metadata, type, nullptr, 0,
                                                              key, sizeof(key), frame, sizeof(frame)));
    }
}

void test_hmac_rejects_invalid_pointers_and_accepts_zero_length_inputs(void) {
    const uint8_t key[16] = {0xA5};
    const uint8_t data[] = {1, 2, 3};
    uint8_t hash[SHA256_HASH_SIZE] = {};
    uint8_t tag[HMAC_TAG_SIZE] = {};
    TEST_ASSERT_FALSE(HmacSha256::calculate(nullptr, 1, data, sizeof(data), hash));
    TEST_ASSERT_FALSE(HmacSha256::calculate(key, sizeof(key), nullptr, 1, hash));
    TEST_ASSERT_FALSE(HmacSha256::calculate(key, sizeof(key), data, sizeof(data), nullptr));
    TEST_ASSERT_FALSE(HmacSha256::calculateTruncated(nullptr, 1, data, sizeof(data), tag));
    TEST_ASSERT_FALSE(HmacSha256::calculateTruncated(key, sizeof(key), nullptr, 1, tag));
    TEST_ASSERT_FALSE(HmacSha256::calculateTruncated(key, sizeof(key), data, sizeof(data), nullptr));
    TEST_ASSERT_TRUE(HmacSha256::calculate(nullptr, 0, nullptr, 0, hash));
    TEST_ASSERT_TRUE(HmacSha256::calculateTruncated(nullptr, 0, nullptr, 0, tag));
}

void test_rf_provisioning_commit_failure_keeps_manager_fail_closed(void) {
    FakeNvsBackend backend;
    backend.setValue(FakeNvsBackend::SPRAY_DAY, 9); // rf_boot
    backend.setValue(FakeNvsBackend::COOLDOWN_DAY, 0x01020304);
    backend.setValue(FakeNvsBackend::SPRAY_NIGHT, 0x05060708);
    backend.setValue(FakeNvsBackend::COOLDOWN_NIGHT, 0x090A0B0C);
    backend.setCommitResult(FakeNvsBackend::IO_ERROR);
    NvsStorage storage(&backend);
    TEST_ASSERT_TRUE(storage.begin());
    FakeRfTransport rf;
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_FALSE(manager.provisionFromNvs(storage));
    TEST_ASSERT_FALSE(manager.isProvisioned());
    uint8_t frame[128] = {};
    TEST_ASSERT_EQUAL_UINT32(0, manager.buildFrame(RfMessageType::PING, 1, 1, nullptr, 0, frame, sizeof(frame)));
}

void test_rf_missing_key_keeps_rx_and_tx_locked_without_fallback(void) {
    FakeNvsBackend backend;
    backend.setGetResult(FakeNvsBackend::COOLDOWN_NIGHT, FakeNvsBackend::NOT_FOUND);
    NvsStorage storage(&backend, RF_NVS_NAMESPACE);
    TEST_ASSERT_TRUE(storage.begin());

    FakeRfTransport rf;
    NodeRegistry registry;
    CommandManager locked_manager;
    TEST_ASSERT_TRUE(locked_manager.begin(&registry, &rf));
    TEST_ASSERT_FALSE(locked_manager.provisionFromNvs(storage));
    uint8_t frame[RF_MAX_FRAME_SIZE] = {};
    TEST_ASSERT_EQUAL_UINT32(0, locked_manager.buildFrame(RfMessageType::PING, 1, 1, nullptr, 0, frame, sizeof(frame)));

    CommandManager provisioned_sender;
    TEST_ASSERT_TRUE(provisioned_sender.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(provisioned_sender));
    const PingPayload ping{123};
    const size_t frame_len = provisioned_sender.buildFrame(RfMessageType::PING, 1, 1,
                                                            reinterpret_cast<const uint8_t*>(&ping),
                                                            sizeof(ping), frame, sizeof(frame));
    TEST_ASSERT_TRUE(frame_len > 0);
    TEST_ASSERT_FALSE(locked_manager.handleIncomingFrame(frame, frame_len, 1));
}

void test_rf_provisioning_uses_canonical_namespace_and_keys(void) {
    FakeNvsBackend backend;
    backend.setValue(FakeNvsBackend::SPRAY_DAY, 9);
    NvsStorage storage(&backend, RF_NVS_NAMESPACE);
    TEST_ASSERT_TRUE(storage.begin());
    FakeRfTransport rf; NodeRegistry registry; CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(manager.provisionFromNvs(storage));
    TEST_ASSERT_EQUAL_STRING("rf_config", backend.lastNamespace());
}

void test_rf_boot_session_uses_uint32_range_and_fails_closed_at_exhaustion(void) {
    FakeNvsBackend backend;
    backend.setValue(FakeNvsBackend::SPRAY_DAY, UINT32_MAX - 1U);
    backend.setValue(FakeNvsBackend::COOLDOWN_DAY, 0x01020304);
    backend.setValue(FakeNvsBackend::SPRAY_NIGHT, 0x05060708);
    backend.setValue(FakeNvsBackend::COOLDOWN_NIGHT, 0x090A0B0C);
    NvsStorage storage(&backend, RF_NVS_NAMESPACE);
    TEST_ASSERT_TRUE(storage.begin());
    FakeRfTransport rf;
    NodeRegistry registry;
    CommandManager first_boot;
    TEST_ASSERT_TRUE(first_boot.begin(&registry, &rf));
    TEST_ASSERT_TRUE(first_boot.provisionFromNvs(storage));

    // The persisted uint32 session has reached its final legal value; the next
    // boot must not wrap and thereby weaken the anti-replay session ordering.
    CommandManager exhausted_boot;
    TEST_ASSERT_TRUE(exhausted_boot.begin(&registry, &rf));
    TEST_ASSERT_FALSE(exhausted_boot.provisionFromNvs(storage));
    TEST_ASSERT_FALSE(exhausted_boot.isProvisioned());
}

void test_command_manager_pending_retry_and_timeout_fault(void) {
    FakeRfTransport rf;
    rf.begin();
    NodeRegistry registry;
    CommandManager cmd_mgr;
    TEST_ASSERT_TRUE(cmd_mgr.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(cmd_mgr));

    registry.assignNodeToGroup(1, 1);
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));
    TEST_ASSERT_TRUE(provisionTestNodePolicy(cmd_mgr, 1));
    registry.updateDesiredStateForGroup(1, NodePumpState::ON);

    // Initial dispatch (retry 1)
    uint32_t now = 1000;
    cmd_mgr.serviceCommandFanout(now);
    TEST_ASSERT_TRUE(cmd_mgr.isPending(1));

    // Retry 2
    now += 1000;
    cmd_mgr.serviceCommandFanout(now);
    TEST_ASSERT_TRUE(cmd_mgr.isPending(1));

    // Retry 3
    now += 1000;
    cmd_mgr.serviceCommandFanout(now);
    TEST_ASSERT_TRUE(cmd_mgr.isPending(1));

    // Terminal timeout -> node should be marked FAULT
    now += 1000;
    cmd_mgr.serviceCommandFanout(now);
    TEST_ASSERT_FALSE(cmd_mgr.isPending(1));

    NodeState st{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, st));
    TEST_ASSERT_EQUAL(NodeHealthStatus::FAULT, st.health);
    TEST_ASSERT_EQUAL(NodePumpState::OFF, st.desired_state);
    TEST_ASSERT_TRUE(st.fault_latched);
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::ON, 1, 0, 0, now));
    TEST_ASSERT_TRUE(registry.getNodeState(1, st));
    TEST_ASSERT_EQUAL(NodeHealthStatus::FAULT, st.health);
    TEST_ASSERT_TRUE(cmd_mgr.serviceCommandFanout(now + 1000));
}

void test_on_is_rejected_until_authenticated_node_flow_policy_is_provisioned(void) {
    FakeRfTransport rf;
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(rf.begin());
    TEST_ASSERT_TRUE(registry.begin());
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));

    TEST_ASSERT_FALSE(manager.queueExternalNodeCommand(1, NodePumpState::ON, "policy-required"));
    TEST_ASSERT_FALSE(manager.hasProvisionedNodeFlowPolicy(1));
    TEST_ASSERT_EQUAL_UINT(0, rf.getTxBuffer().size());
    NodeState state{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL(NodePumpState::OFF, state.desired_state);

    TEST_ASSERT_TRUE(provisionTestNodePolicy(manager, 1));
    TEST_ASSERT_TRUE(manager.queueExternalNodeCommand(1, NodePumpState::ON, "policy-present"));
}

void test_invalid_or_other_node_flow_policy_cannot_authorize_on(void) {
    FakeRfTransport rf;
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(rf.begin());
    TEST_ASSERT_TRUE(registry.begin());
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(2, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(2, NodePumpState::OFF, 0, 0, 0, 1));

    TEST_ASSERT_FALSE(manager.provisionNodeFlowPolicy(1, 50, 20, 40, 3000,
                                                       FlowPolicyProvenance{1, 101, 1001}));
    TEST_ASSERT_FALSE(manager.provisionNodeFlowPolicy(1, 50, 20, 600, 3000,
                                                       FlowPolicyProvenance{0, 101, 1001}));
    TEST_ASSERT_TRUE(provisionTestNodePolicy(manager, 1, 80));
    TEST_ASSERT_FALSE(manager.queueExternalNodeCommand(2, NodePumpState::ON, "node-b-unprovisioned"));
    TEST_ASSERT_TRUE(manager.queueExternalNodeCommand(1, NodePumpState::ON, "node-a-provisioned"));
}

void test_invalid_control_policy_update_preserves_existing_policy_atomically(void) {
    FakeRfTransport rf;
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(rf.begin());
    TEST_ASSERT_TRUE(registry.begin());
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));
    TEST_ASSERT_TRUE(manager.provisionNodeControlPolicy(
        1, 60000, 300000, 50, 20, 600, 3000, FlowPolicyProvenance{7, 101, 1001}));

    NodeLeasePolicy old_lease{};
    NodeFlowPolicy old_flow{};
    TEST_ASSERT_TRUE(manager.getNodeControlPolicy(1, old_lease, old_flow));
    TEST_ASSERT_FALSE(manager.provisionNodeControlPolicy(
        1, 12345, 67890, 500, 20, 400, 3000, FlowPolicyProvenance{8, 102, 1002}));

    NodeLeasePolicy lease{};
    NodeFlowPolicy flow{};
    TEST_ASSERT_TRUE(manager.getNodeControlPolicy(1, lease, flow));
    TEST_ASSERT_EQUAL_MEMORY(&old_lease, &lease, sizeof(lease));
    TEST_ASSERT_EQUAL_MEMORY(&old_flow, &flow, sizeof(flow));
    TEST_ASSERT_TRUE(manager.queueExternalNodeCommand(1, NodePumpState::ON, "old-policy-still-valid"));
}

void test_out_of_range_control_or_flow_policy_preserves_existing_policy(void) {
    const uint16_t invalid_max_flows[] = {601, UINT16_MAX};
    for (const uint16_t invalid_max_flow : invalid_max_flows) {
        FakeRfTransport rf;
        NodeRegistry registry;
        CommandManager manager;
        TEST_ASSERT_TRUE(rf.begin());
        TEST_ASSERT_TRUE(registry.begin());
        TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
        TEST_ASSERT_TRUE(provisionTestPsk(manager));
        TEST_ASSERT_TRUE(manager.provisionNodeControlPolicy(
            1, 60000, 300000, 50, 20, 600, 3000, FlowPolicyProvenance{7, 101, 1001}));

        NodeLeasePolicy old_lease{};
        NodeFlowPolicy old_flow{};
        TEST_ASSERT_TRUE(manager.getNodeControlPolicy(1, old_lease, old_flow));
        TEST_ASSERT_FALSE(manager.provisionNodeControlPolicy(
            1, 12345, 67890, 50, 20, invalid_max_flow, 3000, FlowPolicyProvenance{8, 102, 1002}));
        TEST_ASSERT_FALSE(manager.provisionNodeFlowPolicy(
            1, 50, 20, invalid_max_flow, 3000, FlowPolicyProvenance{8, 102, 1002}));

        NodeLeasePolicy lease{};
        NodeFlowPolicy flow{};
        TEST_ASSERT_TRUE(manager.getNodeControlPolicy(1, lease, flow));
        TEST_ASSERT_EQUAL_MEMORY(&old_lease, &lease, sizeof(lease));
        TEST_ASSERT_EQUAL_MEMORY(&old_flow, &flow, sizeof(flow));
    }
}

void test_invalid_mqtt_control_policy_update_preserves_existing_policy(void) {
    FakeRfTransport rf;
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(rf.begin());
    TEST_ASSERT_TRUE(registry.begin());
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));
    TEST_ASSERT_TRUE(manager.provisionNodeControlPolicy(
        1, 60000, 300000, 50, 20, 600, 3000, FlowPolicyProvenance{7, 101, 1001}));

    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "gateway-1", "pass", "gateway-1"};
    TEST_ASSERT_TRUE(mqtt.begin(cfg, nullptr, &registry, &manager));
    TEST_ASSERT_TRUE(mqtt.connect());
    char topic[] = "aeroponics/device/gateway-1/command/config/flow-policy";
    char invalid_payload[] = "{\"command_id\":\"bad-update\",\"version\":1,\"node_id\":1,\"policy_version\":8,\"treatment_version_id\":102,\"calibration_id\":1002,\"min_flow_lpm_x100\":500,\"max_off_flow_lpm_x100\":20,\"max_flow_lpm_x100\":400,\"flow_start_timeout_ms\":3000,\"run_lease_ms\":12345,\"max_on_duration_ms\":67890}";
    mqtt.simulateIncomingMessage(topic, reinterpret_cast<uint8_t*>(invalid_payload), strlen(invalid_payload));
    mqtt.serviceIncomingCommands();
    TEST_ASSERT_TRUE(strstr(mqtt.mockLastPublishedPayload(), "\"status\":\"REJECTED\"") != nullptr);

    NodeLeasePolicy lease{};
    NodeFlowPolicy flow{};
    TEST_ASSERT_TRUE(manager.getNodeControlPolicy(1, lease, flow));
    TEST_ASSERT_EQUAL_UINT32(60000, lease.run_lease_ms);
    TEST_ASSERT_EQUAL_UINT32(300000, lease.max_on_duration_ms);
    TEST_ASSERT_EQUAL_UINT16(50, flow.min_flow_lpm_x100);
    TEST_ASSERT_EQUAL_UINT32(7, flow.provenance.policy_version);
}

void test_out_of_range_mqtt_control_policy_is_rejected_without_mutation(void) {
    const uint16_t invalid_max_flows[] = {601, UINT16_MAX};
    for (const uint16_t invalid_max_flow : invalid_max_flows) {
        FakeRfTransport rf;
        NodeRegistry registry;
        CommandManager manager;
        TEST_ASSERT_TRUE(rf.begin());
        TEST_ASSERT_TRUE(registry.begin());
        TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
        TEST_ASSERT_TRUE(provisionTestPsk(manager));
        TEST_ASSERT_TRUE(manager.provisionNodeControlPolicy(
            1, 60000, 300000, 50, 20, 600, 3000, FlowPolicyProvenance{7, 101, 1001}));
        MqttClient mqtt;
        MqttConfig cfg{"mqtt.local", 1883, "gateway-1", "pass", "gateway-1"};
        TEST_ASSERT_TRUE(mqtt.begin(cfg, nullptr, &registry, &manager));
        TEST_ASSERT_TRUE(mqtt.connect());

        char topic[] = "aeroponics/device/gateway-1/command/config/flow-policy";
        char payload[384] = {};
        std::snprintf(payload, sizeof(payload),
                      "{\"command_id\":\"range-%u\",\"version\":1,\"node_id\":1,\"policy_version\":8,"
                      "\"treatment_version_id\":102,\"calibration_id\":1002,\"min_flow_lpm_x100\":50,"
                      "\"max_off_flow_lpm_x100\":20,\"max_flow_lpm_x100\":%u,\"flow_start_timeout_ms\":3000,"
                      "\"run_lease_ms\":12345,\"max_on_duration_ms\":67890}", invalid_max_flow, invalid_max_flow);
        mqtt.simulateIncomingMessage(topic, reinterpret_cast<uint8_t*>(payload), strlen(payload));
        mqtt.serviceIncomingCommands();
        TEST_ASSERT_TRUE(strstr(mqtt.mockLastPublishedPayload(), "\"status\":\"REJECTED\"") != nullptr);

        NodeLeasePolicy lease{};
        NodeFlowPolicy flow{};
        TEST_ASSERT_TRUE(manager.getNodeControlPolicy(1, lease, flow));
        TEST_ASSERT_EQUAL_UINT32(60000, lease.run_lease_ms);
        TEST_ASSERT_EQUAL_UINT16(600, flow.max_flow_lpm_x100);
        TEST_ASSERT_EQUAL_UINT32(7, flow.provenance.policy_version);
    }
}

void test_flow_confirmation_uses_only_the_provisioned_node_threshold(void) {
    FakeRfTransport rf;
    NodeRegistry registry;
    CommandManager manager;
    FakeOutcomeSink outcomes;
    TEST_ASSERT_TRUE(rf.begin());
    TEST_ASSERT_TRUE(registry.begin());
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    manager.setOutcomeSink(&outcomes);
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));
    TEST_ASSERT_TRUE(provisionTestNodePolicy(manager, 1, 175));
    TEST_ASSERT_TRUE(manager.queueExternalNodeCommand(1, NodePumpState::ON, "node-a-threshold"));
    TEST_ASSERT_TRUE(manager.serviceCommandFanout(100));
    RfHeader request{};
    std::memcpy(&request, rf.getTxBuffer().data(), sizeof(request));
    TEST_ASSERT_TRUE(acknowledgePumpCommand(manager, request, 101));

    TelemetryPayload below_a{1, 1, 100, 0, 0, 0, request.command_id};
    uint8_t frame[128] = {};
    size_t frame_len = buildAuthenticatedNodeFrame(manager, RfMessageType::TELEMETRY, 1, 2,
                                                    request.command_id, &below_a, sizeof(below_a),
                                                    frame, sizeof(frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(frame, frame_len, 102));
    TEST_ASSERT_TRUE(manager.isPending(1));
    TEST_ASSERT_EQUAL_STRING("PUMP_FEEDBACK_ON", outcomes.last_status);

    TelemetryPayload enough_a{1, 1, 175, 0, 0, 0, request.command_id};
    frame_len = buildAuthenticatedNodeFrame(manager, RfMessageType::TELEMETRY, 1, 3,
                                            request.command_id, &enough_a, sizeof(enough_a),
                                            frame, sizeof(frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(frame, frame_len, 103));
    TEST_ASSERT_FALSE(manager.isPending(1));
    TEST_ASSERT_EQUAL_STRING("COMPLETED", outcomes.last_status);
}

void test_rf_retry_reuses_immutable_frame_and_correlated_telemetry_completes(void) {
    FakeRfTransport rf;
    TEST_ASSERT_TRUE(rf.begin());
    NodeRegistry registry;
    TEST_ASSERT_TRUE(registry.begin());
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));
    TEST_ASSERT_TRUE(provisionTestNodePolicy(manager, 1));
    TEST_ASSERT_TRUE(manager.queueExternalNodeCommand(1, NodePumpState::ON, "retry-idempotent-1"));

    // Node simulator: it actuates once for a new correlation key and caches the
    // ACK. A retry must present the exact same bytes/key, so no second actuation.
    TEST_ASSERT_TRUE(manager.serviceCommandFanout(1000));
    const std::vector<uint8_t>& initial_tx = rf.getTxBuffer();
    const size_t frame_len = initial_tx.size();
    TEST_ASSERT_TRUE(frame_len > sizeof(RfHeader));
    std::vector<uint8_t> initial_frame(initial_tx.begin(), initial_tx.end());
    RfHeader initial_header{};
    std::memcpy(&initial_header, initial_frame.data(), sizeof(initial_header));
    int simulated_actuation_count = 1;
    uint8_t cached_ack[128] = {};
    const size_t cached_ack_len = buildAuthenticatedNodeAck(manager, initial_header, 1, 1,
                                                             cached_ack, sizeof(cached_ack));

    TEST_ASSERT_TRUE(manager.serviceCommandFanout(2000));
    const std::vector<uint8_t>& retried_tx = rf.getTxBuffer();
    TEST_ASSERT_EQUAL_UINT(frame_len * 2, retried_tx.size());
    TEST_ASSERT_EQUAL_MEMORY(initial_frame.data(), retried_tx.data() + frame_len, frame_len);
    RfHeader retry_header{};
    std::memcpy(&retry_header, retried_tx.data() + frame_len, sizeof(retry_header));
    if (retry_header.boot_session_id == initial_header.boot_session_id &&
        retry_header.sequence == initial_header.sequence &&
        retry_header.command_id == initial_header.command_id) {
        // Duplicate correlation key: simulator returns cached outcome only.
    } else {
        ++simulated_actuation_count;
    }
    TEST_ASSERT_EQUAL_INT(1, simulated_actuation_count);

    // The ACK generated for the initial transmission remains valid after retry,
    // but receipt is not state/flow evidence and therefore cannot complete it.
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(cached_ack, cached_ack_len, 2001));
    TEST_ASSERT_TRUE(manager.isPending(1));
    TelemetryPayload telemetry{1, 1, 250, 1200, 450, 0, initial_header.command_id};
    uint8_t telemetry_frame[128] = {};
    const size_t telemetry_len = buildAuthenticatedNodeFrame(manager, RfMessageType::TELEMETRY, 1, 3,
                                                              initial_header.command_id, &telemetry,
                                                              sizeof(telemetry), telemetry_frame,
                                                              sizeof(telemetry_frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(telemetry_frame, telemetry_len, 2002));
    TEST_ASSERT_FALSE(manager.isPending(1));
}

void test_ack_without_correlated_telemetry_never_renews_on_lease_and_times_out_safe_off(void) {
    FakeRfTransport rf;
    TEST_ASSERT_TRUE(rf.begin());
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));
    TEST_ASSERT_TRUE(manager.provisionNodeLeasePolicy(1, 4321, 8765));
    TEST_ASSERT_TRUE(manager.provisionNodeFlowPolicy(1, 50, 20, 600, 3000,
                                                      FlowPolicyProvenance{1, 101, 1001}));
    TEST_ASSERT_TRUE(manager.queueExternalNodeCommand(1, NodePumpState::ON, "ack-no-telemetry"));

    TEST_ASSERT_TRUE(manager.serviceCommandFanout(100));
    const std::vector<uint8_t>& tx = rf.getTxBuffer();
    const size_t on_frame_len = tx.size();
    RfHeader on_request{};
    std::memcpy(&on_request, tx.data(), sizeof(on_request));
    SetPumpPayload on_payload{};
    std::memcpy(&on_payload, tx.data() + sizeof(on_request), sizeof(on_payload));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodePumpState::ON), on_payload.desired_state);
    TEST_ASSERT_EQUAL_UINT32(4321, on_payload.run_lease_ms);
    TEST_ASSERT_EQUAL_UINT32(8765, on_payload.max_on_duration_ms);

    uint8_t ack_frame[128] = {};
    const size_t ack_len = buildAuthenticatedNodeAck(manager, on_request, 1, 1, ack_frame, sizeof(ack_frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(ack_frame, ack_len, 101));
    // A retransmitted success ACK may refresh liveness but must not renew the
    // original feedback deadline or create a new ON/lease correlation.
    const CommandAckPayload duplicate_ack{on_request.sequence, static_cast<uint8_t>(AckOutcome::SUCCESS),
                                          1, 1, {0, 0, 0}};
    const uint8_t key[16] = {0xA5};
    const RfFrameMetadata duplicate_metadata{1, 0, 1, 1, on_request.command_id};
    const size_t duplicate_ack_len = RfFrameCodec::encodeFrame(duplicate_metadata, RfMessageType::COMMAND_ACK,
        &duplicate_ack, sizeof(duplicate_ack), key, sizeof(key), ack_frame, sizeof(ack_frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(ack_frame, duplicate_ack_len, 4000));
    for (uint32_t now = 200; now < 101 + RF_FEEDBACK_DEADLINE_MS; now += 100) {
        TEST_ASSERT_TRUE(manager.serviceCommandFanout(now));
    }
    // No retry and no new logical ON: the original immutable frame is the only
    // RF traffic while waiting for the exact command's feedback correlation.
    TEST_ASSERT_EQUAL_UINT(on_frame_len, rf.getTxBuffer().size());
    TEST_ASSERT_TRUE(manager.isPending(1));

    TEST_ASSERT_TRUE(manager.serviceCommandFanout(101 + RF_FEEDBACK_DEADLINE_MS));
    NodeState state{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL(NodeHealthStatus::FAULT, state.health);
    TEST_ASSERT_EQUAL(NodePumpState::OFF, state.desired_state);
    TEST_ASSERT_TRUE(state.fault_latched);
    TEST_ASSERT_TRUE(manager.isPending(1));
    // Deadline may queue only explicit OFF; it never emits a newly correlated ON.
    TEST_ASSERT_TRUE(manager.serviceCommandFanout(102 + RF_FEEDBACK_DEADLINE_MS));
    RfHeader safe_off{};
    std::memcpy(&safe_off, rf.getTxBuffer().data() + on_frame_len, sizeof(safe_off));
    SetPumpPayload safe_off_payload{};
    std::memcpy(&safe_off_payload, rf.getTxBuffer().data() + on_frame_len + sizeof(safe_off),
                sizeof(safe_off_payload));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodePumpState::OFF), safe_off_payload.desired_state);
    TEST_ASSERT_NOT_EQUAL(on_request.command_id, safe_off.command_id);
}

void test_on_zero_flow_waits_then_latches_no_flow_and_queues_one_safe_off(void) {
    FakeRfTransport rf; NodeRegistry registry; CommandManager manager; FakeOutcomeSink outcomes;
    TEST_ASSERT_TRUE(rf.begin()); TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager)); manager.setOutcomeSink(&outcomes);
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));
    TEST_ASSERT_TRUE(provisionTestNodePolicy(manager, 1));
    TEST_ASSERT_TRUE(manager.queueExternalNodeCommand(1, NodePumpState::ON, "zero-flow"));
    TEST_ASSERT_TRUE(manager.serviceCommandFanout(100));
    RfHeader request{}; std::memcpy(&request, rf.getTxBuffer().data(), sizeof(request));
    TEST_ASSERT_TRUE(acknowledgePumpCommand(manager, request, 101));

    TelemetryPayload zero_flow{1, 1, 0, 0, 0, 0, request.command_id};
    uint8_t frame[128] = {};
    const size_t frame_len = buildAuthenticatedNodeFrame(manager, RfMessageType::TELEMETRY, 1, 2,
        request.command_id, &zero_flow, sizeof(zero_flow), frame, sizeof(frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(frame, frame_len, 102));
    TEST_ASSERT_EQUAL_STRING("PUMP_FEEDBACK_ON", outcomes.last_status);
    TEST_ASSERT_TRUE(manager.isPending(1));

    TEST_ASSERT_TRUE(manager.serviceCommandFanout(3102));
    NodeState state{}; TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL(NodeHealthStatus::FAULT, state.health);
    TEST_ASSERT_TRUE(state.fault_latched); TEST_ASSERT_EQUAL_STRING("NO_FLOW_FAULT_SAFE_OFF", outcomes.last_audit);
    TEST_ASSERT_EQUAL_STRING("NO_FLOW_FAULT", outcomes.last_status);
    TEST_ASSERT_TRUE(manager.isPending(1));
    TEST_ASSERT_TRUE(manager.serviceCommandFanout(3103));
    const size_t one_off_size = rf.getTxBuffer().size();
    TEST_ASSERT_TRUE(manager.serviceCommandFanout(3104));
    TEST_ASSERT_EQUAL_UINT(one_off_size, rf.getTxBuffer().size());
}

void test_on_delayed_valid_flow_before_deadline_completes(void) {
    FakeRfTransport rf; NodeRegistry registry; CommandManager manager; FakeOutcomeSink outcomes;
    TEST_ASSERT_TRUE(rf.begin()); TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager)); manager.setOutcomeSink(&outcomes);
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));
    TEST_ASSERT_TRUE(provisionTestNodePolicy(manager, 1));
    TEST_ASSERT_TRUE(manager.queueExternalNodeCommand(1, NodePumpState::ON, "delayed-flow"));
    TEST_ASSERT_TRUE(manager.serviceCommandFanout(100));
    RfHeader request{}; std::memcpy(&request, rf.getTxBuffer().data(), sizeof(request));
    TEST_ASSERT_TRUE(acknowledgePumpCommand(manager, request, 101));

    TelemetryPayload feedback_on{1, 1, 0, 0, 0, 0, request.command_id};
    uint8_t frame[128] = {};
    size_t frame_len = buildAuthenticatedNodeFrame(manager, RfMessageType::TELEMETRY, 1, 2,
        request.command_id, &feedback_on, sizeof(feedback_on), frame, sizeof(frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(frame, frame_len, 102));
    TelemetryPayload valid_flow{1, 1, 50, 250, 100, 0, request.command_id};
    frame_len = buildAuthenticatedNodeFrame(manager, RfMessageType::TELEMETRY, 1, 3,
        request.command_id, &valid_flow, sizeof(valid_flow), frame, sizeof(frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(frame, frame_len, 3101));
    TEST_ASSERT_FALSE(manager.isPending(1));
    TEST_ASSERT_EQUAL_STRING("COMPLETED", outcomes.last_status);
}

void test_over_range_or_fault_telemetry_latches_safe_off(void) {
    const uint16_t flows[] = {601, 100};
    const uint8_t faults[] = {0, 1};
    for (size_t i = 0; i < 2; ++i) {
        FakeRfTransport rf; NodeRegistry registry; CommandManager manager;
        TEST_ASSERT_TRUE(rf.begin()); TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
        TEST_ASSERT_TRUE(provisionTestPsk(manager)); TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
        TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));
        TEST_ASSERT_TRUE(provisionTestNodePolicy(manager, 1));
        TelemetryPayload invalid{1, 1, flows[i], 0, 0, faults[i], 0}; uint8_t frame[128] = {};
        const size_t frame_len = buildAuthenticatedNodeFrame(manager, RfMessageType::TELEMETRY, 1, 1,
            0, &invalid, sizeof(invalid), frame, sizeof(frame));
        TEST_ASSERT_FALSE(manager.handleIncomingFrame(frame, frame_len, 2));
        NodeState state{}; TEST_ASSERT_TRUE(registry.getNodeState(1, state));
        TEST_ASSERT_EQUAL(NodeHealthStatus::FAULT, state.health); TEST_ASSERT_TRUE(manager.isPending(1));
    }
}

void test_physical_over_range_telemetry_never_completes_and_queues_safe_off(void) {
    const uint16_t invalid_flows[] = {601, UINT16_MAX};
    for (const uint16_t invalid_flow : invalid_flows) {
        FakeRfTransport rf; NodeRegistry registry; CommandManager manager; FakeOutcomeSink outcomes;
        TEST_ASSERT_TRUE(rf.begin()); TEST_ASSERT_TRUE(registry.begin());
        TEST_ASSERT_TRUE(manager.begin(&registry, &rf)); TEST_ASSERT_TRUE(provisionTestPsk(manager));
        manager.setOutcomeSink(&outcomes);
        TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
        TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));
        TEST_ASSERT_TRUE(provisionTestNodePolicy(manager, 1));
        TEST_ASSERT_TRUE(manager.queueExternalNodeCommand(1, NodePumpState::ON, "physical-over-range"));
        TEST_ASSERT_TRUE(manager.serviceCommandFanout(100));
        const size_t on_offset = rf.getTxBuffer().size() - (RF_HEADER_SIZE + 9 + HMAC_TAG_SIZE + 2);
        RfHeader request{}; std::memcpy(&request, rf.getTxBuffer().data() + on_offset, sizeof(request));
        TEST_ASSERT_TRUE(acknowledgePumpCommand(manager, request, 101));
        TelemetryPayload telemetry{1, 1, invalid_flow, 0, 0, 0, request.command_id};
        uint8_t frame[128] = {};
        const size_t frame_len = buildAuthenticatedNodeFrame(manager, RfMessageType::TELEMETRY, 1,
            2, request.command_id, &telemetry,
            sizeof(telemetry), frame, sizeof(frame));
        TEST_ASSERT_FALSE(manager.handleIncomingFrame(frame, frame_len, 102));
        NodeState state{}; TEST_ASSERT_TRUE(registry.getNodeState(1, state));
        TEST_ASSERT_EQUAL(NodeHealthStatus::FAULT, state.health); TEST_ASSERT_TRUE(state.fault_latched);
        TEST_ASSERT_TRUE(manager.isPending(1));
        TEST_ASSERT_TRUE(std::strcmp("COMPLETED", outcomes.last_status) != 0);
        TEST_ASSERT_EQUAL_STRING("INVALID_OR_FAULT_TELEMETRY_SAFE_OFF", outcomes.last_audit);
        TEST_ASSERT_TRUE(manager.serviceCommandFanout(103));
        const size_t safe_off_offset = rf.getTxBuffer().size() -
            (RF_HEADER_SIZE + sizeof(SetPumpPayload) + HMAC_TAG_SIZE + 2);
        RfHeader safe_off_header{};
        TEST_ASSERT_TRUE(RfFrameCodec::decodeHeader(rf.getTxBuffer().data() + safe_off_offset,
                                                    RF_HEADER_SIZE, safe_off_header));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RfMessageType::SET_PUMP), safe_off_header.message_type);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodePumpState::OFF),
                                rf.getTxBuffer()[safe_off_offset + RF_HEADER_SIZE]);
    }
}

void test_off_residual_flow_latches_unexpected_flow_fault(void) {
    FakeRfTransport rf; NodeRegistry registry; CommandManager manager; FakeOutcomeSink outcomes;
    TEST_ASSERT_TRUE(rf.begin()); TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager)); manager.setOutcomeSink(&outcomes);
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::ON, 1, 100, 0, 1));
    TEST_ASSERT_TRUE(provisionTestNodePolicy(manager, 1));
    TEST_ASSERT_TRUE(manager.queueExternalNodeCommand(1, NodePumpState::OFF, "residual-flow"));
    TEST_ASSERT_TRUE(manager.serviceCommandFanout(100));
    RfHeader request{}; std::memcpy(&request, rf.getTxBuffer().data(), sizeof(request));
    TEST_ASSERT_TRUE(acknowledgePumpCommand(manager, request, 101));
    TelemetryPayload residual_flow{0, 0, 21, 0, 0, 0, request.command_id}; uint8_t frame[128] = {};
    const size_t frame_len = buildAuthenticatedNodeFrame(manager, RfMessageType::TELEMETRY, 1, 2,
        request.command_id, &residual_flow, sizeof(residual_flow), frame, sizeof(frame));
    TEST_ASSERT_FALSE(manager.handleIncomingFrame(frame, frame_len, 102));
    NodeState state{}; TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL(NodeHealthStatus::FAULT, state.health); TEST_ASSERT_TRUE(manager.isPending(1));
    TEST_ASSERT_EQUAL_STRING("UNEXPECTED_FLOW_FAULT_SAFE_OFF", outcomes.last_audit);
}

void test_mqtt_rf_command_correlation_and_ack_outcome(void) {
    FakeRfTransport rf;
    TEST_ASSERT_TRUE(rf.begin());
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "gateway-1", "pass", "gateway-1"};
    TEST_ASSERT_TRUE(mqtt.begin(cfg, nullptr, &registry, &manager));
    TEST_ASSERT_TRUE(mqtt.connect());
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));

    char policy_topic[] = "aeroponics/device/gateway-1/command/config/flow-policy";
    char policy_payload[] = "{\"command_id\":\"flow-policy-1\",\"version\":1,\"node_id\":1,\"policy_version\":1,\"treatment_version_id\":101,\"calibration_id\":1001,\"min_flow_lpm_x100\":50,\"max_off_flow_lpm_x100\":20,\"max_flow_lpm_x100\":600,\"flow_start_timeout_ms\":3000,\"run_lease_ms\":60000,\"max_on_duration_ms\":300000}";
    mqtt.simulateIncomingMessage(policy_topic, reinterpret_cast<uint8_t*>(policy_payload), strlen(policy_payload));
    mqtt.serviceIncomingCommands();
    TEST_ASSERT_TRUE(strstr(mqtt.mockLastPublishedPayload(), "\"status\":\"ACCEPTED\"") != nullptr);

    char topic[] = "aeroponics/device/gateway-1/command/node/1/override";
    char payload[] = "{\"command_id\":\"rf-cmd-1\",\"version\":1,\"desired_state\":\"ON\"}";
    mqtt.simulateIncomingMessage(topic, reinterpret_cast<uint8_t*>(payload), strlen(payload));
    mqtt.serviceIncomingCommands();
    TEST_ASSERT_EQUAL_STRING("aeroponics/device/gateway-1/ack/rf-cmd-1", mqtt.mockLastPublishedTopic());
    TEST_ASSERT_TRUE(strstr(mqtt.mockLastPublishedPayload(), "\"status\":\"ACCEPTED\"") != nullptr);
    TEST_ASSERT_TRUE(manager.serviceCommandFanout(1000));
    TEST_ASSERT_EQUAL_STRING("aeroponics/device/gateway-1/telemetry/command/rf-cmd-1/event",
                             mqtt.mockLastPublishedTopic());
    TEST_ASSERT_TRUE(strstr(mqtt.mockLastPublishedPayload(), "\"status\":\"QUEUED\"") != nullptr);

    const std::vector<uint8_t>& tx = rf.getTxBuffer();
    RfHeader request{};
    std::memcpy(&request, tx.data(), sizeof(request));
    uint8_t ack_frame[128] = {};
    const size_t ack_len = buildAuthenticatedNodeAck(manager, request, 1, 1, ack_frame, sizeof(ack_frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(ack_frame, ack_len, 1001));
    TEST_ASSERT_TRUE(strstr(mqtt.mockLastPublishedPayload(), "\"command_id\":\"rf-cmd-1\"") != nullptr);
    TEST_ASSERT_EQUAL_STRING("aeroponics/device/gateway-1/telemetry/command/rf-cmd-1/event",
                             mqtt.mockLastPublishedTopic());
    TEST_ASSERT_TRUE(strstr(mqtt.mockLastPublishedPayload(), "\"status\":\"RF_ACKED\"") != nullptr);
}

void test_rf_multi_frame_bounded_rx(void) {
    FakeRfTransport rf;
    TEST_ASSERT_TRUE(rf.begin());
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));

    TelemetryPayload t1{1, 1, 250, 1000, 450, 0, 0};
    uint8_t f1[128] = {};
    const size_t len1 = buildAuthenticatedNodeFrame(manager, RfMessageType::TELEMETRY, 1, 1, 0,
                                                    &t1, sizeof(t1), f1, sizeof(f1));

    TelemetryPayload t2{0, 0, 0, 1000, 450, 0, 0};
    uint8_t f2[128] = {};
    const uint8_t key[16] = {0xA5};
    const RfFrameMetadata second_node_metadata{2, 0, 1, 1, 0};
    const size_t len2 = RfFrameCodec::encodeFrame(second_node_metadata, RfMessageType::TELEMETRY,
                                                   &t2, sizeof(t2), key, sizeof(key), f2, sizeof(f2));

    uint8_t multi_buf[256];
    std::memcpy(multi_buf, f1, len1);
    std::memcpy(multi_buf + len1, f2, len2);

    rf.injectRxData(multi_buf, len1 + len2);
    TEST_ASSERT_EQUAL_UINT(len1 + len2, rf.available());
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(f1, len1, 1000));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(f2, len2, 1000));
}

void test_rejects_telemetry_from_previous_command_after_retry_and_new_command(void) {
    FakeRfTransport rf;
    TEST_ASSERT_TRUE(rf.begin());
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));
    TEST_ASSERT_TRUE(provisionTestNodePolicy(manager, 1));

    HeartbeatPayload heartbeat{1, -70, 90};
    uint8_t heartbeat_frame[128] = {};
    const size_t heartbeat_len = buildAuthenticatedNodeFrame(manager, RfMessageType::HEARTBEAT, 1, 1, 0,
                                                              &heartbeat, sizeof(heartbeat), heartbeat_frame,
                                                              sizeof(heartbeat_frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(heartbeat_frame, heartbeat_len, 10));

    TEST_ASSERT_TRUE(provisionTestNodePolicy(manager, 1));
    TEST_ASSERT_TRUE(manager.queueExternalNodeCommand(1, NodePumpState::ON, "command-a"));
    TEST_ASSERT_TRUE(manager.serviceCommandFanout(100));
    RfHeader request_a{};
    std::memcpy(&request_a, rf.getTxBuffer().data(), sizeof(request_a));
    // The retry must not create another actuator command/correlation key.
    TEST_ASSERT_TRUE(manager.serviceCommandFanout(1100));
    uint8_t ack_frame[128] = {};
    const RfFrameMetadata ack_metadata{1, 0, 1, 2, request_a.command_id};
    const CommandAckPayload ack{request_a.sequence, static_cast<uint8_t>(AckOutcome::SUCCESS), 1, 1, {0, 0, 0}};
    const uint8_t key[16] = {0xA5};
    const size_t ack_len = RfFrameCodec::encodeFrame(ack_metadata, RfMessageType::COMMAND_ACK,
        &ack, sizeof(ack), key, sizeof(key), ack_frame, sizeof(ack_frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(ack_frame, ack_len, 1110));

    TelemetryPayload telemetry_a{1, 1, 500, 9999, 1234, 0, request_a.command_id};
    uint8_t telemetry_a_frame[128] = {};
    const size_t telemetry_a_len = buildAuthenticatedNodeFrame(manager, RfMessageType::TELEMETRY, 1, 3,
                                                                 request_a.command_id, &telemetry_a,
                                                                 sizeof(telemetry_a), telemetry_a_frame,
                                                                 sizeof(telemetry_a_frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(telemetry_a_frame, telemetry_a_len, 1111));

    TEST_ASSERT_TRUE(manager.queueExternalNodeCommand(1, NodePumpState::OFF, "command-b"));
    TEST_ASSERT_TRUE(manager.serviceCommandFanout(1200));
    RfHeader request_b{};
    std::memcpy(&request_b, rf.getTxBuffer().data() + (rf.getTxBuffer().size() -
                 (sizeof(RfHeader) + sizeof(SetPumpPayload) + HMAC_TAG_SIZE + 2)), sizeof(request_b));

    TelemetryPayload old_telemetry{1, 1, 500, 9999, 1234, 0, request_a.command_id};
    uint8_t old_frame[128] = {};
    const size_t old_len = buildAuthenticatedNodeFrame(manager, RfMessageType::TELEMETRY, 1, 3,
                                                        request_a.command_id, &old_telemetry,
                                                        sizeof(old_telemetry), old_frame, sizeof(old_frame));
    TEST_ASSERT_FALSE(manager.handleIncomingFrame(old_frame, old_len, 1300));

    NodeState state{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    // The rejected old frame cannot overwrite telemetry already confirmed for A.
    TEST_ASSERT_EQUAL(NodePumpState::ON, state.reported_state);
    TEST_ASSERT_EQUAL_UINT8(1, state.driver_feedback);
    TEST_ASSERT_EQUAL_UINT16(500, state.flow_lpm_x100);
    TEST_ASSERT_EQUAL_UINT32(9999, state.delivered_volume_ml);
    TEST_ASSERT_EQUAL_UINT32(1111, state.last_seen_ms);
    TEST_ASSERT_TRUE(manager.isPending(1));
    TEST_ASSERT_NOT_EQUAL(request_a.command_id, request_b.command_id);
}

void test_applies_telemetry_only_for_current_command_and_session(void) {
    FakeRfTransport rf;
    TEST_ASSERT_TRUE(rf.begin());
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));

    HeartbeatPayload heartbeat{1, -70, 90};
    uint8_t heartbeat_frame[128] = {};
    const size_t heartbeat_len = buildAuthenticatedNodeFrame(manager, RfMessageType::HEARTBEAT, 7, 1, 0,
                                                              &heartbeat, sizeof(heartbeat), heartbeat_frame,
                                                              sizeof(heartbeat_frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(heartbeat_frame, heartbeat_len, 10));
    TEST_ASSERT_TRUE(provisionTestNodePolicy(manager, 1));
    TEST_ASSERT_TRUE(manager.queueExternalNodeCommand(1, NodePumpState::ON, "telemetry-current"));
    TEST_ASSERT_TRUE(manager.serviceCommandFanout(100));
    RfHeader request{};
    std::memcpy(&request, rf.getTxBuffer().data(), sizeof(request));

    TelemetryPayload telemetry{1, 1, 250, 1234, 450, 0, request.command_id};
    uint8_t frame[128] = {};
    const size_t length = buildAuthenticatedNodeFrame(manager, RfMessageType::TELEMETRY, 7, 2,
                                                      request.command_id, &telemetry, sizeof(telemetry),
                                                      frame, sizeof(frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(frame, length, 200));
    NodeState state{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL(NodePumpState::ON, state.reported_state);
    TEST_ASSERT_EQUAL_UINT8(1, state.driver_feedback);
    TEST_ASSERT_EQUAL_UINT16(250, state.flow_lpm_x100);
    TEST_ASSERT_EQUAL_UINT32(1234, state.delivered_volume_ml);
    TEST_ASSERT_EQUAL_UINT32(200, state.last_seen_ms);
}

void test_rejects_old_telemetry_and_fault_after_node_reboot_session_change(void) {
    FakeRfTransport rf;
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::ON, 1, 200, 1000, 100));

    HeartbeatPayload heartbeat{1, -70, 90};
    uint8_t first_session[128] = {};
    const size_t first_len = buildAuthenticatedNodeFrame(manager, RfMessageType::HEARTBEAT, 10, 1, 0,
                                                         &heartbeat, sizeof(heartbeat), first_session,
                                                         sizeof(first_session));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(first_session, first_len, 110));

    TelemetryPayload old_telemetry{0, 0, 0, 0, 0, 0, 0xCAFE};
    uint8_t reboot_frame[128] = {};
    const size_t reboot_len = buildAuthenticatedNodeFrame(manager, RfMessageType::HEARTBEAT, 11, 1, 0,
                                                          &heartbeat, sizeof(heartbeat), reboot_frame,
                                                          sizeof(reboot_frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(reboot_frame, reboot_len, 120));
    NodeState before_old_frame{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, before_old_frame));

    uint8_t old_frame[128] = {};
    const size_t old_len = buildAuthenticatedNodeFrame(manager, RfMessageType::TELEMETRY, 11, 2,
                                                       old_telemetry.last_command_id, &old_telemetry,
                                                       sizeof(old_telemetry), old_frame, sizeof(old_frame));
    TEST_ASSERT_FALSE(manager.handleIncomingFrame(old_frame, old_len, 130));

    FaultReportPayload fault{1, 123, 0, old_telemetry.last_command_id};
    uint8_t fault_frame[128] = {};
    const size_t fault_len = buildAuthenticatedNodeFrame(manager, RfMessageType::FAULT_REPORT, 11, 3,
                                                         fault.command_id, &fault, sizeof(fault), fault_frame,
                                                         sizeof(fault_frame));
    TEST_ASSERT_FALSE(manager.handleIncomingFrame(fault_frame, fault_len, 140));
    NodeState after_old_frame{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, after_old_frame));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(before_old_frame.reported_state),
                            static_cast<uint8_t>(after_old_frame.reported_state));
    TEST_ASSERT_EQUAL_UINT8(before_old_frame.driver_feedback, after_old_frame.driver_feedback);
    TEST_ASSERT_EQUAL_UINT16(before_old_frame.flow_lpm_x100, after_old_frame.flow_lpm_x100);
    TEST_ASSERT_EQUAL_UINT32(before_old_frame.delivered_volume_ml, after_old_frame.delivered_volume_ml);
    TEST_ASSERT_EQUAL_UINT32(before_old_frame.last_seen_ms, after_old_frame.last_seen_ms);
}

void test_rejects_malformed_fault_payload_without_mutating_registry(void) {
    FakeRfTransport rf;
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    uint8_t payload[sizeof(FaultReportPayload) + 1] = {};
    uint8_t frame[128] = {};
    const size_t length = buildAuthenticatedNodeFrame(manager, RfMessageType::FAULT_REPORT, 1, 1, 0,
                                                      payload, sizeof(payload), frame, sizeof(frame));
    TEST_ASSERT_FALSE(manager.handleIncomingFrame(frame, length, 100));
    NodeState state{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL(NodeHealthStatus::OFFLINE, state.health);
}

void test_stale_node_safe_off_and_reconnect_recovery(void) {
    NodeRegistry registry;
    TEST_ASSERT_TRUE(registry.begin());
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::ON, 1, 200, 1000, 1000));

    NodeState state;
    TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeHealthStatus::ONLINE), static_cast<uint8_t>(state.health));

    uint16_t newly_stale = registry.evaluateStaleNodes(21000, 15000);
    TEST_ASSERT_TRUE((newly_stale & (1 << 0)) != 0);

    TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeHealthStatus::STALE), static_cast<uint8_t>(state.health));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodePumpState::OFF), static_cast<uint8_t>(state.desired_state));
    TEST_ASSERT_TRUE(state.fault_latched);

    TEST_ASSERT_FALSE(registry.setDesiredState(1, NodePumpState::ON));
    TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodePumpState::OFF), static_cast<uint8_t>(state.desired_state));

    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 1000, 22000));
    TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeHealthStatus::FAULT), static_cast<uint8_t>(state.health));
    TEST_ASSERT_TRUE(state.fault_latched);

    TEST_ASSERT_TRUE(registry.resetFault(1));
    TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_FALSE(state.fault_latched);
}

void test_command_manager_queueing_and_idempotency(void) {
    FakeRfTransport rf;
    TEST_ASSERT_TRUE(rf.begin());
    NodeRegistry registry;
    TEST_ASSERT_TRUE(registry.begin());
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));

    TEST_ASSERT_TRUE(provisionTestNodePolicy(manager, 1));
    TEST_ASSERT_TRUE(manager.queueExternalNodeCommand(1, NodePumpState::ON, "cmd-101"));
    TEST_ASSERT_TRUE(manager.isPending(1));

    TEST_ASSERT_TRUE(manager.queueExternalNodeCommand(1, NodePumpState::ON, "cmd-101"));
    TEST_ASSERT_FALSE(manager.queueExternalNodeCommand(1, NodePumpState::OFF, "cmd-102"));

    TEST_ASSERT_TRUE(manager.serviceCommandFanout(1000));

    manager.cancelNodeCommands(1);
    TEST_ASSERT_FALSE(manager.isPending(1));

    TEST_ASSERT_TRUE(manager.queueExternalNodeCommand(1, NodePumpState::OFF, "cmd-102"));
    TEST_ASSERT_TRUE(manager.isPending(1));
}

void test_mqtt_callback_defers_command_manager_mutation_to_main_loop(void) {
    FakeRfTransport rf;
    NodeRegistry registry;
    CommandManager manager;
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "gateway-queue", "pass", "gateway-queue"};
    TEST_ASSERT_TRUE(rf.begin()); TEST_ASSERT_TRUE(registry.begin());
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf)); TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));
    TEST_ASSERT_TRUE(provisionTestNodePolicy(manager, 1));
    TEST_ASSERT_TRUE(mqtt.begin(cfg, nullptr, &registry, &manager)); TEST_ASSERT_TRUE(mqtt.connect());

    char topic[] = "aeroponics/device/gateway-queue/command/node/1/override";
    char payload[] = "{\"command_id\":\"deferred-on\",\"version\":1,\"desired_state\":\"ON\"}";
    mqtt.simulateIncomingMessage(topic, reinterpret_cast<uint8_t*>(payload), strlen(payload));
    TEST_ASSERT_FALSE(manager.isPending(1));
    mqtt.serviceIncomingCommands();
    TEST_ASSERT_TRUE(manager.isPending(1));
    TEST_ASSERT_EQUAL_STRING("aeroponics/device/gateway-queue/ack/deferred-on", mqtt.mockLastPublishedTopic());
}

void test_main_loop_serializes_interleaved_mqtt_policy_command_ack_and_telemetry(void) {
    FakeRfTransport rf;
    NodeRegistry registry;
    CommandManager manager;
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "gateway-stress", "pass", "gateway-stress"};
    TEST_ASSERT_TRUE(rf.begin()); TEST_ASSERT_TRUE(registry.begin());
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf)); TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));
    TEST_ASSERT_TRUE(mqtt.begin(cfg, nullptr, &registry, &manager)); TEST_ASSERT_TRUE(mqtt.connect());

    char policy_topic[] = "aeroponics/device/gateway-stress/command/config/flow-policy";
    char override_topic[] = "aeroponics/device/gateway-stress/command/node/1/override";
    uint16_t node_sequence = 1;
    for (uint8_t iteration = 1; iteration <= 8; ++iteration) {
        char policy[384] = {};
        char on_command[160] = {};
        char off_command[160] = {};
        std::snprintf(policy, sizeof(policy),
                      "{\"command_id\":\"stress-policy-%u\",\"version\":1,\"node_id\":1,\"policy_version\":%u,"
                      "\"treatment_version_id\":101,\"calibration_id\":1001,\"min_flow_lpm_x100\":50,"
                      "\"max_off_flow_lpm_x100\":20,\"max_flow_lpm_x100\":600,\"flow_start_timeout_ms\":3000,"
                      "\"run_lease_ms\":60000,\"max_on_duration_ms\":300000}", iteration, iteration);
        std::snprintf(on_command, sizeof(on_command),
                      "{\"command_id\":\"stress-on-%u\",\"version\":1,\"desired_state\":\"ON\"}", iteration);
        mqtt.simulateIncomingMessage(policy_topic, reinterpret_cast<uint8_t*>(policy), strlen(policy));
        mqtt.simulateIncomingMessage(override_topic, reinterpret_cast<uint8_t*>(on_command), strlen(on_command));
        mqtt.serviceIncomingCommands();
        TEST_ASSERT_TRUE(manager.isPending(1));
        TEST_ASSERT_TRUE(manager.serviceCommandFanout(1000U + iteration));
        const size_t on_offset = rf.getTxBuffer().size() - (RF_HEADER_SIZE + 9 + HMAC_TAG_SIZE + 2);
        RfHeader request{}; std::memcpy(&request, rf.getTxBuffer().data() + on_offset, sizeof(request));
        uint8_t frame[128] = {};
        CommandAckPayload on_ack{request.sequence, static_cast<uint8_t>(AckOutcome::SUCCESS), 1, 1, {0, 0, 0}};
        const size_t ack_len = buildAuthenticatedNodeFrame(manager, RfMessageType::COMMAND_ACK, 1, node_sequence++,
            request.command_id, &on_ack, sizeof(on_ack), frame, sizeof(frame));
        TEST_ASSERT_TRUE(manager.handleIncomingFrame(frame, ack_len, 1100U + iteration));
        TelemetryPayload on_telemetry{1, 1, 50, 0, 0, 0, request.command_id};
        const size_t on_len = buildAuthenticatedNodeFrame(manager, RfMessageType::TELEMETRY, 1, node_sequence++,
            request.command_id, &on_telemetry, sizeof(on_telemetry), frame, sizeof(frame));
        TEST_ASSERT_TRUE(manager.handleIncomingFrame(frame, on_len, 1200U + iteration));
        TEST_ASSERT_FALSE(manager.isPending(1));

        std::snprintf(off_command, sizeof(off_command),
                      "{\"command_id\":\"stress-off-%u\",\"version\":1,\"desired_state\":\"OFF\"}", iteration);
        mqtt.simulateIncomingMessage(override_topic, reinterpret_cast<uint8_t*>(off_command), strlen(off_command));
        mqtt.serviceIncomingCommands();
        TEST_ASSERT_TRUE(manager.serviceCommandFanout(1300U + iteration));
        const size_t offset = rf.getTxBuffer().size() - (RF_HEADER_SIZE + 9 + HMAC_TAG_SIZE + 2);
        std::memcpy(&request, rf.getTxBuffer().data() + offset, sizeof(request));
        CommandAckPayload off_ack{request.sequence, static_cast<uint8_t>(AckOutcome::SUCCESS), 0, 0, {0, 0, 0}};
        const size_t off_ack_len = buildAuthenticatedNodeFrame(manager, RfMessageType::COMMAND_ACK, 1, node_sequence++,
            request.command_id, &off_ack, sizeof(off_ack), frame, sizeof(frame));
        TEST_ASSERT_TRUE(manager.handleIncomingFrame(frame, off_ack_len, 1400U + iteration));
        TelemetryPayload off_telemetry{0, 0, 0, 0, 0, 0, request.command_id};
        const size_t off_len = buildAuthenticatedNodeFrame(manager, RfMessageType::TELEMETRY, 1, node_sequence++,
            request.command_id, &off_telemetry, sizeof(off_telemetry), frame, sizeof(frame));
        TEST_ASSERT_TRUE(manager.handleIncomingFrame(frame, off_len, 1500U + iteration));
        TEST_ASSERT_FALSE(manager.isPending(1));
    }
}

void test_mqtt_ack_admission_survives_full_telemetry_lane_and_publish_retry(void) {
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "gateway-ack", "pass", "gateway-ack"};
    TEST_ASSERT_TRUE(mqtt.begin(cfg));
    TEST_ASSERT_TRUE(mqtt.connect());
    mqtt.setMockPublishResult(false);

    for (size_t i = 0; i < MQTT_OUTBOUND_TELEMETRY_QUEUE_DEPTH; ++i) {
        TEST_ASSERT_TRUE(mqtt.publishGroupTelemetry(1, static_cast<uint32_t>(i), "BURST"));
    }
    char topic[] = "aeroponics/device/gateway-ack/command/config/assignment";
    char malformed[] = "{\"command_id\":\"overflow-malformed\",\"version\":1}";
    mqtt.simulateIncomingMessage(topic, reinterpret_cast<uint8_t*>(malformed), strlen(malformed));

    mqtt.setMockPublishResult(true);
    const size_t published_before = mqtt.mockPublishedTopicCount();
    mqtt.serviceOutgoingEvents();
    TEST_ASSERT_EQUAL_UINT(published_before + MQTT_OUTBOUND_DRAIN_BUDGET,
                           mqtt.mockPublishedTopicCount());
    TEST_ASSERT_EQUAL_STRING("aeroponics/device/gateway-ack/ack/overflow-malformed",
                             mqtt.mockPublishedTopic(published_before));
}

void test_mqtt_valid_command_ack_survives_full_telemetry_lane(void) {
    FakeRfTransport rf;
    NodeRegistry registry;
    CommandManager manager;
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "gateway-valid", "pass", "gateway-valid"};
    TEST_ASSERT_TRUE(rf.begin());
    TEST_ASSERT_TRUE(registry.begin());
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(mqtt.begin(cfg, nullptr, &registry, &manager));
    TEST_ASSERT_TRUE(mqtt.connect());
    mqtt.setMockPublishResult(false);
    for (size_t i = 0; i < MQTT_OUTBOUND_TELEMETRY_QUEUE_DEPTH; ++i) {
        TEST_ASSERT_TRUE(mqtt.publishGroupTelemetry(1, static_cast<uint32_t>(i), "BURST"));
    }

    char topic[] = "aeroponics/device/gateway-valid/command/config/flow-policy";
    char command[] =
        "{\"command_id\":\"overflow-valid\",\"version\":1,\"node_id\":1,\"policy_version\":1,"
        "\"treatment_version_id\":101,\"calibration_id\":1001,\"min_flow_lpm_x100\":50,"
        "\"max_off_flow_lpm_x100\":20,\"max_flow_lpm_x100\":600,\"flow_start_timeout_ms\":3000,"
        "\"run_lease_ms\":60000,\"max_on_duration_ms\":300000}";
    mqtt.simulateIncomingMessage(topic, reinterpret_cast<uint8_t*>(command), strlen(command));
    mqtt.serviceIncomingCommands();

    mqtt.setMockPublishResult(true);
    const size_t published_before = mqtt.mockPublishedTopicCount();
    mqtt.serviceOutgoingEvents();
    TEST_ASSERT_EQUAL_STRING("aeroponics/device/gateway-valid/ack/overflow-valid",
                             mqtt.mockPublishedTopic(published_before));
}

void test_mqtt_outbound_drain_is_bounded_and_telemetry_fifo(void) {
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "gateway-budget", "pass", "gateway-budget"};
    TEST_ASSERT_TRUE(mqtt.begin(cfg));
    TEST_ASSERT_TRUE(mqtt.connect());
    mqtt.setMockPublishResult(false);
    for (size_t i = 0; i < MQTT_OUTBOUND_TELEMETRY_QUEUE_DEPTH; ++i) {
        TEST_ASSERT_TRUE(mqtt.publishGroupTelemetry(static_cast<uint8_t>(i + 1), 0, "BURST"));
    }

    mqtt.setMockPublishResult(true);
    const size_t published_before = mqtt.mockPublishedTopicCount();
    mqtt.serviceOutgoingEvents();
    TEST_ASSERT_EQUAL_UINT(published_before + MQTT_OUTBOUND_DRAIN_BUDGET,
                           mqtt.mockPublishedTopicCount());
    TEST_ASSERT_EQUAL_STRING("aeroponics/device/gateway-budget/telemetry/group/1",
                             mqtt.mockPublishedTopic(published_before));
    TEST_ASSERT_EQUAL_STRING("aeroponics/device/gateway-budget/telemetry/group/8",
                             mqtt.mockPublishedTopic(published_before + MQTT_OUTBOUND_DRAIN_BUDGET - 1));
}

void test_mqtt_ack_reservation_backpressures_before_any_command_mutation(void) {
    FakeRfTransport rf;
    NodeRegistry registry;
    CommandManager manager;
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "gateway-admission", "pass", "gateway-admission"};
    TEST_ASSERT_TRUE(rf.begin()); TEST_ASSERT_TRUE(registry.begin());
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf)); TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));
    TEST_ASSERT_TRUE(provisionTestNodePolicy(manager, 1));
    TEST_ASSERT_TRUE(mqtt.begin(cfg, nullptr, &registry, &manager)); TEST_ASSERT_TRUE(mqtt.connect());
    mqtt.setMockPublishResult(false);
    const size_t published_before = mqtt.mockPublishedTopicCount();

    char topic[] = "aeroponics/device/gateway-admission/command/node/1/override";
    for (size_t i = 0; i < MQTT_OUTBOUND_ACK_QUEUE_DEPTH; ++i) {
        char payload[128] = {};
        std::snprintf(payload, sizeof(payload),
                      "{\"command_id\":\"reject-%u\",\"version\":1}", static_cast<unsigned>(i));
        mqtt.simulateIncomingMessage(topic, reinterpret_cast<uint8_t*>(payload), strlen(payload));
    }
    char command[] = "{\"command_id\":\"must-not-mutate\",\"version\":1,\"desired_state\":\"ON\"}";
    mqtt.simulateIncomingMessage(topic, reinterpret_cast<uint8_t*>(command), strlen(command));
    mqtt.serviceIncomingCommands();
    TEST_ASSERT_FALSE(manager.isPending(1));

    mqtt.setMockPublishResult(true);
    // The 17th command cannot enter the ordinary ACK lane. It must still be
    // represented by the independent backpressure failure FIFO.
    for (size_t i = 0; i < MQTT_OUTBOUND_ACK_QUEUE_DEPTH + 1; ++i) mqtt.serviceOutgoingEvents();
    const size_t expected_rejections = MQTT_OUTBOUND_ACK_QUEUE_DEPTH + 1;
    TEST_ASSERT_EQUAL_UINT(published_before + expected_rejections, mqtt.mockPublishedTopicCount());
    for (size_t i = 0; i < expected_rejections; ++i) {
        char expected[MQTT_TOPIC_BUFFER_SIZE] = {};
        if (i < MQTT_OUTBOUND_ACK_QUEUE_DEPTH) {
            std::snprintf(expected, sizeof(expected), "aeroponics/device/gateway-admission/ack/reject-%u",
                          static_cast<unsigned>(i));
        } else {
            std::snprintf(expected, sizeof(expected), "aeroponics/device/gateway-admission/ack/must-not-mutate");
        }
        TEST_ASSERT_EQUAL_STRING(expected, mqtt.mockPublishedTopic(published_before + i));
        TEST_ASSERT_TRUE(strstr(mqtt.mockPublishedPayload(published_before + i),
                                "\"status\":\"REJECTED\"") != nullptr);
        TEST_ASSERT_TRUE(mqtt.mockPublishedRetained(published_before + i));
    }
}

void test_group_command_prepare_failure_leaves_all_nodes_unchanged(void) {
    FakeRfTransport rf;
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(rf.begin()); TEST_ASSERT_TRUE(registry.begin());
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf)); TEST_ASSERT_TRUE(provisionTestPsk(manager));
    for (uint8_t node_id = 1; node_id <= 2; ++node_id) {
        TEST_ASSERT_TRUE(registry.assignNodeToGroup(node_id, 1));
        TEST_ASSERT_TRUE(registry.updateTelemetry(node_id, NodePumpState::OFF, 0, 0, 0, node_id));
        TEST_ASSERT_TRUE(provisionTestNodePolicy(manager, node_id));
    }
    TEST_ASSERT_TRUE(registry.updateHealth(2, NodeHealthStatus::FAULT));
    TEST_ASSERT_FALSE(manager.queueExternalGroupCommand(1, NodePumpState::ON, "group-atomic-fail"));
    for (uint8_t node_id = 1; node_id <= 2; ++node_id) {
        NodeState state{}; TEST_ASSERT_TRUE(registry.getNodeState(node_id, state));
        TEST_ASSERT_EQUAL(NodePumpState::OFF, state.desired_state);
        TEST_ASSERT_FALSE(manager.isPending(node_id));
    }
}

void test_group_command_commits_all_prepared_nodes(void) {
    FakeRfTransport rf;
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(rf.begin()); TEST_ASSERT_TRUE(registry.begin());
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf)); TEST_ASSERT_TRUE(provisionTestPsk(manager));
    for (uint8_t node_id = 1; node_id <= 2; ++node_id) {
        TEST_ASSERT_TRUE(registry.assignNodeToGroup(node_id, 1));
        TEST_ASSERT_TRUE(registry.updateTelemetry(node_id, NodePumpState::OFF, 0, 0, 0, node_id));
        TEST_ASSERT_TRUE(provisionTestNodePolicy(manager, node_id));
    }
    TEST_ASSERT_TRUE(manager.queueExternalGroupCommand(1, NodePumpState::ON, "group-atomic-ok"));
    for (uint8_t node_id = 1; node_id <= 2; ++node_id) {
        NodeState state{}; TEST_ASSERT_TRUE(registry.getNodeState(node_id, state));
        TEST_ASSERT_EQUAL(NodePumpState::ON, state.desired_state);
        TEST_ASSERT_TRUE(manager.isPending(node_id));
    }
}

void test_mqtt_gateway_domain_publishing_and_assignment_command(void) {
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "gateway-1", "pass", "gateway-1"};
    NodeRegistry registry;
    FakeRfTransport rf;
    TEST_ASSERT_TRUE(rf.begin());
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));

    TEST_ASSERT_TRUE(mqtt.begin(cfg, nullptr, &registry, &manager));
    TEST_ASSERT_TRUE(mqtt.connect());

    // 1. Group telemetry
    TEST_ASSERT_TRUE(mqtt.publishGroupTelemetry(1, 0x05, "SPRAYING"));
    TEST_ASSERT_EQUAL_STRING("aeroponics/device/gateway-1/telemetry/group/1", mqtt.mockLastPublishedTopic());
    TEST_ASSERT_TRUE(strstr(mqtt.mockLastPublishedPayload(), "\"state\":\"SPRAYING\"") != nullptr);

    // 2. Node snapshot
    NodeState st{};
    st.node_id = 1;
    st.group_id = 1;
    st.desired_state = NodePumpState::ON;
    st.reported_state = NodePumpState::ON;
    st.driver_feedback = true;
    st.flow_lpm_x100 = 250;
    st.delivered_volume_ml = 1000;
    st.health = NodeHealthStatus::ONLINE;

    TEST_ASSERT_TRUE(mqtt.publishNodeSnapshot(1, st));
    TEST_ASSERT_EQUAL_STRING("aeroponics/device/gateway-1/telemetry/node/1/snapshot", mqtt.mockLastPublishedTopic());

    // 3. Command Assignment
    char assign_topic[] = "aeroponics/device/gateway-1/command/config/assignment";
    char assign_payload[] = "{\"command_id\":\"cmd-999\",\"version\":1,\"node_id\":3,\"group_id\":2}";
    mqtt.simulateIncomingMessage(assign_topic, (uint8_t*)assign_payload, strlen(assign_payload));
    mqtt.serviceIncomingCommands();

    TEST_ASSERT_EQUAL_UINT8(0, registry.getNodeGroup(3));
    TEST_ASSERT_EQUAL_STRING("aeroponics/device/gateway-1/ack/cmd-999", mqtt.mockLastPublishedTopic());
    TEST_ASSERT_TRUE(strstr(mqtt.mockLastPublishedPayload(), "\"status\":\"ACCEPTED\"") != nullptr);
}

void test_offline_stale_and_fault_nodes_reject_on_but_allow_safe_off(void) {
    FakeRfTransport rf;
    TEST_ASSERT_TRUE(rf.begin());
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));

    TEST_ASSERT_FALSE(manager.queueExternalNodeCommand(1, NodePumpState::ON, "offline-on"));
    TEST_ASSERT_EQUAL_UINT(0, rf.getTxBuffer().size());
    TEST_ASSERT_TRUE(manager.queueExternalNodeCommand(1, NodePumpState::OFF, "offline-off"));
    TEST_ASSERT_TRUE(manager.serviceCommandFanout(1000));
    TEST_ASSERT_TRUE(rf.getTxBuffer().size() > 0);
    manager.cancelNodeCommands(1);

    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1001));
    TEST_ASSERT_TRUE(registry.updateHealth(1, NodeHealthStatus::STALE));
    TEST_ASSERT_FALSE(manager.queueExternalNodeCommand(1, NodePumpState::ON, "stale-on"));
    TEST_ASSERT_TRUE(registry.resetFault(1));
    TEST_ASSERT_TRUE(registry.updateHealth(1, NodeHealthStatus::FAULT));
    TEST_ASSERT_FALSE(manager.queueExternalNodeCommand(1, NodePumpState::ON, "fault-on"));
}

void test_command_id_is_a_safe_mqtt_topic_segment(void) {
    const char* valid_ids[] = {"cmd-101", "a_B-9", "550e8400-e29b-41d4-a716-446655440000"};
    const char control_id[] = {'b', 'a', 'd', 0x01, '\0'};
    const char* invalid_ids[] = {"audit/evil", "plus+", "hash#", "white space", control_id, "định-danh"};
    for (const char* id : valid_ids) TEST_ASSERT_TRUE(isValidMqttCommandId(id));
    for (const char* id : invalid_ids) TEST_ASSERT_FALSE(isValidMqttCommandId(id));
    char oversized[66] = {};
    memset(oversized, 'a', 65);
    TEST_ASSERT_FALSE(isValidMqttCommandId(oversized));
}

void test_reassignment_commits_only_after_rf_safe_off_ack(void) {
    FakeRfTransport rf;
    TEST_ASSERT_TRUE(rf.begin());
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::ON, 1, 0, 0, 1));

    TEST_ASSERT_TRUE(manager.requestNodeReassignment(1, 2, "move-1"));
    TEST_ASSERT_EQUAL_UINT8(1, registry.getNodeGroup(1));
    TEST_ASSERT_TRUE(manager.serviceCommandFanout(1000));
    RfHeader request{};
    std::memcpy(&request, rf.getTxBuffer().data(), sizeof(request));
    SetPumpPayload payload{};
    std::memcpy(&payload, rf.getTxBuffer().data() + sizeof(request), sizeof(payload));
    TEST_ASSERT_EQUAL_UINT8(0, payload.desired_state);

    uint8_t ack_frame[128] = {};
    const size_t ack_len = buildAuthenticatedNodeAck(manager, request, 0, 0, ack_frame, sizeof(ack_frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(ack_frame, ack_len, 1001));
    TelemetryPayload telemetry{0, 0, 0, 0, 0, 0, request.command_id};
    uint8_t telemetry_frame[128] = {};
    const size_t telemetry_len = buildAuthenticatedNodeFrame(manager, RfMessageType::TELEMETRY, 1, 2,
                                                              request.command_id, &telemetry, sizeof(telemetry),
                                                              telemetry_frame, sizeof(telemetry_frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(telemetry_frame, telemetry_len, 1002));
    TEST_ASSERT_FALSE(manager.isPending(1));
    TEST_ASSERT_EQUAL_UINT8(2, registry.getNodeGroup(1));
}

void test_reassignment_rf_safe_off_timeout_keeps_old_mapping_and_latches_fault(void) {
    FakeRfTransport rf;
    TEST_ASSERT_TRUE(rf.begin());
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::ON, 1, 0, 0, 1));
    TEST_ASSERT_TRUE(manager.requestNodeReassignment(1, 2, "move-timeout"));
    for (uint32_t now = 1000; now <= 4000; now += 1000) manager.serviceCommandFanout(now);

    NodeState state{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL_UINT8(1, registry.getNodeGroup(1));
    TEST_ASSERT_EQUAL(NodeHealthStatus::FAULT, state.health);
    TEST_ASSERT_EQUAL(NodePumpState::OFF, state.desired_state);
}

void test_reassignment_cancel_or_nack_never_commits_mapping(void) {
    FakeRfTransport rf;
    TEST_ASSERT_TRUE(rf.begin());
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::ON, 1, 0, 0, 1));

    TEST_ASSERT_TRUE(manager.requestNodeReassignment(1, 2, "move-cancel"));
    manager.cancelNodeCommands(1);
    NodeState state{};
    TEST_ASSERT_EQUAL_UINT8(1, registry.getNodeGroup(1));
    TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_TRUE(state.fault_latched);

    TEST_ASSERT_TRUE(registry.resetFault(1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::ON, 1, 0, 0, 2));
    TEST_ASSERT_TRUE(manager.requestNodeReassignment(1, 2, "move-nack"));
    TEST_ASSERT_TRUE(manager.serviceCommandFanout(1000));
    RfHeader request{};
    std::memcpy(&request, rf.getTxBuffer().data(), sizeof(request));
    uint8_t nack_frame[128] = {};
    CommandAckPayload nack{request.sequence, static_cast<uint8_t>(AckOutcome::FAULT_LOCKOUT), 1, 1, {0, 0, 0}};
    const RfFrameMetadata nack_metadata{1, 0, request.boot_session_id, request.sequence, request.command_id};
    const uint8_t key[16] = {0xA5};
    const size_t nack_len = RfFrameCodec::encodeFrame(nack_metadata, RfMessageType::COMMAND_ACK,
        &nack, sizeof(nack), key, sizeof(key), nack_frame, sizeof(nack_frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(nack_frame, nack_len, 1001));
    TEST_ASSERT_EQUAL_UINT8(1, registry.getNodeGroup(1));
}

void test_published_treatment_is_required_before_scheduler_activation(void) {
    NodeRegistry registry;
    FakeClock clock(12, true);
    GroupScheduleManager scheduler;
    TEST_ASSERT_TRUE(scheduler.begin(&clock, &registry));
    TEST_ASSERT_FALSE(scheduler.setGroupActive(1, true));
    PublishedTreatmentAssignment draft{};
    TEST_ASSERT_FALSE(scheduler.applyPublishedTreatment(1, draft));
    PublishedTreatmentAssignment published{7, 42, 3, GroupProfile{30, 300, 30, 600}};
    TEST_ASSERT_TRUE(scheduler.applyPublishedTreatment(1, published));
}

void test_invalid_flow_or_fault_telemetry_latches_safe_off(void) {
    FakeRfTransport rf;
    TEST_ASSERT_TRUE(rf.begin());
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));
    TEST_ASSERT_TRUE(provisionTestNodePolicy(manager, 1));
    TelemetryPayload telemetry{1, 1, 601, 0, 0, 0, 0};
    uint8_t frame[128] = {};
    const size_t len = buildAuthenticatedNodeFrame(manager, RfMessageType::TELEMETRY, 1, 1, 0,
                                                   &telemetry, sizeof(telemetry), frame, sizeof(frame));
    TEST_ASSERT_FALSE(manager.handleIncomingFrame(frame, len, 2));
    NodeState state{}; TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL(NodeHealthStatus::FAULT, state.health);
}

void test_authenticated_heartbeat_refreshes_liveness_without_pump_inference(void) {
    FakeRfTransport rf;
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));

    HeartbeatPayload heartbeat{20, -70, 80};
    uint8_t frame[128] = {};
    const size_t length = buildAuthenticatedNodeFrame(manager, RfMessageType::HEARTBEAT, 1, 1, 0,
                                                       &heartbeat, sizeof(heartbeat), frame, sizeof(frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(frame, length, 10000));
    NodeState state{}; TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL(NodePumpState::OFF, state.reported_state);
    TEST_ASSERT_EQUAL_UINT32(10000, state.last_seen_ms);
    TEST_ASSERT_EQUAL_UINT(0, registry.evaluateStaleNodes(25000, 15000));

    frame[sizeof(RfHeader) + sizeof(heartbeat)] ^= 0xFF;
    TEST_ASSERT_FALSE(manager.handleIncomingFrame(frame, length, 30000));
    TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL_UINT32(10000, state.last_seen_ms);
}

void test_authenticated_ac_heartbeat_refreshes_liveness_and_reserved_battery_value_is_rejected(void) {
    FakeRfTransport rf;
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));

    HeartbeatPayload ac_heartbeat{20, -70, 255};
    uint8_t frame[128] = {};
    const size_t ac_length = buildAuthenticatedNodeFrame(manager, RfMessageType::HEARTBEAT, 1, 1, 0,
                                                          &ac_heartbeat, sizeof(ac_heartbeat), frame, sizeof(frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(frame, ac_length, 10000));
    NodeState state{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL_UINT32(10000, state.last_seen_ms);

    HeartbeatPayload reserved_battery_value{21, -70, 101};
    const size_t invalid_length = buildAuthenticatedNodeFrame(manager, RfMessageType::HEARTBEAT, 1, 2, 0,
                                                               &reserved_battery_value, sizeof(reserved_battery_value),
                                                               frame, sizeof(frame));
    TEST_ASSERT_FALSE(manager.handleIncomingFrame(frame, invalid_length, 20000));
    TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL_UINT32(10000, state.last_seen_ms);
}

void test_node_reboot_session_queues_explicit_safe_off(void) {
    FakeRfTransport rf;
    TEST_ASSERT_TRUE(rf.begin());
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));

    HeartbeatPayload heartbeat{1, -70, 90};
    uint8_t frame[128] = {};
    const size_t length = buildAuthenticatedNodeFrame(manager, RfMessageType::HEARTBEAT, 1, 1, 0,
                                                       &heartbeat, sizeof(heartbeat), frame, sizeof(frame));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(frame, length, 2));
    // A higher boot-session is an authenticated reboot transition.
    const size_t reboot_length = buildAuthenticatedNodeFrame(manager, RfMessageType::HEARTBEAT, 2, 1, 0,
                                                              &heartbeat, sizeof(heartbeat), frame, sizeof(frame));
    TEST_ASSERT_EQUAL_UINT(length, reboot_length);
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(frame, length, 3));
    TEST_ASSERT_TRUE(manager.serviceCommandFanout(3));
    RfHeader request{}; std::memcpy(&request, rf.getTxBuffer().data(), sizeof(request));
    SetPumpPayload command{}; std::memcpy(&command, rf.getTxBuffer().data() + sizeof(request), sizeof(command));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RfMessageType::SET_PUMP), request.message_type);
    TEST_ASSERT_EQUAL_UINT8(0, command.desired_state);
    NodeState state{}; TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_TRUE(state.fault_latched);
}

void test_regression_no_legacy_relay_symbols_in_production_config(void) {
    TEST_ASSERT_NULL(strstr(MQTT_COMMAND_TREATMENT_SUFFIX, "relay"));
    TEST_ASSERT_NULL(strstr(MQTT_COMMAND_ASSIGNMENT_SUFFIX, "relay"));
    TEST_ASSERT_NULL(strstr(MQTT_TELEMETRY_GROUP_SUFFIX, "relay"));
    TEST_ASSERT_NULL(strstr(MQTT_TELEMETRY_NODE_SUFFIX, "relay"));
    TEST_ASSERT_NULL(strstr(MQTT_COMMAND_NODE_OVERRIDE_SUFFIX, "relay"));
    TEST_ASSERT_NULL(strstr(MQTT_COMMAND_GROUP_CONTROL_SUFFIX, "relay"));
}

void test_pump_feedback_normal_cycle_with_multi_tier_confirmation() {
    PumpFeedbackEvaluator evaluator;
    evaluator.update(0, false, false, 0, 0.0f);
    TEST_ASSERT_EQUAL(PUMP_HEALTH_OFF_HEALTHY, evaluator.getHealthState());
    TEST_ASSERT_FALSE(evaluator.isFaultLatched());
    TEST_ASSERT_TRUE(evaluator.isSafeOff());

    evaluator.update(100, true, true, 5500, 0.0f);
    TEST_ASSERT_EQUAL(PUMP_HEALTH_STARTING_INRUSH, evaluator.getHealthState());
    TEST_ASSERT_FALSE(evaluator.isFaultLatched());

    evaluator.update(150, true, true, 2100, 0.1f);
    TEST_ASSERT_EQUAL(PUMP_HEALTH_STARTING_INRUSH, evaluator.getHealthState());
    TEST_ASSERT_TRUE(evaluator.isDriverFeedbackActive());
    TEST_ASSERT_TRUE(evaluator.isLoadCurrentActive());
    TEST_ASSERT_FALSE(evaluator.isFlowConfirmed());

    evaluator.update(500, true, true, 2050, 2.4f);
    TEST_ASSERT_EQUAL(PUMP_HEALTH_RUNNING_CONFIRMED, evaluator.getHealthState());
    TEST_ASSERT_TRUE(evaluator.isPumpConfirmedRunning());
    TEST_ASSERT_TRUE(evaluator.isFlowConfirmed());

    evaluator.update(10000, false, false, 0, 0.0f);
    TEST_ASSERT_EQUAL(PUMP_HEALTH_OFF_HEALTHY, evaluator.getHealthState());
    TEST_ASSERT_TRUE(evaluator.isSafeOff());
}

void test_pump_feedback_driver_mismatch_on_and_off() {
    PumpFeedbackEvaluator evaluator;
    evaluator.update(0, false, false, 0, 0.0f);

    evaluator.update(100, true, false, 0, 0.0f);
    TEST_ASSERT_FALSE(evaluator.isFaultLatched());
    evaluator.update(135, true, false, 0, 0.0f);
    TEST_ASSERT_TRUE(evaluator.isFaultLatched());
    TEST_ASSERT_EQUAL(FEEDBACK_FAULT_DRIVER_MISMATCH, evaluator.getFaultCode());
    TEST_ASSERT_EQUAL(PUMP_HEALTH_FAULT_LATCHED, evaluator.getHealthState());

    evaluator.resetFault();
    TEST_ASSERT_FALSE(evaluator.isFaultLatched());

    evaluator.update(200, false, true, 0, 0.0f);
    evaluator.update(235, false, true, 0, 0.0f);
    TEST_ASSERT_TRUE(evaluator.isFaultLatched());
    TEST_ASSERT_EQUAL(FEEDBACK_FAULT_DRIVER_MISMATCH, evaluator.getFaultCode());
}

void test_pump_feedback_inrush_blanking_and_sustained_overcurrent_stall() {
    PumpFeedbackEvaluator evaluator;
    evaluator.update(0, false, false, 0, 0.0f);

    evaluator.update(100, true, true, 2000, 0.0f);
    evaluator.update(120, true, true, 6000, 0.0f);
    TEST_ASSERT_FALSE(evaluator.isFaultLatched());

    evaluator.update(200, true, true, 4500, 0.0f);
    TEST_ASSERT_FALSE(evaluator.isFaultLatched());
    evaluator.update(230, true, true, 4500, 0.0f);
    TEST_ASSERT_FALSE(evaluator.isFaultLatched());

    evaluator.update(255, true, true, 4500, 0.0f);
    TEST_ASSERT_TRUE(evaluator.isFaultLatched());
    TEST_ASSERT_EQUAL(FEEDBACK_FAULT_OVERCURRENT_STALL, evaluator.getFaultCode());
    TEST_ASSERT_EQUAL(PUMP_HEALTH_FAULT_LATCHED, evaluator.getHealthState());
}

void test_pump_feedback_open_load_broken_wire() {
    PumpFeedbackEvaluator evaluator;
    evaluator.update(0, false, false, 0, 0.0f);

    evaluator.update(100, true, true, 0, 0.0f);
    TEST_ASSERT_FALSE(evaluator.isFaultLatched());
    evaluator.update(200, true, true, 50, 0.0f);
    TEST_ASSERT_FALSE(evaluator.isFaultLatched());

    evaluator.update(260, true, true, 50, 0.0f);
    TEST_ASSERT_TRUE(evaluator.isFaultLatched());
    TEST_ASSERT_EQUAL(FEEDBACK_FAULT_OPEN_LOAD, evaluator.getFaultCode());
}

void test_pump_feedback_stuck_on_relay_or_shorted_fet() {
    PumpFeedbackEvaluator evaluator;
    evaluator.update(0, false, false, 0, 0.0f);

    evaluator.update(100, false, false, 1800, 0.0f);
    TEST_ASSERT_FALSE(evaluator.isFaultLatched());

    evaluator.update(260, false, false, 1800, 0.0f);
    TEST_ASSERT_TRUE(evaluator.isFaultLatched());
    TEST_ASSERT_EQUAL(FEEDBACK_FAULT_STUCK_ON, evaluator.getFaultCode());
}

void test_pump_feedback_dry_run_differentiation_vs_clogged_nozzle() {
    PumpFeedbackEvaluator evaluator;

    evaluator.update(0, false, false, 0, 0.0f);
    evaluator.update(100, true, true, 800, 0.0f);
    evaluator.update(3000, true, true, 800, 0.0f);
    TEST_ASSERT_FALSE(evaluator.isFaultLatched());

    evaluator.update(3150, true, true, 800, 0.0f);
    TEST_ASSERT_TRUE(evaluator.isFaultLatched());
    TEST_ASSERT_EQUAL(FEEDBACK_FAULT_DRY_RUN, evaluator.getFaultCode());

    evaluator.resetFault();
    evaluator.update(4000, true, true, 2100, 0.0f);
    evaluator.update(7050, true, true, 2100, 0.0f);
    TEST_ASSERT_TRUE(evaluator.isFaultLatched());
    TEST_ASSERT_EQUAL(FEEDBACK_FAULT_NO_FLOW, evaluator.getFaultCode());
}

void test_pump_feedback_over_range_flow_pipe_burst() {
    PumpFeedbackEvaluator evaluator;
    evaluator.update(0, false, false, 0, 0.0f);

    evaluator.update(100, true, true, 1900, 0.0f);
    evaluator.update(200, true, true, 1900, 2.0f);
    TEST_ASSERT_EQUAL(PUMP_HEALTH_RUNNING_CONFIRMED, evaluator.getHealthState());

    evaluator.update(300, true, true, 1600, 7.5f);
    TEST_ASSERT_TRUE(evaluator.isFaultLatched());
    TEST_ASSERT_EQUAL(FEEDBACK_FAULT_OVER_RANGE_FLOW, evaluator.getFaultCode());
    TEST_ASSERT_EQUAL(PUMP_HEALTH_FAULT_LATCHED, evaluator.getHealthState());
}

void test_pump_feedback_fault_latching_and_explicit_reset() {
    PumpFeedbackEvaluator evaluator;
    evaluator.update(0, false, false, 0, 0.0f);

    evaluator.update(100, true, true, 0, 0.0f);
    evaluator.update(300, true, true, 0, 0.0f);
    TEST_ASSERT_TRUE(evaluator.isFaultLatched());
    TEST_ASSERT_EQUAL(FEEDBACK_FAULT_OPEN_LOAD, evaluator.getFaultCode());

    evaluator.update(400, true, true, 2000, 2.5f);
    TEST_ASSERT_TRUE(evaluator.isFaultLatched());
    TEST_ASSERT_EQUAL(FEEDBACK_FAULT_OPEN_LOAD, evaluator.getFaultCode());
    TEST_ASSERT_EQUAL(PUMP_HEALTH_FAULT_LATCHED, evaluator.getHealthState());

    evaluator.resetFault();
    TEST_ASSERT_FALSE(evaluator.isFaultLatched());
    TEST_ASSERT_EQUAL(FEEDBACK_FAULT_NONE, evaluator.getFaultCode());
}

void test_flow_calibration_profile_validation_and_crc() {
    FlowCalibrationEngine engine;
    TEST_ASSERT_FALSE(engine.hasValidProfile());

    SensorCalibrationProfile profile{};
    profile.calibration_id = 101;
    profile.version = 1;
    profile.node_id = 1;
    std::strncpy(profile.sensor_serial, "OF06-2026-0042", sizeof(profile.sensor_serial) - 1);
    profile.nominal_pulses_per_litre = 4450;
    profile.low_flow_cutoff_lpm_x100 = 15; // 0.15 L/min
    profile.max_flow_limit_lpm_x100 = 600; // 6.00 L/min
    profile.num_calibration_points = 5;

    // 5 Calibration points (increasing frequency)
    profile.points[0] = {35, 259, 4410};   // 0.35 L/min, 25.9 Hz, 4410 p/L
    profile.points[1] = {120, 888, 4438};  // 1.20 L/min, 88.8 Hz, 4438 p/L
    profile.points[2] = {250, 1856, 4456}; // 2.50 L/min, 185.6 Hz, 4456 p/L
    profile.points[3] = {400, 2980, 4470}; // 4.00 L/min, 298.0 Hz, 4470 p/L
    profile.points[4] = {550, 4110, 4483}; // 5.50 L/min, 411.0 Hz, 4483 p/L

    uint32_t computed_crc = FlowCalibrationEngine::calculateProfileCrc32(profile);
    profile.checksum_crc32 = computed_crc;

    TEST_ASSERT_TRUE(engine.loadProfile(profile));
    TEST_ASSERT_TRUE(engine.hasValidProfile());
    TEST_ASSERT_EQUAL_UINT32(computed_crc, engine.getProfile().checksum_crc32);

    // Invalid profile: non-monotonic frequency
    SensorCalibrationProfile bad_profile = profile;
    bad_profile.points[3].pulse_freq_hz_x10 = 500; // Lower than points[2]
    bad_profile.checksum_crc32 = FlowCalibrationEngine::calculateProfileCrc32(bad_profile);
    TEST_ASSERT_FALSE(engine.loadProfile(bad_profile));

    // Invalid profile: wrong CRC
    SensorCalibrationProfile bad_crc_profile = profile;
    bad_crc_profile.checksum_crc32 = computed_crc ^ 0xFFFFFFFF;
    TEST_ASSERT_FALSE(engine.loadProfile(bad_crc_profile));
}

void test_flow_calibration_piecewise_k_factor_interpolation() {
    FlowCalibrationEngine engine;
    SensorCalibrationProfile profile{};
    profile.nominal_pulses_per_litre = 4450;
    profile.low_flow_cutoff_lpm_x100 = 15;
    profile.max_flow_limit_lpm_x100 = 600;
    profile.num_calibration_points = 5;

    profile.points[0] = {35, 250, 4400};   // 25.0 Hz -> 4400 p/L
    profile.points[1] = {120, 880, 4440};  // 88.0 Hz -> 4440 p/L
    profile.points[2] = {250, 1850, 4460}; // 185.0 Hz -> 4460 p/L
    profile.points[3] = {400, 3000, 4480}; // 300.0 Hz -> 4480 p/L
    profile.points[4] = {550, 4150, 4500}; // 415.0 Hz -> 4500 p/L
    profile.checksum_crc32 = FlowCalibrationEngine::calculateProfileCrc32(profile);

    TEST_ASSERT_TRUE(engine.loadProfile(profile));

    // Below first point -> clamps to points[0]
    TEST_ASSERT_EQUAL_UINT32(4400, engine.interpolateKFactor(100));

    // Exact points
    TEST_ASSERT_EQUAL_UINT32(4400, engine.interpolateKFactor(250));
    TEST_ASSERT_EQUAL_UINT32(4460, engine.interpolateKFactor(1850));
    TEST_ASSERT_EQUAL_UINT32(4500, engine.interpolateKFactor(4150));

    // Above last point -> clamps to points[4]
    TEST_ASSERT_EQUAL_UINT32(4500, engine.interpolateKFactor(5000));

    // Interpolation midpoint between point 0 (250, 4400) and point 1 (880, 4440)
    // freq = 565 -> delta_f = 315 / 630 = 0.5 -> k = 4400 + 20 = 4420
    uint32_t k_mid = engine.interpolateKFactor(565);
    TEST_ASSERT_UINT32_WITHIN(2, 4420, k_mid);
}

void test_flow_calibration_flow_rate_and_cutoff_and_over_range() {
    FlowCalibrationEngine engine;
    SensorCalibrationProfile profile{};
    profile.nominal_pulses_per_litre = 4450;
    profile.low_flow_cutoff_lpm_x100 = 20; // 0.20 L/min cutoff
    profile.max_flow_limit_lpm_x100 = 600; // 6.00 L/min max limit
    profile.num_calibration_points = 0;    // Use nominal
    profile.checksum_crc32 = FlowCalibrationEngine::calculateProfileCrc32(profile);
    TEST_ASSERT_TRUE(engine.loadProfile(profile));

    uint16_t flow_lpm_x100 = 0;

    // Zero delta time -> invalid parameters
    FlowEvaluationStatus status = engine.calculateFlowRate(100, 0, flow_lpm_x100);
    TEST_ASSERT_EQUAL(FlowEvaluationStatus::FLOW_INVALID_PARAMETERS, status);
    TEST_ASSERT_EQUAL_UINT16(0, flow_lpm_x100);

    // Zero pulses -> cutoff
    status = engine.calculateFlowRate(0, 1000, flow_lpm_x100);
    TEST_ASSERT_EQUAL(FlowEvaluationStatus::FLOW_ZERO_OR_CUTOFF, status);
    TEST_ASSERT_EQUAL_UINT16(0, flow_lpm_x100);

    // Tiny flow below cutoff: 1 pulse in 2000 ms -> flow = (1 * 6000000) / (2000 * 4450) = 0.67 -> 0.0067 L/min < 0.20 L/min
    status = engine.calculateFlowRate(1, 2000, flow_lpm_x100);
    TEST_ASSERT_EQUAL(FlowEvaluationStatus::FLOW_ZERO_OR_CUTOFF, status);
    TEST_ASSERT_EQUAL_UINT16(0, flow_lpm_x100);

    // Normal flow: 2.50 L/min for 1000 ms -> pulses = 2.50 * 4450 / 60 = 185.4 pulses -> 185 pulses
    // flow = (185 * 6000000) / (1000 * 4450) = 249.4 -> 249 (2.49 L/min)
    status = engine.calculateFlowRate(185, 1000, flow_lpm_x100);
    TEST_ASSERT_EQUAL(FlowEvaluationStatus::FLOW_NORMAL, status);
    TEST_ASSERT_UINT16_WITHIN(2, 249, flow_lpm_x100);

    // Over-range flow: 8.00 L/min -> pulses = 8.00 * 4450 / 60 = 593 pulses in 1000 ms -> flow = ~800 (8.00 L/min) > 600
    status = engine.calculateFlowRate(593, 1000, flow_lpm_x100);
    TEST_ASSERT_EQUAL(FlowEvaluationStatus::FLOW_OVER_RANGE, status);
    TEST_ASSERT_GREATER_THAN_UINT16(600, flow_lpm_x100);
}

void test_flow_calibration_delivered_volume_ml() {
    FlowCalibrationEngine engine;
    SensorCalibrationProfile profile{};
    profile.nominal_pulses_per_litre = 4450;
    profile.low_flow_cutoff_lpm_x100 = 15;
    profile.max_flow_limit_lpm_x100 = 600;
    profile.num_calibration_points = 0;
    profile.checksum_crc32 = FlowCalibrationEngine::calculateProfileCrc32(profile);
    TEST_ASSERT_TRUE(engine.loadProfile(profile));

    // 0 pulses -> 0 mL
    TEST_ASSERT_EQUAL_UINT32(0, engine.calculateDeliveredVolumeMl(0));

    // 4450 pulses -> 1000 mL (1.000 L)
    TEST_ASSERT_EQUAL_UINT32(1000, engine.calculateDeliveredVolumeMl(4450));

    // 8900 pulses -> 2000 mL (2.000 L)
    TEST_ASSERT_EQUAL_UINT32(2000, engine.calculateDeliveredVolumeMl(8900));

    // 2225 pulses -> 500 mL
    TEST_ASSERT_EQUAL_UINT32(500, engine.calculateDeliveredVolumeMl(2225));
}

void test_flow_calibration_statistical_trials_evaluation() {
    // 5 Calibration trials for Point 3 (Target 2000 mL, nominal K = 4450)
    // Recorded pulses: 8910, 8915, 8912, 8908, 8914
    const uint32_t trials_pass[5] = {8910, 8915, 8912, 8908, 8914};
    CalibrationStatistics stats{};

    TEST_ASSERT_TRUE(FlowCalibrationEngine::evaluateCalibrationTrials(trials_pass, 5, 2000, stats));
    TEST_ASSERT_EQUAL_UINT32(8911, stats.mean_pulses); // Mean = 8911.8 -> integer 8911
    TEST_ASSERT_TRUE(stats.is_repeatability_acceptable); // E_rep ~ 0.06% << 1.50%
    TEST_ASSERT_TRUE(stats.is_accuracy_acceptable);      // E_acc ~ 0.1% << 2.00%
    TEST_ASSERT_LESS_THAN_UINT16(50, stats.repeatability_error_pct_x100);

    // Fail case: high variance / inconsistent trials (erratic pulses due to air bubbles)
    const uint32_t trials_fail[5] = {7500, 9200, 6800, 10500, 8100};
    CalibrationStatistics bad_stats{};
    TEST_ASSERT_TRUE(FlowCalibrationEngine::evaluateCalibrationTrials(trials_fail, 5, 2000, bad_stats));
    TEST_ASSERT_FALSE(bad_stats.is_repeatability_acceptable); // E_rep will be > 1.50%

    // Reject trial_count < 3
    CalibrationStatistics invalid_stats{};
    TEST_ASSERT_FALSE(FlowCalibrationEngine::evaluateCalibrationTrials(trials_pass, 2, 2000, invalid_stats));
}

void test_flow_calibration_water_density_temperature_compensation() {
    // Check water density approximation around standard range
    // 4.0 °C -> max density ~ 1000000 g/m3
    uint32_t rho_4c = FlowCalibrationEngine::calculateWaterDensity(40);
    TEST_ASSERT_UINT32_WITHIN(100, 1000000, rho_4c);

    // 20.0 °C -> ~998200 g/m3
    uint32_t rho_20c = FlowCalibrationEngine::calculateWaterDensity(200);
    TEST_ASSERT_UINT32_WITHIN(500, 998200, rho_20c);

    // 25.0 °C -> ~997047 g/m3
    uint32_t rho_25c = FlowCalibrationEngine::calculateWaterDensity(250);
    TEST_ASSERT_UINT32_WITHIN(500, 997047, rho_25c);

    // 35.0 °C -> ~994030 g/m3
    uint32_t rho_35c = FlowCalibrationEngine::calculateWaterDensity(350);
    TEST_ASSERT_UINT32_WITHIN(600, 994030, rho_35c);
}

void test_rf_crc16_ccitt_false_standard_test_vector(void) {
    // Standard test vector: ASCII "123456789" -> 0x29B1
    const uint8_t standard_input[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    const uint16_t crc_standard = RfFrameCodec::calculateCrc16(standard_input, sizeof(standard_input));
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc_standard);

    // Empty / null handling
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, RfFrameCodec::calculateCrc16(nullptr, 0));
    TEST_ASSERT_EQUAL_HEX16(0, RfFrameCodec::calculateCrc16(nullptr, 10));

    // Single byte test
    const uint8_t single_byte[] = {0x00};
    const uint16_t crc_single = RfFrameCodec::calculateCrc16(single_byte, 1);
    TEST_ASSERT_NOT_EQUAL(0, crc_single);
}

void test_rf_frame_codec_header_serialization_boundaries(void) {
    RfHeader header{};
    header.sof[0] = RF_SOF_BYTE_1;
    header.sof[1] = RF_SOF_BYTE_2;
    header.version = RF_PROTOCOL_VERSION;
    header.message_type = static_cast<uint8_t>(RfMessageType::SET_PUMP);
    header.target_node_id = 12;
    header.source_node_id = 0;
    header.boot_session_id = 0xFFFFFFFFU;
    header.sequence = 0xFFFFU;
    header.command_id = 0xFFFFFFFFU;
    header.payload_len = 9;

    uint8_t wire[RF_HEADER_SIZE] = {};
    TEST_ASSERT_TRUE(RfFrameCodec::encodeHeader(header, wire, sizeof(wire)));
    TEST_ASSERT_FALSE(RfFrameCodec::encodeHeader(header, wire, RF_HEADER_SIZE - 1));
    TEST_ASSERT_FALSE(RfFrameCodec::encodeHeader(header, nullptr, sizeof(wire)));

    RfHeader decoded{};
    TEST_ASSERT_TRUE(RfFrameCodec::decodeHeader(wire, sizeof(wire), decoded));
    TEST_ASSERT_FALSE(RfFrameCodec::decodeHeader(wire, RF_HEADER_SIZE - 1, decoded));
    TEST_ASSERT_FALSE(RfFrameCodec::decodeHeader(nullptr, sizeof(wire), decoded));

    TEST_ASSERT_EQUAL_UINT8(RF_SOF_BYTE_1, decoded.sof[0]);
    TEST_ASSERT_EQUAL_UINT8(RF_SOF_BYTE_2, decoded.sof[1]);
    TEST_ASSERT_EQUAL_UINT8(RF_PROTOCOL_VERSION, decoded.version);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RfMessageType::SET_PUMP), decoded.message_type);
    TEST_ASSERT_EQUAL_UINT8(12, decoded.target_node_id);
    TEST_ASSERT_EQUAL_UINT8(0, decoded.source_node_id);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFU, decoded.boot_session_id);
    TEST_ASSERT_EQUAL_UINT16(0xFFFFU, decoded.sequence);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFU, decoded.command_id);
    TEST_ASSERT_EQUAL_UINT8(9, decoded.payload_len);
}

void test_rf_frame_codec_payload_all_schemas_boundaries(void) {
    uint8_t wire[64] = {};
    uint8_t wire_len = 0;

    // 1. PING (4 bytes)
    PingPayload ping{0x12345678U};
    TEST_ASSERT_TRUE(RfFrameCodec::encodePayload(RfMessageType::PING, &ping, sizeof(ping), wire, wire_len));
    TEST_ASSERT_EQUAL_UINT8(4, wire_len);
    PingPayload decoded_ping{};
    TEST_ASSERT_TRUE(RfFrameCodec::decodePayload(RfMessageType::PING, wire, wire_len, &decoded_ping, sizeof(decoded_ping)));
    TEST_ASSERT_EQUAL_UINT32(0x12345678U, decoded_ping.ping_timestamp_ms);

    // 2. PONG (4 bytes)
    PongPayload pong{0x87654321U};
    TEST_ASSERT_TRUE(RfFrameCodec::encodePayload(RfMessageType::PONG, &pong, sizeof(pong), wire, wire_len));
    TEST_ASSERT_EQUAL_UINT8(4, wire_len);
    PongPayload decoded_pong{};
    TEST_ASSERT_TRUE(RfFrameCodec::decodePayload(RfMessageType::PONG, wire, wire_len, &decoded_pong, sizeof(decoded_pong)));
    TEST_ASSERT_EQUAL_UINT32(0x87654321U, decoded_pong.echo_timestamp_ms);

    // 3. SET_PUMP (9 bytes)
    SetPumpPayload set_pump{1, 5000, 30000};
    TEST_ASSERT_TRUE(RfFrameCodec::encodePayload(RfMessageType::SET_PUMP, &set_pump, sizeof(set_pump), wire, wire_len));
    TEST_ASSERT_EQUAL_UINT8(9, wire_len);
    SetPumpPayload decoded_set_pump{};
    TEST_ASSERT_TRUE(RfFrameCodec::decodePayload(RfMessageType::SET_PUMP, wire, wire_len, &decoded_set_pump, sizeof(decoded_set_pump)));
    TEST_ASSERT_EQUAL_UINT8(1, decoded_set_pump.desired_state);
    TEST_ASSERT_EQUAL_UINT32(5000, decoded_set_pump.run_lease_ms);
    TEST_ASSERT_EQUAL_UINT32(30000, decoded_set_pump.max_on_duration_ms);

    // 4. COMMAND_ACK (8 bytes)
    CommandAckPayload ack{100, static_cast<uint8_t>(AckOutcome::SUCCESS), 1, 1, {0, 0, 0}};
    TEST_ASSERT_TRUE(RfFrameCodec::encodePayload(RfMessageType::COMMAND_ACK, &ack, sizeof(ack), wire, wire_len));
    TEST_ASSERT_EQUAL_UINT8(8, wire_len);
    CommandAckPayload decoded_ack{};
    TEST_ASSERT_TRUE(RfFrameCodec::decodePayload(RfMessageType::COMMAND_ACK, wire, wire_len, &decoded_ack, sizeof(decoded_ack)));
    TEST_ASSERT_EQUAL_UINT16(100, decoded_ack.ack_sequence);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AckOutcome::SUCCESS), decoded_ack.ack_outcome);
    TEST_ASSERT_EQUAL_UINT8(1, decoded_ack.reported_pump_state);
    TEST_ASSERT_EQUAL_UINT8(1, decoded_ack.driver_feedback);

    // 5. TELEMETRY (17 bytes)
    TelemetryPayload telemetry{1, 1, 450, 1000, 500, 0, 999};
    TEST_ASSERT_TRUE(RfFrameCodec::encodePayload(RfMessageType::TELEMETRY, &telemetry, sizeof(telemetry), wire, wire_len));
    TEST_ASSERT_EQUAL_UINT8(17, wire_len);
    TelemetryPayload decoded_telemetry{};
    TEST_ASSERT_TRUE(RfFrameCodec::decodePayload(RfMessageType::TELEMETRY, wire, wire_len, &decoded_telemetry, sizeof(decoded_telemetry)));
    TEST_ASSERT_EQUAL_UINT8(1, decoded_telemetry.reported_pump_state);
    TEST_ASSERT_EQUAL_UINT8(1, decoded_telemetry.driver_feedback);
    TEST_ASSERT_EQUAL_UINT16(450, decoded_telemetry.flow_lpm_x100);
    TEST_ASSERT_EQUAL_UINT32(1000, decoded_telemetry.delivered_volume_ml);
    TEST_ASSERT_EQUAL_UINT32(500, decoded_telemetry.pulse_count);
    TEST_ASSERT_EQUAL_UINT8(0, decoded_telemetry.fault_flags);
    TEST_ASSERT_EQUAL_UINT32(999, decoded_telemetry.last_command_id);

    // 6. HEARTBEAT (6 bytes)
    HeartbeatPayload heartbeat{3600, -65, 95};
    TEST_ASSERT_TRUE(RfFrameCodec::encodePayload(RfMessageType::HEARTBEAT, &heartbeat, sizeof(heartbeat), wire, wire_len));
    TEST_ASSERT_EQUAL_UINT8(6, wire_len);
    HeartbeatPayload decoded_heartbeat{};
    TEST_ASSERT_TRUE(RfFrameCodec::decodePayload(RfMessageType::HEARTBEAT, wire, wire_len, &decoded_heartbeat, sizeof(decoded_heartbeat)));
    TEST_ASSERT_EQUAL_UINT32(3600, decoded_heartbeat.uptime_s);
    TEST_ASSERT_EQUAL_INT8(-65, decoded_heartbeat.rssi_dbm);
    TEST_ASSERT_EQUAL_UINT8(95, decoded_heartbeat.battery_percent);

    // 7. FAULT_REPORT (10 bytes)
    FaultReportPayload fault{2, 10000, 0, 888};
    TEST_ASSERT_TRUE(RfFrameCodec::encodePayload(RfMessageType::FAULT_REPORT, &fault, sizeof(fault), wire, wire_len));
    TEST_ASSERT_EQUAL_UINT8(10, wire_len);
    FaultReportPayload decoded_fault{};
    TEST_ASSERT_TRUE(RfFrameCodec::decodePayload(RfMessageType::FAULT_REPORT, wire, wire_len, &decoded_fault, sizeof(decoded_fault)));
    TEST_ASSERT_EQUAL_UINT8(2, decoded_fault.fault_code);
    TEST_ASSERT_EQUAL_UINT32(10000, decoded_fault.timestamp_ms);
    TEST_ASSERT_EQUAL_UINT32(888, decoded_fault.command_id);

    // Boundary / Error Cases
    TEST_ASSERT_FALSE(RfFrameCodec::encodePayload(RfMessageType::SET_PUMP, &set_pump, sizeof(set_pump) - 1, wire, wire_len));
    TEST_ASSERT_FALSE(RfFrameCodec::encodePayload(RfMessageType::SET_PUMP, nullptr, sizeof(set_pump), wire, wire_len));
    TEST_ASSERT_FALSE(RfFrameCodec::decodePayload(RfMessageType::SET_PUMP, wire, 8, &decoded_set_pump, sizeof(decoded_set_pump)));
    TEST_ASSERT_FALSE(RfFrameCodec::decodePayload(RfMessageType::SET_PUMP, wire, 9, nullptr, sizeof(decoded_set_pump)));
    TEST_ASSERT_FALSE(RfFrameCodec::decodePayload(RfMessageType::SET_PUMP, wire, 9, &decoded_set_pump, sizeof(decoded_set_pump) - 1));
}

void test_rf_frame_codec_metadata_and_node_id_boundaries(void) {
    // Node ID validity
    for (uint8_t id = 0; id <= 12; ++id) {
        TEST_ASSERT_TRUE(RfFrameCodec::isValidNodeId(id));
    }
    TEST_ASSERT_FALSE(RfFrameCodec::isValidNodeId(13));
    TEST_ASSERT_FALSE(RfFrameCodec::isValidNodeId(255));

    // Message type validity
    TEST_ASSERT_TRUE(RfFrameCodec::isValidMessageType(RfMessageType::PING));
    TEST_ASSERT_TRUE(RfFrameCodec::isValidMessageType(RfMessageType::FAULT_REPORT));
    TEST_ASSERT_FALSE(RfFrameCodec::isValidMessageType(static_cast<RfMessageType>(0)));
    TEST_ASSERT_FALSE(RfFrameCodec::isValidMessageType(static_cast<RfMessageType>(8)));
    TEST_ASSERT_FALSE(RfFrameCodec::isValidMessageType(static_cast<RfMessageType>(255)));

    const uint8_t psk[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                             0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};
    uint8_t out_frame[RF_MAX_FRAME_SIZE] = {};
    SetPumpPayload payload{1, 1000, 5000};

    // Invalid source node ID (> 12)
    RfFrameMetadata meta_bad_src{13, 0, 1, 1, 100};
    TEST_ASSERT_EQUAL_UINT(0, RfFrameCodec::encodeFrame(meta_bad_src, RfMessageType::SET_PUMP,
                                                         &payload, sizeof(payload), psk, sizeof(psk),
                                                         out_frame, sizeof(out_frame)));

    // Invalid target node ID (> 12)
    RfFrameMetadata meta_bad_tgt{0, 13, 1, 1, 100};
    TEST_ASSERT_EQUAL_UINT(0, RfFrameCodec::encodeFrame(meta_bad_tgt, RfMessageType::SET_PUMP,
                                                         &payload, sizeof(payload), psk, sizeof(psk),
                                                         out_frame, sizeof(out_frame)));

    // Source == Target (0 == 0)
    RfFrameMetadata meta_same_zero{0, 0, 1, 1, 100};
    TEST_ASSERT_EQUAL_UINT(0, RfFrameCodec::encodeFrame(meta_same_zero, RfMessageType::SET_PUMP,
                                                         &payload, sizeof(payload), psk, sizeof(psk),
                                                         out_frame, sizeof(out_frame)));

    // Source == Target (2 == 2)
    RfFrameMetadata meta_same_node{2, 2, 1, 1, 100};
    TEST_ASSERT_EQUAL_UINT(0, RfFrameCodec::encodeFrame(meta_same_node, RfMessageType::SET_PUMP,
                                                         &payload, sizeof(payload), psk, sizeof(psk),
                                                         out_frame, sizeof(out_frame)));
}

void test_rf_frame_codec_fuzz_and_malformed_frames(void) {
    const uint8_t psk[16] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
                             0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};
    uint8_t valid_frame[RF_MAX_FRAME_SIZE] = {};
    SetPumpPayload payload{1, 5000, 30000};
    const RfFrameMetadata meta{0, 2, 100, 200, 300};

    const size_t frame_len = RfFrameCodec::encodeFrame(meta, RfMessageType::SET_PUMP,
                                                       &payload, sizeof(payload),
                                                       psk, sizeof(psk),
                                                       valid_frame, sizeof(valid_frame));
    TEST_ASSERT_TRUE(frame_len > 0);

    // Verify valid frame decodes cleanly
    RfHeader hdr{};
    SetPumpPayload decoded_payload{};
    TEST_ASSERT_TRUE(RfFrameCodec::decodeFrame(valid_frame, frame_len, psk, sizeof(psk),
                                               hdr, &decoded_payload, sizeof(decoded_payload)));

    // 1. Truncated frame tests (lengths 0 to frame_len - 1)
    for (size_t trunc_len = 0; trunc_len < frame_len; ++trunc_len) {
        TEST_ASSERT_FALSE(RfFrameCodec::decodeFrame(valid_frame, trunc_len, psk, sizeof(psk),
                                                    hdr, &decoded_payload, sizeof(decoded_payload)));
    }

    // 2. Corrupted SOF byte 0
    uint8_t corrupted[RF_MAX_FRAME_SIZE];
    std::memcpy(corrupted, valid_frame, frame_len);
    corrupted[0] = 0x00;
    TEST_ASSERT_FALSE(RfFrameCodec::decodeFrame(corrupted, frame_len, psk, sizeof(psk),
                                                hdr, &decoded_payload, sizeof(decoded_payload)));

    // 3. Corrupted SOF byte 1
    std::memcpy(corrupted, valid_frame, frame_len);
    corrupted[1] = 0x00;
    TEST_ASSERT_FALSE(RfFrameCodec::decodeFrame(corrupted, frame_len, psk, sizeof(psk),
                                                hdr, &decoded_payload, sizeof(decoded_payload)));

    // 4. Unsupported version
    std::memcpy(corrupted, valid_frame, frame_len);
    corrupted[2] = 0x02;
    TEST_ASSERT_FALSE(RfFrameCodec::decodeFrame(corrupted, frame_len, psk, sizeof(psk),
                                                hdr, &decoded_payload, sizeof(decoded_payload)));

    // 5. Invalid message type
    std::memcpy(corrupted, valid_frame, frame_len);
    corrupted[3] = 0x00;
    TEST_ASSERT_FALSE(RfFrameCodec::decodeFrame(corrupted, frame_len, psk, sizeof(psk),
                                                hdr, &decoded_payload, sizeof(decoded_payload)));

    // 6. Invalid target (> 12)
    std::memcpy(corrupted, valid_frame, frame_len);
    corrupted[4] = 13;
    TEST_ASSERT_FALSE(RfFrameCodec::decodeFrame(corrupted, frame_len, psk, sizeof(psk),
                                                hdr, &decoded_payload, sizeof(decoded_payload)));

    // 7. Same source and target
    std::memcpy(corrupted, valid_frame, frame_len);
    corrupted[4] = 0;
    corrupted[5] = 0;
    TEST_ASSERT_FALSE(RfFrameCodec::decodeFrame(corrupted, frame_len, psk, sizeof(psk),
                                                hdr, &decoded_payload, sizeof(decoded_payload)));

    // 8. Payload length header field mismatch
    std::memcpy(corrupted, valid_frame, frame_len);
    corrupted[16] = 8; // expected 9 for SET_PUMP
    TEST_ASSERT_FALSE(RfFrameCodec::decodeFrame(corrupted, frame_len, psk, sizeof(psk),
                                                hdr, &decoded_payload, sizeof(decoded_payload)));

    // 9. Single-byte bit flip fuzzing across entire frame
    for (size_t i = 0; i < frame_len; ++i) {
        std::memcpy(corrupted, valid_frame, frame_len);
        corrupted[i] ^= 0x01; // flip 1 bit
        TEST_ASSERT_FALSE(RfFrameCodec::decodeFrame(corrupted, frame_len, psk, sizeof(psk),
                                                    hdr, &decoded_payload, sizeof(decoded_payload)));
    }
}

void test_rf_sequence_wrap_and_distance_modulo_math(void) {
    // Normal monotonic advance
    TEST_ASSERT_EQUAL_UINT16(1, RfFrameCodec::calculateSequenceDistance(101, 100));
    TEST_ASSERT_TRUE(RfFrameCodec::isSequenceAdvanceValid(101, 100));

    TEST_ASSERT_EQUAL_UINT16(50, RfFrameCodec::calculateSequenceDistance(150, 100));
    TEST_ASSERT_TRUE(RfFrameCodec::isSequenceAdvanceValid(150, 100));

    // Wrap-around modulo 65536 advance
    TEST_ASSERT_EQUAL_UINT16(11, RfFrameCodec::calculateSequenceDistance(5, 65530));
    TEST_ASSERT_TRUE(RfFrameCodec::isSequenceAdvanceValid(5, 65530));

    TEST_ASSERT_EQUAL_UINT16(1, RfFrameCodec::calculateSequenceDistance(0, 65535));
    TEST_ASSERT_TRUE(RfFrameCodec::isSequenceAdvanceValid(0, 65535));

    // Duplicate detection (distance = 0)
    TEST_ASSERT_EQUAL_UINT16(0, RfFrameCodec::calculateSequenceDistance(500, 500));
    TEST_ASSERT_FALSE(RfFrameCodec::isSequenceAdvanceValid(500, 500));

    // Replay / Behind current sequence (distance > 32767)
    TEST_ASSERT_EQUAL_UINT16(65535, RfFrameCodec::calculateSequenceDistance(499, 500));
    TEST_ASSERT_FALSE(RfFrameCodec::isSequenceAdvanceValid(499, 500));

    TEST_ASSERT_EQUAL_UINT16(65530, RfFrameCodec::calculateSequenceDistance(10, 16));
    TEST_ASSERT_FALSE(RfFrameCodec::isSequenceAdvanceValid(10, 16));

    // Boundary at exactly RF_SEQUENCE_WRAP_WINDOW (32767)
    TEST_ASSERT_EQUAL_UINT16(32767, RfFrameCodec::calculateSequenceDistance(32767, 0));
    TEST_ASSERT_TRUE(RfFrameCodec::isSequenceAdvanceValid(32767, 0));

    // Exceeding window (> 32767, e.g. 32768)
    TEST_ASSERT_EQUAL_UINT16(32768, RfFrameCodec::calculateSequenceDistance(32768, 0));
    TEST_ASSERT_FALSE(RfFrameCodec::isSequenceAdvanceValid(32768, 0));
}

void test_uart_rf_transport_initialization_and_stats(void) {
    UartRfTransport transport(1, 18, 17, 9600, 256);
    TEST_ASSERT_FALSE(transport.isInitialized());
    TEST_ASSERT_EQUAL_UINT8(1, transport.getUartNum());
    TEST_ASSERT_EQUAL_INT8(18, transport.getRxPin());
    TEST_ASSERT_EQUAL_INT8(17, transport.getTxPin());
    TEST_ASSERT_EQUAL_UINT32(9600, transport.getBaudRate());
    TEST_ASSERT_EQUAL_UINT32(256, transport.getRxCapacity());

    // Uninitialized operations fail safe
    uint8_t dummy[16] = {};
    TEST_ASSERT_EQUAL_UINT32(0, transport.send(dummy, sizeof(dummy)));
    TEST_ASSERT_EQUAL_UINT32(0, transport.receive(dummy, sizeof(dummy)));
    TEST_ASSERT_EQUAL_UINT32(0, transport.available());

    // Begin
    TEST_ASSERT_TRUE(transport.begin());
    TEST_ASSERT_TRUE(transport.isInitialized());

    // Send data
    uint8_t tx_data[] = {0xAA, 0x55, 0x01, 0x02, 0x03};
    TEST_ASSERT_EQUAL_UINT32(5, transport.send(tx_data, sizeof(tx_data)));
    const UartTransportStats& stats = transport.getStats();
    TEST_ASSERT_EQUAL_UINT32(5, stats.tx_bytes);
    TEST_ASSERT_EQUAL_UINT32(1, stats.tx_packets);
    TEST_ASSERT_EQUAL_UINT32(0, stats.tx_errors);

    // Inject and receive RX data
    uint8_t rx_data[] = {0x11, 0x22, 0x33, 0x44};
    transport.injectRxBytes(rx_data, sizeof(rx_data));
    TEST_ASSERT_EQUAL_UINT32(4, transport.available());

    uint8_t rx_buf[8] = {};
    TEST_ASSERT_EQUAL_UINT32(4, transport.receive(rx_buf, sizeof(rx_buf)));
    TEST_ASSERT_EQUAL_UINT8(0x11, rx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x44, rx_buf[3]);
    TEST_ASSERT_EQUAL_UINT32(0, transport.available());

    TEST_ASSERT_EQUAL_UINT32(4, transport.getStats().rx_bytes);
    TEST_ASSERT_EQUAL_UINT32(1, transport.getStats().rx_packets);

    // Reset stats
    transport.resetStats();
    TEST_ASSERT_EQUAL_UINT32(0, transport.getStats().tx_bytes);
    TEST_ASSERT_EQUAL_UINT32(0, transport.getStats().rx_bytes);
}

void test_uart_rf_transport_bounded_rx_overflow_and_drop_counters(void) {
    UartRfTransport transport(1, 18, 17, 9600, 16); // Small 16-byte capacity
    TEST_ASSERT_TRUE(transport.begin());

    uint8_t data[24] = {};
    for (size_t i = 0; i < 24; ++i) data[i] = static_cast<uint8_t>(i + 1);

    // Inject 24 bytes into 16-byte buffer -> 16 bytes accepted, 8 bytes dropped
    transport.injectRxBytes(data, 24);
    TEST_ASSERT_EQUAL_UINT32(16, transport.available());
    TEST_ASSERT_EQUAL_UINT32(8, transport.getStats().dropped_bytes);
    TEST_ASSERT_EQUAL_UINT32(1, transport.getStats().rx_overflows);

    // Inject another 10 bytes -> all 10 dropped
    transport.injectRxBytes(data, 10);
    TEST_ASSERT_EQUAL_UINT32(16, transport.available());
    TEST_ASSERT_EQUAL_UINT32(18, transport.getStats().dropped_bytes);
    TEST_ASSERT_EQUAL_UINT32(2, transport.getStats().rx_overflows);

    // Receive the 16 bytes
    uint8_t rx_buf[32] = {};
    TEST_ASSERT_EQUAL_UINT32(16, transport.receive(rx_buf, sizeof(rx_buf)));
    TEST_ASSERT_EQUAL_UINT32(0, transport.available());
    TEST_ASSERT_EQUAL_UINT8(1, rx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(16, rx_buf[15]);

    // Flush
    transport.flush();
    TEST_ASSERT_EQUAL_UINT32(0, transport.available());
}

void test_uart_rf_transport_tx_error_simulation(void) {
    UartRfTransport transport(1, 18, 17, 9600, 256);
    TEST_ASSERT_TRUE(transport.begin());

    transport.setSimulateTxError(true);
    uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_EQUAL_UINT32(0, transport.send(data, sizeof(data)));
    TEST_ASSERT_EQUAL_UINT32(1, transport.getStats().tx_errors);
    TEST_ASSERT_EQUAL_UINT32(0, transport.getStats().tx_bytes);

    transport.setSimulateTxError(false);
    TEST_ASSERT_EQUAL_UINT32(8, transport.send(data, sizeof(data)));
    TEST_ASSERT_EQUAL_UINT32(1, transport.getStats().tx_errors);
    TEST_ASSERT_EQUAL_UINT32(8, transport.getStats().tx_bytes);
    TEST_ASSERT_EQUAL_UINT32(1, transport.getStats().tx_packets);
}

void test_rf_ping_pong_end_to_end_exchange_and_liveness(void) {
    UartRfTransport transport(1, 18, 17, 9600, 256);
    TEST_ASSERT_TRUE(transport.begin());

    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &transport));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1000));

    // 1. Gateway generates PING frame for Node 1
    PingPayload ping{12345678U};
    uint8_t ping_frame[64] = {};
    const size_t ping_len = manager.buildFrame(RfMessageType::PING, 1, 0,
                                               reinterpret_cast<const uint8_t*>(&ping), sizeof(ping),
                                               ping_frame, sizeof(ping_frame));
    TEST_ASSERT_GREATER_THAN(0, ping_len);
    TEST_ASSERT_EQUAL_UINT32(ping_len, transport.send(ping_frame, ping_len));

    // 2. Node decodes PING frame
    const uint8_t test_psk[16] = {0xA5};
    RfHeader ping_header{};
    PingPayload decoded_ping{};
    TEST_ASSERT_TRUE(RfFrameCodec::decodeFrame(ping_frame, ping_len, test_psk, sizeof(test_psk),
                                               ping_header, &decoded_ping, sizeof(decoded_ping)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RfMessageType::PING), ping_header.message_type);
    TEST_ASSERT_EQUAL_UINT8(1, ping_header.target_node_id);
    TEST_ASSERT_EQUAL_UINT8(0, ping_header.source_node_id);
    TEST_ASSERT_EQUAL_UINT32(12345678U, decoded_ping.ping_timestamp_ms);

    // 3. Node creates PONG response echoing timestamp
    PongPayload pong{decoded_ping.ping_timestamp_ms};
    const RfFrameMetadata pong_meta{1, 0, 1, 1, ping_header.command_id};
    uint8_t pong_frame[64] = {};
    const size_t pong_len = RfFrameCodec::encodeFrame(pong_meta, RfMessageType::PONG,
                                                      reinterpret_cast<const uint8_t*>(&pong), sizeof(pong),
                                                      test_psk, sizeof(test_psk),
                                                      pong_frame, sizeof(pong_frame));
    TEST_ASSERT_GREATER_THAN(0, pong_len);

    // 4. Gateway receives PONG frame at timestamp 25000 ms
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(pong_frame, pong_len, 25000));

    // 5. Verify Node 1 liveness refreshed in registry without modifying pump state
    NodeState state{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL_UINT32(25000, state.last_seen_ms);
    TEST_ASSERT_EQUAL(NodePumpState::OFF, state.reported_state);
    TEST_ASSERT_EQUAL(NodeHealthStatus::ONLINE, state.health);
    TEST_ASSERT_FALSE(state.fault_latched);
}

void test_rf_corrupted_pong_frame_is_rejected(void) {
    UartRfTransport transport(1, 18, 17, 9600, 256);
    TEST_ASSERT_TRUE(transport.begin());

    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &transport));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1000));

    const uint8_t test_psk[16] = {0xA5};
    PongPayload pong{55555U};
    const RfFrameMetadata pong_meta{1, 0, 1, 1, 0};
    uint8_t pong_frame[64] = {};
    const size_t pong_len = RfFrameCodec::encodeFrame(pong_meta, RfMessageType::PONG,
                                                      reinterpret_cast<const uint8_t*>(&pong), sizeof(pong),
                                                      test_psk, sizeof(test_psk),
                                                      pong_frame, sizeof(pong_frame));
    TEST_ASSERT_GREATER_THAN(0, pong_len);

    // Corrupt payload byte
    pong_frame[sizeof(RfHeader)] ^= 0xFF;
    TEST_ASSERT_FALSE(manager.handleIncomingFrame(pong_frame, pong_len, 30000));

    NodeState state{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL_UINT32(1000, state.last_seen_ms); // Not refreshed
}

void test_rf_timing_contracts_heartbeat_telemetry_and_stale_safe_off(void) {
    FakeRfTransport rf;
    TEST_ASSERT_TRUE(rf.begin());
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    TEST_ASSERT_TRUE(provisionTestNodePolicy(manager, 2));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(2, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(2, NodePumpState::OFF, 0, 0, 0, 1000));

    // Enroll node 2 with session 1
    HeartbeatPayload hb_init{1, -70, 95};
    uint8_t frame_init[128] = {};
    const size_t len_init = buildAuthenticatedNodeFrame(manager, RfMessageType::HEARTBEAT, 1, 1, 0,
                                                        &hb_init, sizeof(hb_init), frame_init, sizeof(frame_init));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(frame_init, len_init, 1000));

    // Stale evaluation at 14000ms (13s elapsed < 15s stale threshold) -> NOT stale
    TEST_ASSERT_EQUAL_UINT16(0, registry.evaluateStaleNodes(14000, 15000));

    // Node power off / stops responding. At 17000ms (16s elapsed > 15s) -> STALE
    const uint16_t stale_mask = registry.evaluateStaleNodes(17000, 15000);
    TEST_ASSERT_TRUE((stale_mask & (1 << 1)) != 0); // Node 2 is bit 1
    manager.cancelNodeCommands(2);

    NodeState state{};
    TEST_ASSERT_TRUE(registry.getNodeState(2, state));
    TEST_ASSERT_EQUAL(NodeHealthStatus::STALE, state.health);
    TEST_ASSERT_EQUAL(NodePumpState::OFF, state.desired_state);
    TEST_ASSERT_TRUE(state.fault_latched);

    // Node reboots with higher session ID 2 -> sends heartbeat
    HeartbeatPayload heartbeat{1, -65, 100};
    uint8_t frame_reboot[128] = {};
    const size_t length = buildAuthenticatedNodeFrame(manager, RfMessageType::HEARTBEAT, 2, 1, 0,
                                                       &heartbeat, sizeof(heartbeat), frame_reboot, sizeof(frame_reboot));
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(frame_reboot, length, 18000));
    TEST_ASSERT_TRUE(manager.serviceCommandFanout(18000));

    // Gateway queues explicit safe-off for new session
    TEST_ASSERT_GREATER_OR_EQUAL(sizeof(RfHeader) + sizeof(SetPumpPayload), rf.getTxBuffer().size());
    RfHeader request{};
    std::memcpy(&request, rf.getTxBuffer().data(), sizeof(request));
    SetPumpPayload command{};
    std::memcpy(&command, rf.getTxBuffer().data() + sizeof(request), sizeof(command));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RfMessageType::SET_PUMP), request.message_type);
    TEST_ASSERT_EQUAL_UINT8(0, command.desired_state); // OFF

    TEST_ASSERT_TRUE(registry.getNodeState(2, state));
    TEST_ASSERT_EQUAL(NodePumpState::OFF, state.desired_state);
}

class MockNodeAuditSink : public INodeAuditSink {
public:
    std::string last_event;
    std::string last_reason;
    int call_count = 0;
    void logSafetyEvent(const char* event, const char* reason) override {
        last_event = event ? event : "";
        last_reason = reason ? reason : "";
        call_count++;
    }
};

void test_node_command_processor_boot_safe_output_off(void) {
    FakeRfTransport rf;
    rf.begin();
    SimplePumpActuatorDriver driver;
    // Set output to true before begin to simulate power glitch / unstable pin
    driver.setPumpOutput(true);
    TEST_ASSERT_TRUE(driver.getOutputLevel());

    NodeCommandProcessor processor;
    const uint8_t psk[16] = {0xA5};
    TEST_ASSERT_TRUE(processor.begin(1, &rf, &driver, psk, sizeof(psk), 100));

    // Must be forced LOW immediately upon begin
    TEST_ASSERT_FALSE(driver.getOutputLevel());
    TEST_ASSERT_EQUAL_UINT8(0, processor.getReportedPumpState());
    TEST_ASSERT_EQUAL_UINT8(0, processor.getDriverFeedback());
    TEST_ASSERT_FALSE(processor.isLeaseActive());
    TEST_ASSERT_FALSE(processor.isFaultLatched());
}

void test_node_command_processor_set_pump_on_and_ack(void) {
    FakeRfTransport rf;
    rf.begin();
    SimplePumpActuatorDriver driver;
    NodeCommandProcessor processor;
    const uint8_t psk[16] = {0xA5};
    TEST_ASSERT_TRUE(processor.begin(1, &rf, &driver, psk, sizeof(psk), 100));

    // Construct valid SET_PUMP(ON) frame from Gateway (source=0, target=1)
    SetPumpPayload payload{1, 5000, 10000};
    RfFrameMetadata meta{0, 1, 1, 1, 0x1234};
    uint8_t frame[RF_MAX_FRAME_SIZE] = {};
    size_t frame_len = RfFrameCodec::encodeFrame(meta, RfMessageType::SET_PUMP, &payload, sizeof(payload),
                                                 psk, sizeof(psk), frame, sizeof(frame));
    TEST_ASSERT_GREATER_THAN(0, frame_len);

    uint32_t now = 1000;
    TEST_ASSERT_TRUE(processor.processIncomingFrame(frame, frame_len, now));

    // Assert pump is now ON and lease is active
    TEST_ASSERT_TRUE(driver.getOutputLevel());
    TEST_ASSERT_EQUAL_UINT8(1, processor.getReportedPumpState());
    TEST_ASSERT_EQUAL_UINT8(1, processor.getDriverFeedback());
    TEST_ASSERT_TRUE(processor.isLeaseActive());
    TEST_ASSERT_EQUAL_UINT32(5000, processor.getLeaseRemainingMs(now));
    TEST_ASSERT_EQUAL_UINT32(0x1234, processor.getCurrentCommandId());

    // Verify ACK frame sent to RF transport
    TEST_ASSERT_GREATER_THAN(0, rf.getTxBuffer().size());
    RfHeader ack_header{};
    CommandAckPayload ack_payload{};
    TEST_ASSERT_TRUE(RfFrameCodec::decodeFrame(rf.getTxBuffer().data(), rf.getTxBuffer().size(), psk, sizeof(psk),
                                               ack_header, &ack_payload, sizeof(ack_payload)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RfMessageType::COMMAND_ACK), ack_header.message_type);
    TEST_ASSERT_EQUAL_UINT8(0, ack_header.target_node_id); // To Gateway
    TEST_ASSERT_EQUAL_UINT8(1, ack_header.source_node_id); // From Node 1
    TEST_ASSERT_EQUAL_UINT32(0x1234, ack_header.command_id);
    TEST_ASSERT_EQUAL_UINT16(1, ack_payload.ack_sequence);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AckOutcome::SUCCESS), ack_payload.ack_outcome);
    TEST_ASSERT_EQUAL_UINT8(1, ack_payload.reported_pump_state);
    TEST_ASSERT_EQUAL_UINT8(1, ack_payload.driver_feedback);
}

void test_node_command_processor_lease_deadman_timeout(void) {
    FakeRfTransport rf;
    rf.begin();
    SimplePumpActuatorDriver driver;
    MockNodeAuditSink audit;
    NodeCommandProcessor processor;
    processor.setAuditSink(&audit);
    const uint8_t psk[16] = {0xA5};
    TEST_ASSERT_TRUE(processor.begin(1, &rf, &driver, psk, sizeof(psk), 100));

    // Issue SET_PUMP(ON) with 5000ms lease
    SetPumpPayload payload{1, 5000, 10000};
    RfFrameMetadata meta{0, 1, 1, 1, 0x1234};
    uint8_t frame[RF_MAX_FRAME_SIZE] = {};
    size_t frame_len = RfFrameCodec::encodeFrame(meta, RfMessageType::SET_PUMP, &payload, sizeof(payload),
                                                 psk, sizeof(psk), frame, sizeof(frame));

    uint32_t now = 1000;
    TEST_ASSERT_TRUE(processor.processIncomingFrame(frame, frame_len, now));
    TEST_ASSERT_TRUE(driver.getOutputLevel());
    rf.flush();

    // Advance time by 4999 ms (lease still valid)
    processor.service(now + 4999);
    TEST_ASSERT_TRUE(driver.getOutputLevel());
    TEST_ASSERT_TRUE(processor.isLeaseActive());
    TEST_ASSERT_FALSE(processor.isFaultLatched());
    rf.flush();

    // Advance time to 5000 ms (lease deadline reached) -> Deadman timeout!
    processor.service(now + 5000);
    TEST_ASSERT_FALSE(driver.getOutputLevel()); // Pump forced safe-OFF!
    TEST_ASSERT_EQUAL_UINT8(0, processor.getReportedPumpState());
    TEST_ASSERT_FALSE(processor.isLeaseActive());
    TEST_ASSERT_TRUE(processor.isFaultLatched());
    TEST_ASSERT_EQUAL_UINT8(3, processor.getFaultCode()); // LEASE_EXPIRED
    TEST_ASSERT_EQUAL_STRING("LEASE_EXPIRED_SAFE_OFF", audit.last_event.c_str());

    // Verify FAULT_REPORT frame was sent asynchronously (first 45 bytes)
    const size_t fault_frame_len = RF_HEADER_SIZE + sizeof(FaultReportPayload) + HMAC_TAG_SIZE + 2;
    TEST_ASSERT_GREATER_OR_EQUAL(fault_frame_len, rf.getTxBuffer().size());
    RfHeader header{};
    FaultReportPayload fault_report{};
    TEST_ASSERT_TRUE(RfFrameCodec::decodeFrame(rf.getTxBuffer().data(), fault_frame_len, psk, sizeof(psk),
                                               header, &fault_report, sizeof(fault_report)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RfMessageType::FAULT_REPORT), header.message_type);
    TEST_ASSERT_EQUAL_UINT8(3, fault_report.fault_code);
    TEST_ASSERT_EQUAL_UINT32(0x1234, fault_report.command_id);
}

void test_node_command_processor_idempotency_duplicate_handling(void) {
    FakeRfTransport rf;
    rf.begin();
    SimplePumpActuatorDriver driver;
    NodeCommandProcessor processor;
    const uint8_t psk[16] = {0xA5};
    TEST_ASSERT_TRUE(processor.begin(1, &rf, &driver, psk, sizeof(psk), 100));

    SetPumpPayload payload{1, 5000, 10000};
    RfFrameMetadata meta{0, 1, 1, 42, 0x9999};
    uint8_t frame[RF_MAX_FRAME_SIZE] = {};
    size_t frame_len = RfFrameCodec::encodeFrame(meta, RfMessageType::SET_PUMP, &payload, sizeof(payload),
                                                 psk, sizeof(psk), frame, sizeof(frame));

    uint32_t now = 1000;
    TEST_ASSERT_TRUE(processor.processIncomingFrame(frame, frame_len, now));
    TEST_ASSERT_TRUE(driver.getOutputLevel());
    TEST_ASSERT_EQUAL_UINT32(5000, processor.getLeaseRemainingMs(now));
    rf.flush();

    // Advance 2000 ms into the lease
    now += 2000;
    TEST_ASSERT_EQUAL_UINT32(3000, processor.getLeaseRemainingMs(now));

    // Re-send identical command frame (duplicate retry)
    TEST_ASSERT_TRUE(processor.processIncomingFrame(frame, frame_len, now));

    // Verify cached ACK was returned
    TEST_ASSERT_GREATER_THAN(0, rf.getTxBuffer().size());
    RfHeader ack_header{};
    CommandAckPayload ack_payload{};
    TEST_ASSERT_TRUE(RfFrameCodec::decodeFrame(rf.getTxBuffer().data(), rf.getTxBuffer().size(), psk, sizeof(psk),
                                               ack_header, &ack_payload, sizeof(ack_payload)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AckOutcome::SUCCESS), ack_payload.ack_outcome);
    TEST_ASSERT_EQUAL_UINT16(42, ack_payload.ack_sequence);

    // Critical: Lease was NOT extended/reset! Remaining lease is still 3000 ms, not 5000 ms.
    TEST_ASSERT_EQUAL_UINT32(3000, processor.getLeaseRemainingMs(now));
    TEST_ASSERT_TRUE(driver.getOutputLevel());
}

void test_node_command_processor_invalid_lease_rejection(void) {
    FakeRfTransport rf;
    rf.begin();
    SimplePumpActuatorDriver driver;
    NodeCommandProcessor processor;
    const uint8_t psk[16] = {0xA5};
    TEST_ASSERT_TRUE(processor.begin(1, &rf, &driver, psk, sizeof(psk), 100));

    // Zero run_lease_ms
    SetPumpPayload bad_payload{1, 0, 10000};
    RfFrameMetadata meta{0, 1, 1, 1, 0x1111};
    uint8_t frame[RF_MAX_FRAME_SIZE] = {};
    size_t frame_len = RfFrameCodec::encodeFrame(meta, RfMessageType::SET_PUMP, &bad_payload, sizeof(bad_payload),
                                                 psk, sizeof(psk), frame, sizeof(frame));

    TEST_ASSERT_TRUE(processor.processIncomingFrame(frame, frame_len, 1000));
    TEST_ASSERT_FALSE(driver.getOutputLevel());
    TEST_ASSERT_FALSE(processor.isLeaseActive());

    TEST_ASSERT_GREATER_THAN(0, rf.getTxBuffer().size());
    RfHeader ack_header{};
    CommandAckPayload ack_payload{};
    TEST_ASSERT_TRUE(RfFrameCodec::decodeFrame(rf.getTxBuffer().data(), rf.getTxBuffer().size(),
                                               psk, sizeof(psk), ack_header, &ack_payload, sizeof(ack_payload)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AckOutcome::REJECTED_INVALID_LEASE), ack_payload.ack_outcome);
}

void test_node_command_processor_fault_lockout(void) {
    FakeRfTransport rf;
    rf.begin();
    SimplePumpActuatorDriver driver;
    NodeCommandProcessor processor;
    const uint8_t psk[16] = {0xA5};
    TEST_ASSERT_TRUE(processor.begin(1, &rf, &driver, psk, sizeof(psk), 100));

    // Manually latch fault
    processor.latchFault(1, 1000, "NO_FLOW");
    TEST_ASSERT_TRUE(processor.isFaultLatched());
    rf.flush();

    // SET_PUMP(ON) should be locked out
    SetPumpPayload on_payload{1, 5000, 10000};
    RfFrameMetadata meta_on{0, 1, 1, 1, 0x2222};
    uint8_t on_frame[RF_MAX_FRAME_SIZE] = {};
    size_t on_len = RfFrameCodec::encodeFrame(meta_on, RfMessageType::SET_PUMP, &on_payload, sizeof(on_payload),
                                              psk, sizeof(psk), on_frame, sizeof(on_frame));

    TEST_ASSERT_TRUE(processor.processIncomingFrame(on_frame, on_len, 1000));
    TEST_ASSERT_FALSE(driver.getOutputLevel());

    RfHeader ack_header{};
    CommandAckPayload ack_payload{};
    TEST_ASSERT_TRUE(RfFrameCodec::decodeFrame(rf.getTxBuffer().data(), rf.getTxBuffer().size(),
                                               psk, sizeof(psk), ack_header, &ack_payload, sizeof(ack_payload)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AckOutcome::FAULT_LOCKOUT), ack_payload.ack_outcome);
    rf.flush();

    // SET_PUMP(OFF) should always be accepted
    SetPumpPayload off_payload{0, 0, 0};
    RfFrameMetadata meta_off{0, 1, 1, 2, 0x2223};
    uint8_t off_frame[RF_MAX_FRAME_SIZE] = {};
    size_t off_len = RfFrameCodec::encodeFrame(meta_off, RfMessageType::SET_PUMP, &off_payload, sizeof(off_payload),
                                               psk, sizeof(psk), off_frame, sizeof(off_frame));

    TEST_ASSERT_TRUE(processor.processIncomingFrame(off_frame, off_len, 1000));
    TEST_ASSERT_TRUE(RfFrameCodec::decodeFrame(rf.getTxBuffer().data(), rf.getTxBuffer().size(),
                                               psk, sizeof(psk), ack_header, &ack_payload, sizeof(ack_payload)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AckOutcome::SUCCESS), ack_payload.ack_outcome);
    rf.flush();

    // After resetFault, SET_PUMP(ON) is accepted
    TEST_ASSERT_TRUE(processor.resetFault());
    TEST_ASSERT_FALSE(processor.isFaultLatched());

    RfFrameMetadata meta_on2{0, 1, 1, 3, 0x2224};
    on_len = RfFrameCodec::encodeFrame(meta_on2, RfMessageType::SET_PUMP, &on_payload, sizeof(on_payload),
                                       psk, sizeof(psk), on_frame, sizeof(on_frame));
    TEST_ASSERT_TRUE(processor.processIncomingFrame(on_frame, on_len, 1000));
    TEST_ASSERT_TRUE(driver.getOutputLevel());
    TEST_ASSERT_TRUE(RfFrameCodec::decodeFrame(rf.getTxBuffer().data(), rf.getTxBuffer().size(),
                                               psk, sizeof(psk), ack_header, &ack_payload, sizeof(ack_payload)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AckOutcome::SUCCESS), ack_payload.ack_outcome);
}

void test_node_command_processor_anti_replay_and_auth_rejection(void) {
    FakeRfTransport rf;
    rf.begin();
    SimplePumpActuatorDriver driver;
    NodeCommandProcessor processor;
    const uint8_t psk[16] = {0xA5};
    const uint8_t bad_psk[16] = {0x5A};
    TEST_ASSERT_TRUE(processor.begin(1, &rf, &driver, psk, sizeof(psk), 100));

    SetPumpPayload payload{1, 5000, 10000};
    RfFrameMetadata meta{0, 1, 5, 10, 0x3333};
    uint8_t frame[RF_MAX_FRAME_SIZE] = {};

    // 1. Bad HMAC
    size_t len = RfFrameCodec::encodeFrame(meta, RfMessageType::SET_PUMP, &payload, sizeof(payload),
                                           bad_psk, sizeof(bad_psk), frame, sizeof(frame));
    TEST_ASSERT_FALSE(processor.processIncomingFrame(frame, len, 1000));
    TEST_ASSERT_FALSE(driver.getOutputLevel());

    // 2. Valid frame with session 5, seq 10
    len = RfFrameCodec::encodeFrame(meta, RfMessageType::SET_PUMP, &payload, sizeof(payload),
                                    psk, sizeof(psk), frame, sizeof(frame));
    TEST_ASSERT_TRUE(processor.processIncomingFrame(frame, len, 1000));
    TEST_ASSERT_TRUE(driver.getOutputLevel());

    // 3. Replay with older session 4
    RfFrameMetadata old_meta{0, 1, 4, 11, 0x3334};
    len = RfFrameCodec::encodeFrame(old_meta, RfMessageType::SET_PUMP, &payload, sizeof(payload),
                                    psk, sizeof(psk), frame, sizeof(frame));
    TEST_ASSERT_FALSE(processor.processIncomingFrame(frame, len, 1000));

    // 4. Stale sequence number (seq 8 < seq 10 in same session 5)
    RfFrameMetadata stale_seq_meta{0, 1, 5, 8, 0x3335};
    len = RfFrameCodec::encodeFrame(stale_seq_meta, RfMessageType::SET_PUMP, &payload, sizeof(payload),
                                    psk, sizeof(psk), frame, sizeof(frame));
    TEST_ASSERT_FALSE(processor.processIncomingFrame(frame, len, 1000));

    // 5. Targeting a different node (target = 2)
    RfFrameMetadata other_node_meta{0, 2, 5, 12, 0x3336};
    len = RfFrameCodec::encodeFrame(other_node_meta, RfMessageType::SET_PUMP, &payload, sizeof(payload),
                                    psk, sizeof(psk), frame, sizeof(frame));
    TEST_ASSERT_FALSE(processor.processIncomingFrame(frame, len, 1000));
}

void test_gateway_and_node_command_processor_closed_loop(void) {
    FakeRfTransport gw_rf;
    FakeRfTransport node_rf;
    gw_rf.begin();
    node_rf.begin();

    NodeRegistry registry;
    CommandManager gw_cmd_mgr;
    TEST_ASSERT_TRUE(registry.begin());
    TEST_ASSERT_TRUE(gw_cmd_mgr.begin(&registry, &gw_rf));
    TEST_ASSERT_TRUE(provisionTestPsk(gw_cmd_mgr));
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));
    TEST_ASSERT_TRUE(registry.updateTelemetry(1, NodePumpState::OFF, 0, 0, 0, 1));
    TEST_ASSERT_TRUE(provisionTestNodePolicy(gw_cmd_mgr, 1));

    SimplePumpActuatorDriver driver;
    NodeCommandProcessor node_processor;
    const uint8_t psk[16] = {0xA5};
    TEST_ASSERT_TRUE(node_processor.begin(1, &node_rf, &driver, psk, sizeof(psk), 200));

    // Gateway queues ON command for Node 1
    TEST_ASSERT_TRUE(gw_cmd_mgr.queueExternalNodeCommand(1, NodePumpState::ON, "closed-loop-1"));
    uint32_t now = 1000;
    TEST_ASSERT_TRUE(gw_cmd_mgr.serviceCommandFanout(now));
    TEST_ASSERT_TRUE(gw_cmd_mgr.isPending(1));
    TEST_ASSERT_GREATER_THAN(0, gw_rf.getTxBuffer().size());

    // Node receives Gateway's SET_PUMP frame
    const auto& gw_out = gw_rf.getTxBuffer();
    TEST_ASSERT_TRUE(node_processor.processIncomingFrame(gw_out.data(), gw_out.size(), now));
    TEST_ASSERT_TRUE(driver.getOutputLevel());
    TEST_ASSERT_EQUAL_UINT8(1, node_processor.getReportedPumpState());
    TEST_ASSERT_GREATER_THAN(0, node_rf.getTxBuffer().size());

    // Gateway receives Node's COMMAND_ACK frame
    const auto& node_ack = node_rf.getTxBuffer();
    TEST_ASSERT_TRUE(gw_cmd_mgr.handleIncomingFrame(node_ack.data(), node_ack.size(), now));

    // Gateway should be in AWAITING_PUMP_FEEDBACK phase
    TEST_ASSERT_TRUE(gw_cmd_mgr.isPending(1));
    node_rf.flush();

    // Node sends TELEMETRY with flow confirmation
    driver.setFlowLpmX100(250); // 2.50 L/min (well above min_flow 0.50 L/min)
    driver.setDeliveredVolumeMl(50);
    driver.setPulseCount(200);
    TEST_ASSERT_TRUE(node_processor.sendTelemetry(now));
    TEST_ASSERT_GREATER_THAN(0, node_rf.getTxBuffer().size());

    // Gateway receives TELEMETRY -> FLOW_CONFIRMED -> completes command!
    const auto& node_telem = node_rf.getTxBuffer();
    TEST_ASSERT_TRUE(gw_cmd_mgr.handleIncomingFrame(node_telem.data(), node_telem.size(), now));
    TEST_ASSERT_FALSE(gw_cmd_mgr.isPending(1)); // Completed successfully!

    NodeState final_state{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, final_state));
    TEST_ASSERT_EQUAL(NodePumpState::ON, final_state.reported_state);
    TEST_ASSERT_EQUAL_UINT8(1, final_state.driver_feedback);
    TEST_ASSERT_EQUAL_UINT16(250, final_state.flow_lpm_x100);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_fake_clock_night_mode);
    RUN_TEST(test_nvs_storage_basic_init_and_reset);
    RUN_TEST(test_mqtt_client_connect_and_lwt);
    RUN_TEST(test_mqtt_client_publish_heartbeat);
    RUN_TEST(test_mqtt_begin_invalid_config_resets_previous_connection);
    RUN_TEST(test_mqtt_command_validation_rejects_untrusted_input);
    RUN_TEST(test_mqtt_callback_enforces_payload_length_contract);
    RUN_TEST(test_mqtt_topic_full_match_and_missing_command_id_nack);
    RUN_TEST(test_mqtt_config_provider_load);
    RUN_TEST(test_mqtt_config_rejects_unsafe_device_id);
    RUN_TEST(test_mqtt_connect_is_atomic_on_publish_or_subscribe_failure);
    RUN_TEST(test_mqtt_subscribes_flow_policy_and_rolls_back_on_subscription_failure);
    RUN_TEST(test_mqtt_task_create_failure_rolls_back_facade_state);
    RUN_TEST(test_mqtt_reconnect_backoff_logic);
    RUN_TEST(test_mqtt_heartbeat_publish_result_controls_deadline);
    RUN_TEST(test_rf_transport_interface_and_fake);
    RUN_TEST(test_mqtt_client_gateway_init_without_relays);
    RUN_TEST(test_node_registry_assignment_and_fanout);
    RUN_TEST(test_group_schedule_manager_ticks_and_fanout);
    RUN_TEST(test_group_schedule_manager_invalid_rtc_forces_safe_off);
    RUN_TEST(test_group_schedule_manager_services_watchdog_and_unassigned_safe_off);
    RUN_TEST(test_command_manager_hmac_and_crc_and_frame_codec);
    RUN_TEST(test_rf_protocol_little_endian_byte_vectors);
    RUN_TEST(test_rf_frame_codec_interoperates_for_gateway_and_node_messages);
    RUN_TEST(test_rf_frame_codec_rejects_empty_payload_for_all_current_schemas);
    RUN_TEST(test_hmac_rejects_invalid_pointers_and_accepts_zero_length_inputs);
    RUN_TEST(test_rf_provisioning_commit_failure_keeps_manager_fail_closed);
    RUN_TEST(test_rf_missing_key_keeps_rx_and_tx_locked_without_fallback);
    RUN_TEST(test_rf_provisioning_uses_canonical_namespace_and_keys);
    RUN_TEST(test_rf_boot_session_uses_uint32_range_and_fails_closed_at_exhaustion);
    RUN_TEST(test_command_manager_pending_retry_and_timeout_fault);
    RUN_TEST(test_on_is_rejected_until_authenticated_node_flow_policy_is_provisioned);
    RUN_TEST(test_invalid_or_other_node_flow_policy_cannot_authorize_on);
    RUN_TEST(test_invalid_control_policy_update_preserves_existing_policy_atomically);
    RUN_TEST(test_out_of_range_control_or_flow_policy_preserves_existing_policy);
    RUN_TEST(test_invalid_mqtt_control_policy_update_preserves_existing_policy);
    RUN_TEST(test_out_of_range_mqtt_control_policy_is_rejected_without_mutation);
    RUN_TEST(test_flow_confirmation_uses_only_the_provisioned_node_threshold);
    RUN_TEST(test_rf_retry_reuses_immutable_frame_and_correlated_telemetry_completes);
    RUN_TEST(test_ack_without_correlated_telemetry_never_renews_on_lease_and_times_out_safe_off);
    RUN_TEST(test_on_zero_flow_waits_then_latches_no_flow_and_queues_one_safe_off);
    RUN_TEST(test_on_delayed_valid_flow_before_deadline_completes);
    RUN_TEST(test_over_range_or_fault_telemetry_latches_safe_off);
    RUN_TEST(test_physical_over_range_telemetry_never_completes_and_queues_safe_off);
    RUN_TEST(test_off_residual_flow_latches_unexpected_flow_fault);
    RUN_TEST(test_rejects_telemetry_from_previous_command_after_retry_and_new_command);
    RUN_TEST(test_applies_telemetry_only_for_current_command_and_session);
    RUN_TEST(test_rejects_old_telemetry_and_fault_after_node_reboot_session_change);
    RUN_TEST(test_rejects_malformed_fault_payload_without_mutating_registry);
    RUN_TEST(test_mqtt_rf_command_correlation_and_ack_outcome);
    RUN_TEST(test_rf_multi_frame_bounded_rx);
    RUN_TEST(test_stale_node_safe_off_and_reconnect_recovery);
    RUN_TEST(test_command_manager_queueing_and_idempotency);
    RUN_TEST(test_mqtt_callback_defers_command_manager_mutation_to_main_loop);
    RUN_TEST(test_main_loop_serializes_interleaved_mqtt_policy_command_ack_and_telemetry);
    RUN_TEST(test_mqtt_ack_admission_survives_full_telemetry_lane_and_publish_retry);
    RUN_TEST(test_mqtt_valid_command_ack_survives_full_telemetry_lane);
    RUN_TEST(test_mqtt_outbound_drain_is_bounded_and_telemetry_fifo);
    RUN_TEST(test_mqtt_ack_reservation_backpressures_before_any_command_mutation);
    RUN_TEST(test_group_command_prepare_failure_leaves_all_nodes_unchanged);
    RUN_TEST(test_group_command_commits_all_prepared_nodes);
    RUN_TEST(test_mqtt_gateway_domain_publishing_and_assignment_command);
    RUN_TEST(test_offline_stale_and_fault_nodes_reject_on_but_allow_safe_off);
    RUN_TEST(test_command_id_is_a_safe_mqtt_topic_segment);
    RUN_TEST(test_reassignment_commits_only_after_rf_safe_off_ack);
    RUN_TEST(test_reassignment_rf_safe_off_timeout_keeps_old_mapping_and_latches_fault);
    RUN_TEST(test_reassignment_cancel_or_nack_never_commits_mapping);
    RUN_TEST(test_published_treatment_is_required_before_scheduler_activation);
    RUN_TEST(test_invalid_flow_or_fault_telemetry_latches_safe_off);
    RUN_TEST(test_authenticated_heartbeat_refreshes_liveness_without_pump_inference);
    RUN_TEST(test_authenticated_ac_heartbeat_refreshes_liveness_and_reserved_battery_value_is_rejected);
    RUN_TEST(test_node_reboot_session_queues_explicit_safe_off);
    RUN_TEST(test_regression_no_legacy_relay_symbols_in_production_config);

    // Pump Feedback Evaluator multi-tier tests
    RUN_TEST(test_pump_feedback_normal_cycle_with_multi_tier_confirmation);
    RUN_TEST(test_pump_feedback_driver_mismatch_on_and_off);
    RUN_TEST(test_pump_feedback_inrush_blanking_and_sustained_overcurrent_stall);
    RUN_TEST(test_pump_feedback_open_load_broken_wire);
    RUN_TEST(test_pump_feedback_stuck_on_relay_or_shorted_fet);
    RUN_TEST(test_pump_feedback_dry_run_differentiation_vs_clogged_nozzle);
    RUN_TEST(test_pump_feedback_over_range_flow_pipe_burst);
    RUN_TEST(test_pump_feedback_fault_latching_and_explicit_reset);

    // Flow Calibration Engine & Measurement Traceability tests
    RUN_TEST(test_flow_calibration_profile_validation_and_crc);
    RUN_TEST(test_flow_calibration_piecewise_k_factor_interpolation);
    RUN_TEST(test_flow_calibration_flow_rate_and_cutoff_and_over_range);
    RUN_TEST(test_flow_calibration_delivered_volume_ml);
    RUN_TEST(test_flow_calibration_statistical_trials_evaluation);
    RUN_TEST(test_flow_calibration_water_density_temperature_compensation);

    // RF Wire Protocol & Frame Codec verification tests (Task B1)
    RUN_TEST(test_rf_crc16_ccitt_false_standard_test_vector);
    RUN_TEST(test_rf_frame_codec_header_serialization_boundaries);
    RUN_TEST(test_rf_frame_codec_payload_all_schemas_boundaries);
    RUN_TEST(test_rf_frame_codec_metadata_and_node_id_boundaries);
    RUN_TEST(test_rf_frame_codec_fuzz_and_malformed_frames);
    RUN_TEST(test_rf_sequence_wrap_and_distance_modulo_math);

    // RF UART Transport & Ping-Pong / Stale Timing tests (Task B2)
    RUN_TEST(test_uart_rf_transport_initialization_and_stats);
    RUN_TEST(test_uart_rf_transport_bounded_rx_overflow_and_drop_counters);
    RUN_TEST(test_uart_rf_transport_tx_error_simulation);
    RUN_TEST(test_rf_ping_pong_end_to_end_exchange_and_liveness);
    RUN_TEST(test_rf_corrupted_pong_frame_is_rejected);
    RUN_TEST(test_rf_timing_contracts_heartbeat_telemetry_and_stale_safe_off);

    // Node-Side Command Processor, Lease Deadman & Idempotency tests (Task B3)
    RUN_TEST(test_node_command_processor_boot_safe_output_off);
    RUN_TEST(test_node_command_processor_set_pump_on_and_ack);
    RUN_TEST(test_node_command_processor_lease_deadman_timeout);
    RUN_TEST(test_node_command_processor_idempotency_duplicate_handling);
    RUN_TEST(test_node_command_processor_invalid_lease_rejection);
    RUN_TEST(test_node_command_processor_fault_lockout);
    RUN_TEST(test_node_command_processor_anti_replay_and_auth_rejection);
    RUN_TEST(test_gateway_and_node_command_processor_closed_loop);

    return UNITY_END();
}

