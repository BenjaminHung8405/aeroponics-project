# Sprint 2: MQTT Protocol & Remote Control (Firmware)

> **Phụ thuộc:** Sprint 1 hoàn thành — `NvsStorage`, `RtcManager`, `RelayController`, `ScheduleManager` hoạt động ổn định.  
> **Output bàn giao:** ESP32-S3 kết nối Mosquitto Docker, publish heartbeat 10s, nhận remote command thay đổi schedule, MQTT LWT báo offline khi mất điện/mạng.

> **Điều chỉnh bắt buộc 2026-08-10:** Sprint này mở rộng thành **MQTT + RF gateway** cho 12 node bơm/van. MQTT không thay thế ACK của UART-over-RF. Xem [PROJECT_ALIGNMENT_2026-08-10.md](./PROJECT_ALIGNMENT_2026-08-10.md); nội dung mâu thuẫn với mô hình 4 relay trực tiếp không được triển khai production.

---

## 1. PHẠM VI & MỤC TIÊU

### 1.1 Modules bị tác động

| Module | Tier | Mô tả |
|---|---|---|
| **`mqtt_client.h/cpp`** | Firmware | ESP32 MQTT client: connect, publish, subscribe, LWT, reconnect backoff |
| **`schedule_manager.cpp`** | Firmware | Mở rộng `updateProfile()` để trigger từ MQTT command |
| **`main.cpp`** | Firmware | Tích hợp MqttClient vào boot sequence, tạo MQTT task |

### 1.2 Mục tiêu Sprint 2

- [ ] ESP32 connect MQTT với credentials, đăng ký LWT `offline` trước khi send bất cứ gì.
- [ ] Heartbeat publish `{"status":"online","uptime_s":N}` mỗi 10s đến `aeroponics/device/{device_id}/status`.
- [ ] Subscribe và xử lý command JSON từ `aeroponics/device/{device_id}/command/relay/+/schedule`.
- [ ] Khi nhận schedule mới → `schedule_manager.updateProfile()` → hiệu lực ngay.
- [ ] Publish state change khi relay đổi trạng thái.
- [ ] Reconnect tự động với Exponential Backoff (1s → 2s → 4s → max 60s).

---

## 2. KIẾN TRÚC & LUỒNG DỮ LIỆU

### 2.1 MQTT Topic Schema

```
aeroponics/
  device/
    {device_id}/
      status                               ← PUBLISH (heartbeat + LWT)
      telemetry/relay/{relay_id}           ← PUBLISH (state change)
      command/relay/{relay_id}/schedule    ← SUBSCRIBE (remote schedule update)
      command/relay/{relay_id}/override    ← SUBSCRIBE (manual override)
      config/profile/{relay_id}            ← PUBLISH (profile response/ACK)
  sensor/
    {sensor_id}/reading                    ← PUBLISH by Backend (Tuya data)
```

### 2.2 JSON Schema

**Heartbeat (QoS 0, No Retain):**
```json
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
```

**LWT (QoS 1, Retain = true):**
```json
{ "status": "offline", "device_id": "esp32s3-abc123", "timestamp_utc": null }
```

**Relay Telemetry (publish khi state change):**
```json
{
  "relay_id": 1,
  "state": "SPRAYING",
  "phase_remaining_s": 25,
  "mode": "day",
  "override_active": false,
  "timestamp_utc": "2026-07-30T04:30:00Z"
}
```

**Schedule Command (subscribe từ Backend/UI):**
```json
{
  "relay_id": 1,
  "spray_day_s": 30,
  "cooldown_day_s": 300,
  "spray_night_s": 30,
  "cooldown_night_s": 600
}
```

**Override Command (subscribe):**
```json
{ "relay_id": 1, "action": "on", "duration_s": 120 }
```

### 2.3 Luồng MQTT Connect & LWT

```
[ESP32 Boot — sau WiFi connected]
        │
        ▼
[mqtt_client.connect()]
  1. Set LWT TRƯỚC KHI gọi connect:
     Topic: aeroponics/device/{id}/status
     Payload: '{"status":"offline",...}'
     QoS: 1, Retain: true
  2. Gọi connect(host, port, user, pass, lwt)
  3. Nếu thành công:
     a. Publish {"status":"online",...} (QoS 1, Retain true)
     b. Subscribe command topics
     c. Start heartbeat task
  4. Nếu fail → Exponential Backoff
```

