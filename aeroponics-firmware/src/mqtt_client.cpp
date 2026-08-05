#include "mqtt_client.h"

#include <cstdio>
#include <cstring>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <esp_log.h>
#include <WiFi.h>
#else
#include <chrono>
#ifndef ESP_LOGI
#define ESP_LOGI(tag, fmt, ...) printf("[INFO][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[WARN][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[ERROR][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif
#endif

namespace {
constexpr const char* TAG = "MQTT";

uint32_t getSystemMillis() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    return millis();
#else
    using namespace std::chrono;
    static const auto start_time = steady_clock::now();
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - start_time).count());
#endif
}

bool isDuration(JsonVariantConst value, uint32_t minimum, uint32_t maximum, uint32_t& out) {
    if (!value.is<uint32_t>()) return false;
    out = value.as<uint32_t>();
    return out >= minimum && out <= maximum;
}

bool fitsCString(const char* value, size_t capacity) {
    return value != nullptr && strnlen(value, capacity) < capacity;
}

bool isValidDeviceId(const char* value) {
    const size_t length = strnlen(value, MQTT_DEVICE_ID_MAX_LENGTH + 1);
    if (length == 0 || length > MQTT_DEVICE_ID_MAX_LENGTH) return false;
    for (size_t i = 0; i < length; ++i) {
        const char c = value[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

const char* phaseName(SchedulePhase phase) {
    switch (phase) {
        case PHASE_SPRAYING: return "SPRAYING";
        case PHASE_COOLING_DOWN: return "COOLING_DOWN";
        default: return "UNKNOWN";
    }
}
} // namespace

MqttClient* MqttClient::_instance = nullptr;

MqttClient::MqttClient()
    : _pubsub(), _config{nullptr, 0, nullptr, nullptr, nullptr}, _sm(nullptr), _rc(nullptr),
      _rtc(nullptr), _last_heartbeat_ms(0), _is_initialized(false) {
    _instance = this;
}

MqttClient::~MqttClient() {
    if (_instance == this) _instance = nullptr;
}

bool MqttClient::begin(MqttConfig config, ScheduleManager* sm, IRelayOutput* rc, IClock* rtc) {
    if (!sm || !rc || !config.broker_host || !config.device_id || config.broker_host[0] == '\0' ||
        config.device_id[0] == '\0' || !isValidDeviceId(config.device_id) ||
        !fitsCString(config.broker_host, MQTT_BROKER_HOST_BUFFER_SIZE) ||
        !fitsCString(config.username, MQTT_USERNAME_BUFFER_SIZE) ||
        !fitsCString(config.password, MQTT_PASSWORD_BUFFER_SIZE)) {
        ESP_LOGE(TAG, "Invalid MQTT configuration or dependencies");
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

bool MqttClient::_buildTopic(char* buffer, size_t buffer_size, const char* suffix) const {
    const int written = snprintf(buffer, buffer_size, "%s/%s%s", MQTT_TOPIC_BASE,
                                 _config.device_id, suffix);
    return written >= 0 && static_cast<size_t>(written) < buffer_size;
}

bool MqttClient::_buildRelayTopic(char* buffer, size_t buffer_size, const char* suffix,
                                  uint8_t relay_id) const {
    const int written = snprintf(buffer, buffer_size, "%s/%s%s%u", MQTT_TOPIC_BASE,
                                 _config.device_id, suffix, relay_id);
    return written >= 0 && static_cast<size_t>(written) < buffer_size;
}

bool MqttClient::_buildClientId(char* buffer, size_t buffer_size) const {
    const int written = snprintf(buffer, buffer_size, "%s%s", MQTT_CLIENT_ID_PREFIX,
                                 _config.device_id);
    return written >= 0 && static_cast<size_t>(written) < buffer_size;
}

bool MqttClient::_buildLwtPayload(char* buffer, size_t buffer_size) const {
    StaticJsonDocument<MQTT_LWT_DOC_SIZE> doc;
    doc["status"] = "offline";
    doc["device_id"] = _config.device_id;
    doc["timestamp_utc"] = nullptr;
    const size_t bytes = serializeJson(doc, buffer, buffer_size);
    return bytes > 0 && bytes < buffer_size;
}

bool MqttClient::connect() {
    if (!_is_initialized) return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (WiFi.status() != WL_CONNECTED) return false;
#endif
    char lwt_payload[MQTT_LWT_DOC_SIZE];
    char lwt_topic[MQTT_TOPIC_BUFFER_SIZE];
    char client_id[MQTT_CLIENT_ID_BUFFER_SIZE];
    if (!_buildLwtPayload(lwt_payload, sizeof(lwt_payload)) ||
        !_buildTopic(lwt_topic, sizeof(lwt_topic), MQTT_STATUS_SUFFIX) ||
        !_buildClientId(client_id, sizeof(client_id))) {
        ESP_LOGE(TAG, "MQTT connection fields exceed their configured buffers");
        return false;
    }
    _pubsub.setServer(_config.broker_host, _config.broker_port);
    _pubsub.setCallback(_onMessage);
    _pubsub.setBufferSize(MQTT_BUFFER_SIZE);
    _pubsub.setKeepAlive(MQTT_KEEPALIVE_S);
    if (!_pubsub.connect(client_id, _config.username, _config.password, lwt_topic, MQTT_LWT_QOS,
                         MQTT_LWT_RETAIN, lwt_payload)) {
        ESP_LOGE(TAG, "PubSubClient connect failed with state: %d", _pubsub.state());
        return false;
    }
    if (!publishHeartbeat() || !_subscribeCommandTopics()) {
        _pubsub.disconnect();
        return false;
    }
    return true;
}

bool MqttClient::_subscribeCommandTopics() {
    char schedule_topic[MQTT_TOPIC_BUFFER_SIZE];
    char override_topic[MQTT_TOPIC_BUFFER_SIZE];
    const int schedule_written = snprintf(schedule_topic, sizeof(schedule_topic), "%s/%s%s%s%s",
                                          MQTT_TOPIC_BASE, _config.device_id, MQTT_COMMAND_SUFFIX,
                                          MQTT_WILDCARD_SINGLE_LEVEL, MQTT_SCHEDULE_SUFFIX);
    const int override_written = snprintf(override_topic, sizeof(override_topic), "%s/%s%s%s%s",
                                          MQTT_TOPIC_BASE, _config.device_id, MQTT_COMMAND_SUFFIX,
                                          MQTT_WILDCARD_SINGLE_LEVEL, MQTT_OVERRIDE_SUFFIX);
    if (schedule_written < 0 || static_cast<size_t>(schedule_written) >= sizeof(schedule_topic) ||
        override_written < 0 || static_cast<size_t>(override_written) >= sizeof(override_topic)) {
        ESP_LOGE(TAG, "MQTT command subscription topic was truncated");
        return false;
    }
    if (!_pubsub.subscribe(schedule_topic, MQTT_COMMAND_QOS)) return false;
    if (!_pubsub.subscribe(override_topic, MQTT_COMMAND_QOS)) {
        _pubsub.disconnect();
        return false;
    }
    return true;
}

void MqttClient::loop() {
    if (isConnected()) _pubsub.loop();
}

bool MqttClient::_getTimestamp(char* buffer, size_t buffer_size) const {
    // RTC validity alone has no date/time-zone contract. Only a verified Unix
    // timestamp may populate the schema's ISO-8601 UTC field.
    if (!_rtc || !_rtc->getTime().is_valid || !_isNtpSynced()) return false;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    time_t now_sec = 0;
    time(&now_sec);
    struct tm timeinfo;
    gmtime_r(&now_sec, &timeinfo);
    return strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%SZ", &timeinfo) > 0;
#endif
    return false;
}

bool MqttClient::publishHeartbeat() {
    if (!isConnected()) return false;
    StaticJsonDocument<MQTT_HEARTBEAT_DOC_SIZE> doc;
    char timestamp[32] = {};
    const uint32_t now = getSystemMillis();
    doc["status"] = "online";
    doc["device_id"] = _config.device_id;
    doc["uptime_s"] = now / 1000U;
    doc["rssi_dbm"] = _getRssiDbm();
    doc["free_heap_b"] = _getFreeHeap();
    doc["ntp_synced"] = _isNtpSynced();
    doc["rtc_valid"] = _rtc && _rtc->getTime().is_valid;
    doc["timestamp_utc"] = timestamp[0] ? timestamp : nullptr;
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    if (!_buildTopic(topic, sizeof(topic), MQTT_STATUS_SUFFIX)) return false;
    char payload[MQTT_HEARTBEAT_PAYLOAD_SIZE];
    const size_t bytes = serializeJson(doc, payload, sizeof(payload));
    if (!bytes || bytes >= sizeof(payload)) return false;
    if (!_pubsub.publish(topic, payload, MQTT_PUBLISH_RETAIN)) return false;
    _last_heartbeat_ms = now;
    return true;
}

bool MqttClient::publishRelayTelemetry(uint8_t relay_id, const RelayRuntimeState& state) {
    if (!isConnected()) return false;
    const uint8_t target_relay = relay_id == 0 ? 1 : relay_id;
    if (target_relay < 1 || target_relay > TOTAL_RELAYS) return false;
    StaticJsonDocument<MQTT_TELEMETRY_DOC_SIZE> doc;
    char timestamp[32] = {};
    doc["relay_id"] = target_relay;
    doc["state"] = phaseName(state.phase);
    doc["phase_remaining_s"] = state.phase_remaining_s;
    doc["mode"] = state.is_night_mode ? "night" : "day";
    doc["override_active"] = _rc->isOverrideActive(target_relay - 1);
    doc["timestamp_utc"] = _getTimestamp(timestamp, sizeof(timestamp)) ? timestamp : nullptr;
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    if (!_buildRelayTopic(topic, sizeof(topic), MQTT_TELEMETRY_SUFFIX, target_relay)) return false;
    char payload[MQTT_TELEMETRY_PAYLOAD_SIZE];
    const size_t bytes = serializeJson(doc, payload, sizeof(payload));
    return bytes > 0 && bytes < sizeof(payload) && _pubsub.publish(topic, payload, MQTT_PUBLISH_RETAIN);
}

bool MqttClient::isConnected() const {
    return const_cast<PubSubClient&>(_pubsub).connected();
}

int8_t MqttClient::_parseRelayId(const char* topic, const char** out_cmd_type) {
    if (!_instance || !topic) return -1;
    char prefix[MQTT_TOPIC_BUFFER_SIZE];
    const int prefix_length = snprintf(prefix, sizeof(prefix), "%s/%s%s", MQTT_TOPIC_BASE,
                                       _instance->_config.device_id, MQTT_COMMAND_SUFFIX);
    if (prefix_length < 0 || static_cast<size_t>(prefix_length) >= sizeof(prefix) ||
        strncmp(topic, prefix, static_cast<size_t>(prefix_length)) != 0) return -1;
    const char* relay = topic + prefix_length;
    if (relay[0] < '1' || relay[0] > static_cast<char>('0' + TOTAL_RELAYS) || relay[1] != '/') return -1;
    const char* suffix = relay + 2;
    if (strcmp(suffix, MQTT_SCHEDULE_TOKEN) == 0) {
        if (out_cmd_type) *out_cmd_type = MQTT_SCHEDULE_TOKEN;
    } else if (strcmp(suffix, MQTT_OVERRIDE_TOKEN) == 0) {
        if (out_cmd_type) *out_cmd_type = MQTT_OVERRIDE_TOKEN;
    } else return -1;
    return static_cast<int8_t>(relay[0] - '0');
}

bool MqttClient::_parseSchedule(JsonDocument& doc, uint8_t relay_id, RelayProfile& profile) const {
    uint32_t relay_payload = 0;
    if (!doc.is<JsonObject>() || !doc[MQTT_RELAY_ID_KEY].is<uint32_t>() ||
        (relay_payload = doc[MQTT_RELAY_ID_KEY].as<uint32_t>()) != relay_id ||
        !isDuration(doc[MQTT_SPRAY_DAY_KEY], MIN_SPRAY_DURATION_S, MAX_SPRAY_DURATION_S, profile.spray_day_s) ||
        !isDuration(doc[MQTT_COOLDOWN_DAY_KEY], MIN_COOLDOWN_DURATION_S, MAX_COOLDOWN_DURATION_S, profile.cooldown_day_s) ||
        !isDuration(doc[MQTT_SPRAY_NIGHT_KEY], MIN_SPRAY_DURATION_S, MAX_SPRAY_DURATION_S, profile.spray_night_s) ||
        !isDuration(doc[MQTT_COOLDOWN_NIGHT_KEY], MIN_COOLDOWN_DURATION_S, MAX_COOLDOWN_DURATION_S, profile.cooldown_night_s)) return false;
    return true;
}

bool MqttClient::_parseOverride(JsonDocument& doc, uint8_t relay_id) {
    uint32_t payload_relay = 0;
    if (!doc.is<JsonObject>() || !doc[MQTT_RELAY_ID_KEY].is<uint32_t>() ||
        (payload_relay = doc[MQTT_RELAY_ID_KEY].as<uint32_t>()) != relay_id ||
        !doc[MQTT_OVERRIDE_ACTION_KEY].is<const char*>()) return false;
    const char* action = doc[MQTT_OVERRIDE_ACTION_KEY].as<const char*>();
    const uint8_t zero_relay = relay_id - 1;
    if (strcmp(action, MQTT_ACTION_CANCEL) == 0 || strcmp(action, MQTT_ACTION_CLEAR) == 0) {
        return _rc->cancelOverride(zero_relay);
    }
    if (strcmp(action, MQTT_ACTION_ON) == 0) {
        uint32_t duration = 0;
        return isDuration(doc[MQTT_OVERRIDE_DURATION_KEY], MIN_OVERRIDE_DURATION_S,
                          MAX_OVERRIDE_DURATION_S, duration) &&
               _rc->startManualOverride(zero_relay, RELAY_ON, duration);
    }
    if (strcmp(action, MQTT_ACTION_OFF) == 0) {
        uint32_t duration = 0;
        return isDuration(doc[MQTT_OVERRIDE_DURATION_KEY], MIN_OVERRIDE_DURATION_S,
                          MAX_OVERRIDE_DURATION_S, duration) &&
               _rc->startManualOverride(zero_relay, RELAY_OFF, duration);
    }
    if (strcmp(action, MQTT_ACTION_START) != 0 || !doc[MQTT_OVERRIDE_STATE_KEY].is<const char*>()) return false;
    const char* state = doc[MQTT_OVERRIDE_STATE_KEY].as<const char*>();
    uint32_t duration = 0;
    if (!isDuration(doc[MQTT_OVERRIDE_DURATION_KEY], MIN_OVERRIDE_DURATION_S, MAX_OVERRIDE_DURATION_S, duration)) return false;
    if (strcmp(state, MQTT_STATE_ON) == 0) return _rc->startManualOverride(zero_relay, RELAY_ON, duration);
    if (strcmp(state, MQTT_STATE_OFF) == 0) return _rc->startManualOverride(zero_relay, RELAY_OFF, duration);
    return false;
}

void MqttClient::_onMessage(char* topic, uint8_t* payload, unsigned int length) {
    if (!_instance || !_instance->_sm || !_instance->_rc || !topic || !payload ||
        length >= MQTT_BUFFER_SIZE) return;
    const char* command = nullptr;
    const int8_t relay_id = _parseRelayId(topic, &command);
    if (relay_id < 1 || !command) return;
    StaticJsonDocument<MQTT_COMMAND_DOC_SIZE> doc;
    const DeserializationError error = deserializeJson(doc, payload, length);
    if (error) return;
    const uint8_t zero_relay = static_cast<uint8_t>(relay_id - 1);
    bool applied = false;
    if (strcmp(command, MQTT_SCHEDULE_TOKEN) == 0) {
        RelayProfile profile{};
        applied = _instance->_parseSchedule(doc, static_cast<uint8_t>(relay_id), profile) &&
                  _instance->_sm->updateProfile(zero_relay, profile);
    } else if (strcmp(command, MQTT_OVERRIDE_TOKEN) == 0) {
        applied = _instance->_parseOverride(doc, static_cast<uint8_t>(relay_id));
    }
    if (!applied) {
        ESP_LOGW(TAG, "Rejected invalid or unapplied command for relay %d", relay_id);
        return;
    }
    RelayRuntimeState state = _instance->_sm->getRuntimeState(zero_relay);
    _instance->publishRelayTelemetry(static_cast<uint8_t>(relay_id), state);
}

int MqttClient::_getRssiDbm() const {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
#else
    return 0;
#endif
}

uint32_t MqttClient::_getFreeHeap() const {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    return ESP.getFreeHeap();
#else
    return 0;
#endif
}

bool MqttClient::_isNtpSynced() const {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    time_t now_sec = 0;
    time(&now_sec);
    return now_sec > 1600000000L;
#else
    return false;
#endif
}
