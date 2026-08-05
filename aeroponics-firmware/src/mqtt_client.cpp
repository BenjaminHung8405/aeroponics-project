#include "mqtt_client.h"
#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <esp_log.h>
#include <WiFi.h>
#else
#include <iostream>
#ifndef ESP_LOGI
#define ESP_LOGI(tag, fmt, ...) printf("[INFO][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif
#ifndef ESP_LOGW
#define ESP_LOGW(tag, fmt, ...) printf("[WARN][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif
#ifndef ESP_LOGE
#define ESP_LOGE(tag, fmt, ...) printf("[ERROR][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif
#endif

#if !defined(ESP_PLATFORM) && !defined(ARDUINO)
#include <chrono>
#endif

static const char* TAG = "MQTT";

static uint32_t getSystemMillis() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    return millis();
#else
    using namespace std::chrono;
    static auto start_time = steady_clock::now();
    auto now = steady_clock::now();
    return static_cast<uint32_t>(duration_cast<milliseconds>(now - start_time).count());
#endif
}

MqttClient* MqttClient::_instance = nullptr;

MqttClient::MqttClient()
    : _pubsub()
    , _config{nullptr, 0, nullptr, nullptr, nullptr}
    , _sm(nullptr)
    , _rc(nullptr)
    , _rtc(nullptr)
    , _last_heartbeat_ms(0)
    , _is_initialized(false)
{
    _instance = this;
}

MqttClient::~MqttClient() {
    if (_instance == this) {
        _instance = nullptr;
    }
}

bool MqttClient::begin(MqttConfig config, ScheduleManager* sm, RelayController* rc, IClock* rtc) {
    if (!sm || !rc || !config.broker_host || !config.device_id) {
        ESP_LOGE(TAG, "Invalid MqttClient dependencies or config pointers");
        _is_initialized = false;
        return false;
    }
    _config = config;
    _sm = sm;
    _rc = rc;
    _rtc = rtc;
    _is_initialized = true;
    return true;
}

bool MqttClient::_buildLwtPayload(char* buffer, size_t buffer_size) const {
    if (!buffer || buffer_size == 0) return false;

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    StaticJsonDocument<256> doc;
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    doc["status"] = "offline";
    doc["device_id"] = (_config.device_id != nullptr) ? _config.device_id : "";
    doc["timestamp_utc"] = nullptr;

    size_t bytes = serializeJson(doc, buffer, buffer_size);
    return (bytes > 0 && bytes < buffer_size);
}

bool MqttClient::connect() {
    if (!_is_initialized) {
        ESP_LOGE(TAG, "Cannot connect: MqttClient not initialized");
        return false;
    }

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (WiFi.status() != WL_CONNECTED) {
        ESP_LOGW(TAG, "WiFi drop detected: skipping MQTT connect attempt");
        return false;
    }
#endif

    // 1. Build LWT JSON payload using StaticJsonDocument<256> via private helper
    char lwt_payload[256];
    if (!_buildLwtPayload(lwt_payload, sizeof(lwt_payload))) {
        ESP_LOGE(TAG, "Failed to build LWT JSON payload");
        return false;
    }

    // 2. Set server, callback, buffer size, and keep alive
    _pubsub.setServer(_config.broker_host, _config.broker_port);
    _pubsub.setCallback(_onMessage);
    _pubsub.setBufferSize(MQTT_BUFFER_SIZE);
    _pubsub.setKeepAlive(MQTT_KEEPALIVE_S);

    // Topic format: aeroponics/device/{device_id}/status
    char lwt_topic[128];
    snprintf(lwt_topic, sizeof(lwt_topic), "%s/%s/status",
             MQTT_TOPIC_BASE, _config.device_id ? _config.device_id : "unknown");

    // Security requirement: clientId = "aero-" + device_id (no raw MAC address)
    char client_id[128];
    snprintf(client_id, sizeof(client_id), "aero-%s",
             _config.device_id ? _config.device_id : "unknown");

    // 3. Connect to MQTT broker with LWT parameters (QoS=1, Retain=true)
    bool connected = _pubsub.connect(
        client_id,
        _config.username,
        _config.password,
        lwt_topic,
        1,          // willQoS = 1
        true,       // willRetain = true
        lwt_payload // willMessage
    );

    if (!connected) {
        ESP_LOGE(TAG, "PubSubClient connect failed with state: %d", _pubsub.state());
        return false;
    }

    ESP_LOGI(TAG, "Connected successfully to MQTT broker %s:%u as %s",
             _config.broker_host, _config.broker_port, client_id);

    // 4. Success handling: publish immediate heartbeat and subscribe to command topics with QoS=1
    publishHeartbeat();

    char sub_schedule[128];
    snprintf(sub_schedule, sizeof(sub_schedule), "%s/%s/command/relay/+/schedule",
             MQTT_TOPIC_BASE, _config.device_id ? _config.device_id : "unknown");

    char sub_override[128];
    snprintf(sub_override, sizeof(sub_override), "%s/%s/command/relay/+/override",
             MQTT_TOPIC_BASE, _config.device_id ? _config.device_id : "unknown");

    bool sub1 = _pubsub.subscribe(sub_schedule, 1);
    bool sub2 = _pubsub.subscribe(sub_override, 1);

    if (!sub1 || !sub2) {
        ESP_LOGW(TAG, "Warning: Wildcard topic subscriptions incomplete: schedule=%d, override=%d",
                 sub1, sub2);
    } else {
        ESP_LOGI(TAG, "Subscribed to wildcard command topics with QoS 1 successfully");
    }

    return true;
}

