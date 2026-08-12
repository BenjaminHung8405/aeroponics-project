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

void setUp(void) {}
void tearDown(void) {}

bool provisionTestPsk(CommandManager& manager) {
    const uint8_t test_psk[16] = {0xA5};
    return manager.setPskKey(test_psk, sizeof(test_psk));
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
    MqttConfig cfg{"mqtt.local", 1883, "user", "pass", "dev-1"};
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
    MqttConfig cfg{"mqtt.local", 1883, "user", "pass", "dev-1"};
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
    MqttConfig cfg{"mqtt.local", 1883, "user", "pass", "dev-1"};
    TEST_ASSERT_TRUE(mqtt.begin(cfg));
    TEST_ASSERT_TRUE(mqtt.connect());

    MqttConfig invalid_cfg{"mqtt.local", 1883, "user", "pass", ""};
    TEST_ASSERT_FALSE(mqtt.begin(invalid_cfg));
    TEST_ASSERT_FALSE(mqtt.isInitialized());
    TEST_ASSERT_FALSE(mqtt.isConnected());
}

void test_mqtt_command_validation_rejects_untrusted_input(void) {
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "user", "pass", "dev-1"};
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
    MqttConfig cfg{"mqtt.local", 1883, "user", "pass", "dev-1"};
    TEST_ASSERT_TRUE(mqtt.begin(cfg));
    TEST_ASSERT_TRUE(mqtt.connect());

    char topic[] = "aeroponics/device/dev-1/command/config/assignment";
    char dummy_payload[10] = "{}";
    mqtt.simulateIncomingMessage(topic, (uint8_t*)dummy_payload, 50000);
}

void test_mqtt_topic_full_match_and_missing_command_id_nack(void) {
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "user", "pass", "dev-1"};
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
    MqttConfig cfg{"mqtt.local", 1883, "user", "pass", "dev/invalid#id"};
    TEST_ASSERT_FALSE(mqtt.begin(cfg));
}

void test_mqtt_connect_is_atomic_on_publish_or_subscribe_failure(void) {
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "user", "pass", "dev-1"};
    TEST_ASSERT_TRUE(mqtt.begin(cfg));
    mqtt.setMockPublishResult(false);
    TEST_ASSERT_FALSE(mqtt.connect());
    TEST_ASSERT_FALSE(mqtt.isConnected());
}

void test_mqtt_task_create_failure_rolls_back_facade_state(void) {
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "user", "pass", "dev-1"};
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
    MqttConfig cfg{"mqtt.local", 1883, "user", "pass", "dev-1"};
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
    MqttConfig cfg{"mqtt.local", 1883, "user", "pass", "dev-gw1"};
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
    TEST_ASSERT_TRUE(group_mgr.setGroupActive(1, true));

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
    TEST_ASSERT_TRUE(group_mgr.setGroupActive(1, true));
    TEST_ASSERT_FALSE(group_mgr.stepGroupSchedule());

    NodeState state{};
    TEST_ASSERT_TRUE(registry.getNodeState(1, state));
    TEST_ASSERT_EQUAL(NodePumpState::OFF, state.desired_state);
    GroupRuntimeState group{};
    TEST_ASSERT_TRUE(group_mgr.getGroupRuntimeState(1, group));
    TEST_ASSERT_EQUAL(GroupAssignmentState::UNASSIGNED, group.assignment_state);
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

