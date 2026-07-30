# Sprint 2: MQTT Protocol & Remote Control (Firmware + Infra)

> **Phụ thuộc:** Sprint 1 hoàn thành — `NvsStorage`, `RtcManager`, `RelayController`, `ScheduleManager` đã hoạt động ổn định.
> **Output bàn giao:** ESP32-S3 kết nối MQTT Broker (Mosquitto Docker), publish heartbeat 10s, nhận remote command thay đổi schedule, MQTT LWT báo offline khi mất điện/mạng. Infra Docker Compose chạy ổn định với persistent volume.

---

## 1. PHẠM VI & MỤC TIÊU

### 1.1 Modules / Components bị tác động trực tiếp

| Module | Tier | Mô tả |
|---|---|---|
| **`mqtt_client.h/cpp`** | Firmware | ESP32 MQTT client: connect, publish, subscribe, LWT, reconnect backoff |
| **`schedule_manager.cpp`** | Firmware | Mở rộng `updateProfile()` để trigger từ MQTT command |
| **`main.cpp`** | Firmware | Tích hợp MqttClient vào boot sequence, tạo MQTT task |
| **`docker-compose.yml`** | Infra | Mosquitto + (placeholder NestJS, TimescaleDB cho Sprint 3) |
| **`mosquitto/config/mosquitto.conf`** | Infra | Authentication, ACL, persistence, TLS-ready config |
| **`mosquitto/config/passwd`** | Infra | File password cho MQTT authentication |

### 1.2 Mục tiêu cụ thể của Sprint 2

- [ ] MQTT Broker (Mosquitto) chạy trong Docker với authentication bắt buộc.
- [ ] ESP32 connect MQTT với credentials, đăng ký LWT `offline` trước khi send bất cứ gì.
- [ ] Heartbeat publish `{"status":"online","uptime_s":N}` mỗi 10s đến topic `aeroponics/device/{device_id}/status`.
- [ ] Subscribe và xử lý đúng command JSON từ `aeroponics/device/{device_id}/command/relay/+/schedule`.
- [ ] Khi nhận schedule mới → gọi `schedule_manager.updateProfile()` → hiệu lực ngay.
- [ ] Publish state change mỗi khi relay đổi trạng thái (phun/nghỉ/override).
- [ ] Reconnect tự động với Exponential Backoff (1s → 2s → 4s → ... → max 60s).

---

## 2. KIẾN TRÚC & LUỒNG DỮ LIỆU

### 2.1 MQTT Topic Taxonomy (Full Schema)

```
aeroponics/
  device/
    {device_id}/                           ← device_id = MAC address hoặc custom ID
      status                               ← PUBLISH (heartbeat + LWT)
      telemetry/relay/{relay_id}           ← PUBLISH (state change)
      telemetry/relay/{relay_id}/remaining ← PUBLISH (countdown tick, optional)
      command/relay/{relay_id}/schedule    ← SUBSCRIBE (remote schedule update)
      command/relay/{relay_id}/override    ← SUBSCRIBE (manual override command)
      command/ota                          ← SUBSCRIBE (OTA trigger, Sprint 4+)
      config/profile/{relay_id}            ← PUBLISH (current profile response)
```

### 2.2 JSON Schema Chi Tiết

**Heartbeat / LWT Status Topic:**
```json
// Heartbeat (QoS 0, No Retain)
{
  "status": "online",
  "device_id": "esp32s3-abc123",
  "uptime_s": 3600,
  "rssi_dbm": -65,
  "free_heap_b": 245760,
  "ntp_synced": true,
  "rtc_valid": true,
  "timestamp_utc": "2026-07-30T04:30:00Z"
}

// LWT Message (QoS 1, Retain = true)
{
  "status": "offline",
  "device_id": "esp32s3-abc123",
  "timestamp_utc": null
}
```

**Relay Telemetry (publish khi state change):**
```json
{
  "relay_id": 1,
  "state": "SPRAYING",          // SPRAYING | COOLING_DOWN | MANUAL_ON | MANUAL_OFF | FLUSH
  "phase_remaining_s": 25,
  "mode": "day",                // day | night
  "override_active": false,
  "timestamp_utc": "2026-07-30T04:30:00Z"
}
```

**Schedule Command (subscribe, gửi từ UI/Backend):**
```json
{
  "relay_id": 1,
  "spray_day_s": 30,
  "cooldown_day_s": 300,
  "spray_night_s": 30,
  "cooldown_night_s": 600,
  "requested_by": "ui-user-1"
}
```

**Override Command (subscribe):**
```json
{
  "relay_id": 1,
  "action": "on",              // on | off | flush | cancel
  "duration_s": 120,           // Bỏ qua nếu action = cancel
  "requested_by": "ui-user-1"
}
```