void MqttClient::loop() {
    if (isConnected()) {
        _pubsub.loop();
    }
}

bool MqttClient::publishHeartbeat() {
    if (!isConnected()) {
        ESP_LOGE(TAG, "[MQTT] publishHeartbeat FAILED: Client not connected");
        return false;
    }

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    StaticJsonDocument<MQTT_HEARTBEAT_DOC_SIZE> doc;
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    uint32_t now_ms = getSystemMillis();

    doc["status"] = "online";
    doc["device_id"] = (_config.device_id != nullptr) ? _config.device_id : "";
    doc["uptime_s"] = now_ms / 1000;

    int rssi = 0;
    uint32_t free_heap = 0;
    bool ntp_synced = false;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (WiFi.status() == WL_CONNECTED) {
        rssi = WiFi.RSSI();
    }
    free_heap = ESP.getFreeHeap();

    time_t now_sec = 0;
    time(&now_sec);
    if (now_sec > 1600000000L) {
        ntp_synced = true;
    }
#endif

    doc["rssi_dbm"] = rssi;
    doc["free_heap_b"] = free_heap;
    doc["ntp_synced"] = ntp_synced;

    bool rtc_valid = false;
    char time_str[32] = {0};

    if (_rtc != nullptr) {
        SystemTime t = _rtc->getTime();
        rtc_valid = t.is_valid;
        if (rtc_valid) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
            time_t now_sec = 0;
            time(&now_sec);
            if (now_sec > 1600000000L) {
                struct tm timeinfo;
                gmtime_r(&now_sec, &timeinfo);
                strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
            } else {
                snprintf(time_str, sizeof(time_str), "%02u:%02u:%02u", t.hour, t.minute, t.second);
            }
#else
            snprintf(time_str, sizeof(time_str), "%02u:%02u:%02u", t.hour, t.minute, t.second);
#endif
        }
    }

    doc["rtc_valid"] = rtc_valid;

    if (rtc_valid && time_str[0] != '\0') {
        doc["timestamp_utc"] = time_str;
    } else {
        doc["timestamp_utc"] = nullptr;
    }

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s/status",
             MQTT_TOPIC_BASE, (_config.device_id != nullptr) ? _config.device_id : "unknown");

    char payload[MQTT_HEARTBEAT_DOC_SIZE];
    size_t bytes = serializeJson(doc, payload, sizeof(payload));
    if (bytes == 0 || bytes >= sizeof(payload)) {
        ESP_LOGE(TAG, "[MQTT] publishHeartbeat FAILED: JSON serialization overflow");
        return false;
    }

    bool success = _pubsub.publish(topic, payload, false);
    if (!success) {
        ESP_LOGE(TAG, "[MQTT] publishHeartbeat FAILED");
        return false;
    }

    _last_heartbeat_ms = now_ms;
    ESP_LOGI(TAG, "Heartbeat published successfully to topic: %s", topic);
    return true;
}