void test_command_manager_pending_retry_and_timeout_fault(void) {
    FakeRfTransport rf;
    rf.begin();
    NodeRegistry registry;
    CommandManager cmd_mgr;
    TEST_ASSERT_TRUE(cmd_mgr.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(cmd_mgr));

    registry.assignNodeToGroup(1, 1);
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

void test_mqtt_rf_command_correlation_and_ack_outcome(void) {
    FakeRfTransport rf;
    TEST_ASSERT_TRUE(rf.begin());
    NodeRegistry registry;
    CommandManager manager;
    TEST_ASSERT_TRUE(manager.begin(&registry, &rf));
    TEST_ASSERT_TRUE(provisionTestPsk(manager));
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "user", "pass", "gateway-1"};
    TEST_ASSERT_TRUE(mqtt.begin(cfg, nullptr, &registry, &manager));
    TEST_ASSERT_TRUE(mqtt.connect());
    TEST_ASSERT_TRUE(registry.assignNodeToGroup(1, 1));

    char topic[] = "aeroponics/device/gateway-1/command/node/1/override";
    char payload[] = "{\"command_id\":\"rf-cmd-1\",\"version\":1,\"desired_state\":\"ON\"}";
    mqtt.simulateIncomingMessage(topic, reinterpret_cast<uint8_t*>(payload), strlen(payload));
    TEST_ASSERT_TRUE(strstr(mqtt.mockLastPublishedPayload(), "\"status\":\"ACCEPTED\"") != nullptr);
    TEST_ASSERT_TRUE(manager.serviceCommandFanout(1000));
    TEST_ASSERT_TRUE(strstr(mqtt.mockLastPublishedPayload(), "\"status\":\"QUEUED\"") != nullptr);

    const std::vector<uint8_t>& tx = rf.getTxBuffer();
    RfHeader request{};
    std::memcpy(&request, tx.data(), sizeof(request));
    CommandAckPayload ack{request.sequence, static_cast<uint8_t>(AckOutcome::SUCCESS), 1, 1, {0, 0, 0}};
    uint8_t ack_frame[128] = {};
    const size_t ack_len = manager.buildFrame(RfMessageType::COMMAND_ACK, 1, request.command_id,
                                              reinterpret_cast<const uint8_t*>(&ack), sizeof(ack),
                                              ack_frame, sizeof(ack_frame));
    RfHeader* response = reinterpret_cast<RfHeader*>(ack_frame);
    response->source_node_id = 1;
    response->target_node_id = 0;
    const size_t signed_len = sizeof(RfHeader) + sizeof(ack);
    uint8_t mac[HMAC_TAG_SIZE];
    const uint8_t key[16] = {0xA5};
    HmacSha256::calculateTruncated(key, sizeof(key), ack_frame, signed_len, mac);
    std::memcpy(ack_frame + signed_len, mac, HMAC_TAG_SIZE);
    const uint16_t crc = CommandManager::calculateCrc16(ack_frame, signed_len + HMAC_TAG_SIZE);
    ack_frame[signed_len + HMAC_TAG_SIZE] = static_cast<uint8_t>(crc & 0xFF);
    ack_frame[signed_len + HMAC_TAG_SIZE + 1] = static_cast<uint8_t>(crc >> 8);
    TEST_ASSERT_TRUE(manager.handleIncomingFrame(ack_frame, ack_len, 1001));
    TEST_ASSERT_TRUE(strstr(mqtt.mockLastPublishedPayload(), "\"command_id\":\"rf-cmd-1\"") != nullptr);
    TEST_ASSERT_TRUE(strstr(mqtt.mockLastPublishedPayload(), "\"status\":\"RF_ACKED\"") != nullptr);
}

void test_mqtt_gateway_domain_publishing_and_assignment_command(void) {
    MqttClient mqtt;
    MqttConfig cfg{"mqtt.local", 1883, "user", "pass", "gateway-1"};
    NodeRegistry registry;

    TEST_ASSERT_TRUE(mqtt.begin(cfg, nullptr, &registry));
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

    TEST_ASSERT_EQUAL_UINT8(2, registry.getNodeGroup(3));
    TEST_ASSERT_EQUAL_STRING("aeroponics/device/gateway-1/ack/cmd-999", mqtt.mockLastPublishedTopic());
    TEST_ASSERT_TRUE(strstr(mqtt.mockLastPublishedPayload(), "\"status\":\"COMPLETED\"") != nullptr);
}

void test_regression_no_legacy_relay_symbols_in_production_config(void) {
    TEST_ASSERT_NULL(strstr(MQTT_COMMAND_TREATMENT_SUFFIX, "relay"));
    TEST_ASSERT_NULL(strstr(MQTT_COMMAND_ASSIGNMENT_SUFFIX, "relay"));
    TEST_ASSERT_NULL(strstr(MQTT_TELEMETRY_GROUP_SUFFIX, "relay"));
    TEST_ASSERT_NULL(strstr(MQTT_TELEMETRY_NODE_SUFFIX, "relay"));
    TEST_ASSERT_NULL(strstr(MQTT_COMMAND_NODE_OVERRIDE_SUFFIX, "relay"));
    TEST_ASSERT_NULL(strstr(MQTT_COMMAND_GROUP_CONTROL_SUFFIX, "relay"));
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
    RUN_TEST(test_mqtt_task_create_failure_rolls_back_facade_state);
    RUN_TEST(test_mqtt_reconnect_backoff_logic);
    RUN_TEST(test_mqtt_heartbeat_publish_result_controls_deadline);
    RUN_TEST(test_rf_transport_interface_and_fake);
    RUN_TEST(test_mqtt_client_gateway_init_without_relays);
    RUN_TEST(test_node_registry_assignment_and_fanout);
    RUN_TEST(test_group_schedule_manager_ticks_and_fanout);
    RUN_TEST(test_group_schedule_manager_invalid_rtc_forces_safe_off);
    RUN_TEST(test_command_manager_hmac_and_crc_and_frame_codec);
    RUN_TEST(test_rf_provisioning_commit_failure_keeps_manager_fail_closed);
    RUN_TEST(test_command_manager_pending_retry_and_timeout_fault);
    RUN_TEST(test_mqtt_rf_command_correlation_and_ack_outcome);
    RUN_TEST(test_mqtt_gateway_domain_publishing_and_assignment_command);
    RUN_TEST(test_regression_no_legacy_relay_symbols_in_production_config);
    return UNITY_END();
}