### 2.4 Luồng Reconnect

```
[MQTT Disconnect]
        │
        ▼
backoff_s = 1
LOOP:
  vTaskDelay(backoff_s * 1000ms)
  attempt connect()
  IF success: backoff_s = 1; BREAK
  ELSE: backoff_s = min(backoff_s * 2, 60)
```

---

## 3. PHÂN RÃ CHI TIẾT TÁC VỤ

### Task A — `mqtt_client.h`

**Struct:**
```cpp
struct MqttConfig {
  const char* broker_host;
  uint16_t    broker_port;
  const char* username;
  const char* password;
  const char* device_id;
};
```

**Interface:**
| Hàm | Mô tả |
|---|---|
| `begin(MqttConfig, ScheduleManager*, RelayController*)` | Inject dependencies |
| `connect()` → `bool` | Connect với LWT registration |
| `loop()` | Feed PubSubClient loop, heartbeat |
| `publishHeartbeat()` | Publish heartbeat đầy đủ metadata |
| `publishRelayTelemetry(relay_id, state)` | Publish relay state change |
| `isConnected()` → `bool` | Trạng thái kết nối |

### Task B — `mqtt_client.cpp`

**`connect()` logic:**
```
1. Build LWT JSON payload
2. pubsub.setServer(host, port)
3. pubsub.setCallback(onMessage)
4. pubsub.setBufferSize(2048)
5. pubsub.setKeepAlive(30)
6. pubsub.connect(clientId, user, pass, lwt_topic, QoS=1, retain=true, lwt_payload)
7. IF success:
   a. publishHeartbeat()
   b. subscribe("aeroponics/device/{id}/command/relay/+/schedule", QoS=1)
   c. subscribe("aeroponics/device/{id}/command/relay/+/override", QoS=1)
   d. return true
```

**`onMessage(topic, payload, len)` logic:**
```
1. Parse topic → xác định command_type và relay_id
2. Parse JSON với ArduinoJson
3. IF error: log ERROR, return
4. Validate relay_id [1,4]
5. Route:
   IF "/schedule": validate fields → updateProfile() → publishRelayTelemetry()
   IF "/override": parse action (on/off/flush/cancel) → startManualOverride()/cancelOverride()
```

### Task C — Tích hợp vào `main.cpp`

**Thêm vào `setup()` sau WiFi connected:**
```cpp
mqtt_client.begin(mqtt_config, &schedule_manager, &relay_controller);
xTaskCreatePinnedToCore(mqttTask, "mqtt_task", 8192, NULL, 2, NULL, 0);
//                                                               ^CORE_0 (tách với relay tasks ở CORE_1)
```

**`mqttTask()` function:**
```
LOOP:
  IF WiFi connected:
    IF !mqtt_client.isConnected(): connect() với backoff
    ELSE: mqtt_client.loop()
  IF millis() - last_heartbeat > 10000:
    mqtt_client.publishHeartbeat()
  vTaskDelay(100ms)
```

---

## 4. TIÊU CHUẨN RÀ SOÁT CỨNG (Sprint 2)

### Rule S2-MQTT-01: LWT Registration Trước connect()
```
PASS: LWT được set vào PubSubClient TRƯỚC khi gọi connect()
FAIL: LWT QoS < 1 hoặc Retain = false
```

### Rule S2-MQTT-02: JSON Buffer Protection
```
PASS: DynamicJsonDocument size >= 1024 bytes
PASS: Kiểm tra DeserializationError trước khi access doc[]
FAIL: Access doc[] mà không check error
```

### Rule S2-MQTT-03: Credentials Không Hardcode
```
PASS: Credentials từ NVS hoặc config.h (trong .gitignore)
FAIL: Credentials hardcode trong source code hoặc platformio.ini
```

### Rule S2-MQTT-04: Exponential Backoff Cap
```
PASS: Backoff không vượt quá 60s, reset về 1s sau khi connect thành công
FAIL: Infinite retry không có delay
```

### Rule S2-MQTT-05: Task Isolation
```
PASS: MQTT task trên CORE_0, Relay tasks trên CORE_1
PASS: Giao tiếp qua thread-safe updateProfile() (mutex)
FAIL: MQTT task gọi trực tiếp digitalWrite()
```

---

*Sprint 2 Planning — yêu cầu RF gateway/12 node cập nhật 2026-08-10.*