bool MqttClient::publishRelayTelemetry(uint8_t relay_id, const RelayRuntimeState& state) {
    if (!isConnected()) {
        ESP_LOGE(TAG, "[MQTT] publishRelayTelemetry FAILED: Client not connected");
        return false;
    }

    uint8_t target_relay = relay_id;
    if (target_relay == 0) {
        target_relay = 1;
    }

    if (target_relay < 1 || target_relay > TOTAL_RELAYS) {
        ESP_LOGE(TAG, "[MQTT] publishRelayTelemetry FAILED: Invalid relay_id %u", relay_id);
        return false;
    }

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    StaticJsonDocument<512> doc;
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    const char* phase_str = "UNKNOWN";
    switch (state.phase) {
        case PHASE_SPRAYING:
            phase_str = "SPRAYING";
            break;
        case PHASE_COOLING_DOWN:
            phase_str = "COOLING_DOWN";
            break;
        default:
            phase_str = "UNKNOWN";
            break;
    }

    bool override_active = (_rc != nullptr) ? _rc->isOverrideActive(target_relay - 1) : false;

    doc["relay_id"] = target_relay;
    doc["state"] = phase_str;
    doc["phase_remaining_s"] = state.phase_remaining_s;
    doc["mode"] = state.is_night_mode ? "night" : "day";
    doc["override_active"] = override_active;

    bool rtc_valid = false;
    char time_str[32] = {0};

    if (_rtc != nullptr) {
        SystemTime t = _rtc->getTime();
        rtc_valid = t.is_valid;
        if (rtc_valid) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
            time_t now_sec = 0;
            time(&now_sec);
            if (now_sec > 1600000000L) {
                struct tm timeinfo;
                gmtime_r(&now_sec, &timeinfo);
                strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
            } else {
                snprintf(time_str, sizeof(time_str), "%02u:%02u:%02u", t.hour, t.minute, t.second);
            }
#else
            snprintf(time_str, sizeof(time_str), "%02u:%02u:%02u", t.hour, t.minute, t.second);
#endif
        }
    }

    if (rtc_valid && time_str[0] != '\0') {
        doc["timestamp_utc"] = time_str;
    } else {
        doc["timestamp_utc"] = nullptr;
    }

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s/telemetry/relay/%u",
             MQTT_TOPIC_BASE, (_config.device_id != nullptr) ? _config.device_id : "unknown", target_relay);

    char payload[512];
    size_t bytes = serializeJson(doc, payload, sizeof(payload));
    if (bytes == 0 || bytes >= sizeof(payload)) {
        ESP_LOGE(TAG, "[MQTT] publishRelayTelemetry FAILED: JSON serialization overflow");
        return false;
    }

    bool success = _pubsub.publish(topic, payload, false);
    if (!success) {
        ESP_LOGE(TAG, "[MQTT] publishRelayTelemetry FAILED");
        return false;
    }

    ESP_LOGI(TAG, "Relay telemetry published successfully to topic: %s", topic);
    return true;
}

bool MqttClient::isConnected() const {
    return const_cast<PubSubClient&>(_pubsub).connected();
}

int8_t MqttClient::_parseRelayId(const char* topic, const char** out_cmd_type) {
    if (!topic) return -1;
    const char* rel_ptr = strstr(topic, "/command/relay/");
    if (!rel_ptr) return -1;
    rel_ptr += 15; // strlen("/command/relay/")

    char* end_ptr = nullptr;
    long id_val = strtol(rel_ptr, &end_ptr, 10);
    if (end_ptr == rel_ptr || *end_ptr != '/') {
        return -1;
    }
    if (id_val < 1 || id_val > TOTAL_RELAYS) {
        return -1;
    }
    if (out_cmd_type) {
        *out_cmd_type = end_ptr + 1;
    }
    return static_cast<int8_t>(id_val);
}