### 2.3 Luồng MQTT Connect & LWT Registration

```
[ESP32 Boot — sau khi WiFi connected]
        │
        ▼
[mqtt_client.connect()]
  1. Tạo MQTT client với clientID = device_id
  2. Set LWT TRƯỚC KHI gọi connect:
     - Topic: aeroponics/device/{device_id}/status
     - Payload: '{"status":"offline","device_id":"...","timestamp_utc":null}'
     - QoS: 1, Retain: true
  3. Gọi mqtt_client.connect(host, port, user, pass, lwt)
  4. Nếu kết nối thành công:
     a. Publish '{"status":"online",...}' lên /status (QoS 1, Retain = true)
     b. Subscribe tất cả command topics
     c. Start heartbeat FreeRTOS task
  5. Nếu fail → Exponential Backoff, retry
```

### 2.4 Luồng Reconnect với Exponential Backoff

```
[MQTT Disconnect Event]
        │
        ▼
mqtt_connected_ = false
backoff_s = 1

LOOP:
  vTaskDelay(backoff_s * 1000ms)
  attempt mqtt_client.connect()
  IF success:
    mqtt_connected_ = true
    backoff_s = 1
    BREAK
  ELSE:
    backoff_s = min(backoff_s * 2, 60)  ← Cap tại 60s
    LOG: "Reconnect fail, next in Ns"
```

### 2.5 Luồng Xử Lý MQTT Command

```
[Mosquitto Broker] ──PUBLISH──▶ [ESP32 callback: onMqttMessage()]
        │
        ▼
Bước 1: Parse topic để xác định command type & relay_id
  Topic: aeroponics/device/abc/command/relay/1/schedule
  → command_type = "schedule", relay_id = 1

Bước 2: Parse JSON payload với ArduinoJson
  Nếu JSON invalid → log ERROR, RETURN (không crash)

Bước 3: Validate payload fields
  Nếu field missing hoặc out of range → publish error response, RETURN

Bước 4: Route theo command_type:
  "schedule" → schedule_manager.updateProfile(relay_id, new_profile)
               → nvs_storage.saveProfile(relay_id, new_profile)  [ghi NVS]
               → publish telemetry với state mới
  "override" → relay_controller.startManualOverride(relay_id, state, duration_s)
               → publish telemetry với override state

Bước 5: Publish ACK (confirmation) lên /config/profile/{relay_id}
  Payload: profile hiện tại sau khi update
```

### 2.6 Luồng Infra Docker Compose

```
[docker-compose up]
        │
        ├──▶ mosquitto container
        │      - Mount: ./mosquitto/config/mosquitto.conf
        │      - Mount: ./mosquitto/data (persistent)
        │      - Mount: ./mosquitto/log
        │      - Port: 1883 (MQTT), 9001 (WebSocket MQTT)
        │
        └──▶ (Placeholder) nestjs-backend, timescaledb — cấu hình nhưng chưa bật
```

---

## 3. PHÂN RÃ CHI TIẾT TÁC VỤ

### TRACK A — Tầng Hạ Tầng (Infrastructure)

---

#### Task A-1: Tạo Docker Compose và Mosquitto Config
**File tạo mới:** `aeroponics-project/docker-compose.yml`

**Services cần định nghĩa:**

| Service | Image | Port | Volume | Restart Policy |
|---|---|---|---|---|
| `mosquitto` | `eclipse-mosquitto:2.0` | `1883:1883`, `9001:9001` | `./mosquitto/config`, `mosquitto_data`, `mosquitto_log` | `unless-stopped` |
| `timescaledb` | `timescale/timescaledb:latest-pg15` | `5432` (internal only) | `timescale_data` | `unless-stopped` |
| `redis` | `redis:7-alpine` | `6379` (internal only) | — | `unless-stopped` |

**Networks:** Tất cả services trong `aeroponics_net` (bridge). Chỉ `mosquitto` expose port ra host.

---

#### Task A-2: Cấu hình Mosquitto Broker
**File tạo mới:** `mosquitto/config/mosquitto.conf`

**Các directive bắt buộc:**
```
listener 1883 0.0.0.0
listener 9001 0.0.0.0 (WebSocket protocol)
protocol websockets

allow_anonymous false
password_file /mosquitto/config/passwd

persistence true
persistence_location /mosquitto/data/
log_dest file /mosquitto/log/mosquitto.log
log_type all

# Retained message cho LWT
retain_available true

# Max inflight messages (QoS 1/2)
max_inflight_messages 20

# Keep-alive
keepalive_interval 60
```