void MqttClient::_onMessage(char* topic, uint8_t* payload, unsigned int length) {
    if (!_instance) return;

    // 1. Security BLOCKER: Validate length <= MQTT_BUFFER_SIZE - 1 to prevent buffer overflow
    if (length > MQTT_BUFFER_SIZE - 1) {
        ESP_LOGW(TAG, "[MQTT] Incoming payload length (%u) exceeds maximum buffer limit (%u)",
                 length, static_cast<unsigned int>(MQTT_BUFFER_SIZE - 1));
        return;
    }

    if (!payload || !topic) {
        ESP_LOGW(TAG, "[MQTT] Null topic or payload received");
        return;
    }

    // Null-terminate payload safely
    payload[length] = '\0';
    const char* json_str = reinterpret_cast<const char*>(payload);

    // 2. Parse topic -> command_type + relay_id via _parseRelayId()
    const char* cmd_type = nullptr;
    int8_t relay_id = _parseRelayId(topic, &cmd_type);

    // 4. Validate relay_id [1, 4]
    if (relay_id < 1 || relay_id > TOTAL_RELAYS || !cmd_type) {
        ESP_LOGW(TAG, "[MQTT] Invalid or unparseable command topic: %s", topic);
        return;
    }

    // 3. Deserialize JSON with StaticJsonDocument<MQTT_COMMAND_DOC_SIZE>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    StaticJsonDocument<MQTT_COMMAND_DOC_SIZE> doc;
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    DeserializationError error = deserializeJson(doc, json_str);

    // Rule S2-MQTT-02 (BLOCKER): Must check error before accessing doc[]
    if (error) {
        ESP_LOGE(TAG, "[MQTT] JSON deserialization failed for topic %s: %s", topic, error.c_str());
        return;
    }

    // 5. Route commands: /schedule -> updateProfile() -> publishRelayTelemetry(); /override -> startManualOverride() / cancelOverride()
    uint8_t zero_relay = static_cast<uint8_t>(relay_id - 1); // 0-based index

    if (strcmp(cmd_type, "schedule") == 0) {
        if (!_instance->_sm) {
            ESP_LOGE(TAG, "[MQTT] ScheduleManager instance unavailable");
            return;
        }

        uint32_t spray_val = 0;
        if (!doc["spray_duration_s"].isNull()) {
            spray_val = doc["spray_duration_s"].as<uint32_t>();
        } else if (!doc["spray_day_s"].isNull()) {
            spray_val = doc["spray_day_s"].as<uint32_t>();
        }

        uint32_t cooldown_val = 0;
        if (!doc["cooldown_duration_s"].isNull()) {
            cooldown_val = doc["cooldown_duration_s"].as<uint32_t>();
        } else if (!doc["cooldown_day_s"].isNull()) {
            cooldown_val = doc["cooldown_day_s"].as<uint32_t>();
        }

        if (spray_val == 0 || cooldown_val == 0) {
            ESP_LOGE(TAG, "[MQTT] Missing mandatory schedule parameters (spray_duration_s, cooldown_duration_s), spray=%u, cooldown=%u", spray_val, cooldown_val);
            return;
        }

        ESP_LOGI(TAG, "[MQTT] Parsed schedule command: spray_val=%u, cooldown_val=%u", spray_val, cooldown_val);

        RelayProfile profile;
        profile.spray_day_s = spray_val;
        profile.cooldown_day_s = cooldown_val;

        if (!doc["night_spray_duration_s"].isNull()) {
            profile.spray_night_s = doc["night_spray_duration_s"].as<uint32_t>();
        } else if (!doc["spray_night_s"].isNull()) {
            profile.spray_night_s = doc["spray_night_s"].as<uint32_t>();
        } else {
            profile.spray_night_s = profile.spray_day_s;
        }

        if (!doc["night_cooldown_duration_s"].isNull()) {
            profile.cooldown_night_s = doc["night_cooldown_duration_s"].as<uint32_t>();
        } else if (!doc["cooldown_night_s"].isNull()) {
            profile.cooldown_night_s = doc["cooldown_night_s"].as<uint32_t>();
        } else {
            profile.cooldown_night_s = profile.cooldown_day_s;
        }

        bool ok = _instance->_sm->updateProfile(zero_relay, profile);
        if (ok) {
            ESP_LOGI(TAG, "[MQTT] Successfully updated profile for relay %d", relay_id);
            RelayRuntimeState state = _instance->_sm->getRuntimeState(zero_relay);
            _instance->publishRelayTelemetry(relay_id, state);
        } else {
            ESP_LOGE(TAG, "[MQTT] Failed to update profile for relay %d (out of range or mutex lock failed)", relay_id);
        }
    } else if (strcmp(cmd_type, "override") == 0) {
        if (!_instance->_rc) {
            ESP_LOGE(TAG, "[MQTT] RelayController instance unavailable");
            return;
        }

        const char* action = doc["action"] | "";
        bool is_cancel = (strcmp(action, "CANCEL") == 0 || strcmp(action, "STOP") == 0 ||
                          strcmp(action, "CLEAR") == 0 || doc["cancel"].as<bool>());

        if (is_cancel) {
            bool ok = _instance->_rc->cancelOverride(zero_relay);
            if (ok) {
                ESP_LOGI(TAG, "[MQTT] Successfully cancelled override for relay %d", relay_id);
            } else {
                ESP_LOGE(TAG, "[MQTT] Failed to cancel override for relay %d", relay_id);
            }
        } else {
            const char* state_str = doc["state"] | "";
            RelayState forced_state = RELAY_OFF;
            if (strcmp(state_str, "ON") == 0 || strcmp(state_str, "SPRAYING") == 0 ||
                strcmp(action, "ON") == 0 || strcmp(action, "START") == 0 ||
                doc["state"].as<int>() == 1 || doc["state"].as<bool>()) {
                forced_state = RELAY_ON;
            }

            uint32_t duration_s = doc["duration_s"] | doc["duration"] | MIN_OVERRIDE_DURATION_S;

            bool ok = _instance->_rc->startManualOverride(zero_relay, forced_state, duration_s);
            if (ok) {
                ESP_LOGI(TAG, "[MQTT] Successfully started manual override (%s, %us) for relay %d",
                         (forced_state == RELAY_ON) ? "ON" : "OFF", duration_s, relay_id);
            } else {
                ESP_LOGE(TAG, "[MQTT] Failed to start manual override for relay %d", relay_id);
            }
        }

        if (_instance->_sm) {
            RelayRuntimeState state = _instance->_sm->getRuntimeState(zero_relay);
            _instance->publishRelayTelemetry(relay_id, state);
        }
    } else {
        ESP_LOGW(TAG, "[MQTT] Unknown command type: %s", cmd_type);
    }
}