**File tạo mới:** `mosquitto/config/passwd`
- Tạo bằng lệnh: `mosquitto_passwd -c passwd esp32_device`
- User `esp32_device` cho firmware.
- User `nestjs_backend` cho backend.
- Credentials lưu trong `.env`, không hardcode.

---

### TRACK B — Tầng Nghiệp Vụ MQTT (Firmware)

---

#### Task B-1: Định nghĩa Interface `MqttClient` trong Header
**File tạo mới:** `aeroponics-firmware/include/mqtt_client.h`

**Struct cần định nghĩa:**
```cpp
struct MqttConfig {
  const char* broker_host;
  uint16_t    broker_port;     // 1883
  const char* username;
  const char* password;
  const char* device_id;
  const char* client_id;       // = device_id thường
};

struct MqttPublishOptions {
  uint8_t qos;                 // 0, 1, hoặc 2
  bool    retain;
};
```

**Class `MqttClient` — Public Interface:**

| Hàm | Signature | Mô tả |
|---|---|---|
| `begin` | `void begin(MqttConfig cfg, ScheduleManager* sm, RelayController* rc)` | Inject dependencies, config client |
| `connect` | `bool connect()` | Connect với LWT registration. Trả về false nếu fail. |
| `disconnect` | `void disconnect()` | Graceful disconnect, publish status=offline chủ động |
| `loop` | `void loop()` | Gọi liên tục từ MQTT task, xử lý keep-alive và callback |
| `publish` | `bool publish(const char* topic, const char* payload, MqttPublishOptions opts)` | Generic publish |
| `publishHeartbeat` | `void publishHeartbeat()` | Publish heartbeat với đầy đủ metadata |
| `publishRelayTelemetry` | `void publishRelayTelemetry(uint8_t relay_id, RelayRuntimeState state)` | Publish trạng thái relay |
| `isConnected` | `bool isConnected()` | Trả về trạng thái kết nối |
| `onMessageCallback` | `static void onMessageCallback(char* topic, byte* payload, uint len)` | PubSubClient callback |

---

#### Task B-2: Triển khai MqttClient với Reconnect Logic
**File tạo mới:** `aeroponics-firmware/src/mqtt_client.cpp`

**Logic chi tiết `connect()`:**
```
1. Tạo LWT payload JSON: {"status":"offline","device_id":"..."}
2. pubsub_client_.setServer(cfg.broker_host, cfg.broker_port)
3. pubsub_client_.setCallback(MqttClient::onMessageCallback)
4. pubsub_client_.setBufferSize(2048)  ← Tăng buffer cho JSON lớn
5. pubsub_client_.setKeepAlive(30)     ← Keep-alive 30s
6. pubsub_client_.connect(
     client_id, username, password,
     lwt_topic, 1,          ← QoS 1
     true,                  ← Retain
     lwt_payload
   )
7. IF connected:
   a. publishHeartbeat() ngay lập tức
   b. subscribe("aeroponics/device/{id}/command/relay/+/schedule", QoS 1)
   c. subscribe("aeroponics/device/{id}/command/relay/+/override", QoS 1)
   d. subscribe("aeroponics/device/{id}/command/ota", QoS 1)
   e. mqtt_connected_ = true
   f. backoff_s_ = 1
   g. return true
8. ELSE: return false
```

**Logic chi tiết `onMessageCallback(topic, payload, len)`:**
```
1. Tạo String topic_str(topic)
2. Tạo buffer char[len+1], copy payload, null-terminate
3. Dùng ArduinoJson: DynamicJsonDocument doc(1024)
   deserializeJson(doc, buffer)
   IF error: log ERROR "Invalid JSON", return

4. Parse topic để xác định command_type và relay_id:
   IF topic contains "/command/relay/" AND "/schedule":
     relay_id = extract_relay_id(topic_str)   // parse số từ topic path
     handleScheduleCommand(relay_id, doc)
   IF topic contains "/command/relay/" AND "/override":
     relay_id = extract_relay_id(topic_str)
     handleOverrideCommand(relay_id, doc)
   IF topic contains "/command/ota":
     handleOtaCommand(doc)
```

**`handleScheduleCommand(relay_id, doc)`:**
```
1. Validate relay_id: 1 ≤ relay_id ≤ 4
2. Validate fields tồn tại: spray_day_s, cooldown_day_s, spray_night_s, cooldown_night_s
3. Validate ranges (như Rule S1-NVS-03)
4. Tạo RelayProfile từ JSON
5. schedule_manager_->updateProfile(relay_id, profile)
   → Cập nhật RAM + ghi NVS (1 lần duy nhất)
6. Publish ACK: publishRelayTelemetry(relay_id, sm_->getRuntimeState(relay_id))
```

**`handleOverrideCommand(relay_id, doc)`:**
```
1. Parse "action": on | off | flush | cancel
2. IF action == "cancel": relay_controller_->cancelOverride(relay_id)
3. IF action == "on": relay_controller_->startManualOverride(relay_id, RELAY_ON, doc["duration_s"])
4. IF action == "off": relay_controller_->startManualOverride(relay_id, RELAY_OFF, doc["duration_s"])
5. IF action == "flush": relay_controller_->startCalibrationFlush(relay_id, doc["duration_s"])
6. Publish telemetry
```

---

#### Task B-3: Tạo FreeRTOS MQTT Task trong main.cpp
**File sửa đổi:** `aeroponics-firmware/src/main.cpp`

**Thêm vào `setup()` sau khi wifi connected:**
```
mqtt_client.begin(mqtt_config, &schedule_manager, &relay_controller)
xTaskCreatePinnedToCore(mqttTask, "mqtt_task", 8192, NULL, 2, NULL, 0)
                                                              ^CORE_0 (tách với relay tasks ở CORE_1)
```

**`mqttTask(void* param)` function:**
```
LOOP vĩnh viễn:
  IF WiFi.status() == WL_CONNECTED:
    IF !mqtt_client.isConnected():
      result = mqtt_client.connect()
      IF !result: vTaskDelay(backoff_s * 1000); backoff*=2; cap 60
    ELSE:
      mqtt_client.loop()                    ← Feed PubSubClient loop

  IF millis() - last_heartbeat > 10000:
    mqtt_client.publishHeartbeat()
    last_heartbeat = millis()

  vTaskDelay(100ms)
```

**Thêm `publishRelayTelemetry()` hook vào `relay_controller.setRelay()`:**
- Sau mỗi lần relay state thay đổi, gọi `mqtt_client.publishRelayTelemetry()`.
- Dùng pointer đến MqttClient (inject vào RelayController hoặc dùng global singleton).

---

## 4. TIÊU CHUẨN RÀ SOÁT CỨNG (Sprint 2 Hardened Rules)

### Rule S2-MQTT-01: LWT Registration Trước connect()
```
PASS: LWT payload và topic được set vào PubSubClient TRƯỚC khi gọi connect()
FAIL: connect() được gọi mà không có LWT đã đăng ký
FAIL: LWT QoS < 1 hoặc Retain = false
```
**Kiểm tra:** Review code `mqtt_client.cpp connect()`, verify thứ tự gọi.

### Rule S2-MQTT-02: JSON Buffer Overflow Protection
```
PASS: DynamicJsonDocument size >= 1024 bytes cho mọi deserialize
PASS: Kiểm tra DeserializationError trước khi access doc["field"]
FAIL: Dùng StaticJsonDocument với size không đủ cho payload
FAIL: Access doc[] mà không kiểm tra error
```

### Rule S2-MQTT-03: MQTT Authentication Non-Negotiable
```
PASS: Mosquitto config có allow_anonymous = false
PASS: Firmware connect với username + password từ NVS/config (không hardcode trong source)
FAIL: allow_anonymous = true trong bất kỳ môi trường nào
FAIL: Credentials hardcode trong source code hoặc platformio.ini
```

### Rule S2-MQTT-04: Exponential Backoff Cap
```
PASS: Backoff interval KHÔNG vượt quá 60s (max_backoff = 60)
PASS: Backoff reset về 1s sau khi kết nối thành công
FAIL: Infinite retry không có delay (busy loop)
FAIL: Fixed retry interval (không adaptive)
```

### Rule S2-MQTT-05: MQTT Task Isolation từ Relay Tasks
```
PASS: MQTT task chạy trên CORE_0, Relay tasks chạy trên CORE_1
PASS: Giao tiếp giữa MQTT task và Schedule Manager qua thread-safe updateProfile() (mutex)
FAIL: MQTT task gọi trực tiếp digitalWrite() hoặc relay_controller.setRelay() mà không qua scheduler
```

### Rule S2-INFRA-06: Mosquitto Persistent Data
```
PASS: Docker volume cho mosquitto/data được mount persistent (không tmpfs)
PASS: Retained messages (LWT) survive broker restart
FAIL: Dùng --rm hoặc không mount volume cho mosquitto data
```

### Rule S2-MQTT-07: Payload Size Guard
```
PASS: PubSubClient buffer size set >= 2048 bytes (setBufferSize)
PASS: Heartbeat payload < 512 bytes
FAIL: Publish payload > buffer size (silently drops message)
```

---

*Sprint 2 Planning — Khởi tạo bởi Baseline Agent ngày 2026-07-30*
*Thực thi: Sprint 2 Implementation Agent (phụ thuộc Sprint 1 complete)*
