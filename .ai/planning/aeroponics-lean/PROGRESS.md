# Aeroponics Lean — Progress Tracker

---

## 📌 Started

| Field | Value |
|---|---|
| **Thời gian khởi tạo** | 2026-07-30T19:19:56+07:00 |
| **Sprint 2 bắt đầu** | 2026-07-31T20:24:29+07:00 |
| **Agent thực thi (Execution Agent)** | Gemini |

---

## 🎯 Reference Plan

- **Thư mục kế hoạch:** `.ai/planning/aeroponics-lean/`
- **Sprint tham chiếu hiện tại:** [sprint_2.md](file:///Users/benjaminhung8405/Code/aeroponics-project/.ai/planning/aeroponics-lean/sprint_2.md) — *MQTT Protocol & Remote Control (Firmware)*

---

## 📝 Addition Plan

- **Các yêu cầu phát sinh:** Chưa có. Mặc định tuân thủ 100% phạm vi và quy chuẩn trong `sprint_2.md`. Mọi thay đổi scope phải được ghi thêm tại đây trước khi Agent thực thi.

---

## ✅ Sprint 0 — Infrastructure Setup (3 Containers Stack) — COMPLETED

> Sprint 0 đã hoàn thành và được duyệt. Lịch sử giữ lại để truy xuất kiểm toán.

### TRACK A — Docker Compose Topology & Orchestration

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **A1** | Khởi tạo `docker-compose.yml` khai báo 3 services (`timescaledb`, `mosquitto`, `aero-backend`) trên network `aero_net` cùng 2 named volumes persistent. | [x] Done | Declarative IaC, Least Privilege. CẤM expose 5432 ra host. Port mapping qua env vars. Strict `healthcheck` + `service_healthy` condition. |

### TRACK B — Database Schema & TimescaleDB

| Task ID | Mô tả Task | Status | Note |
|---|---|---|---|
| **B1** | Khởi tạo `database/schema.sql` — DDL 5 bảng, hypertables, indexes. | [x] Done | Idempotent DDL, CHECK constraints, seed 4 relay profile. |

### TRACK C — MQTT Broker & ACL

| Task ID | Mô tả Task | Status | Note |
|---|---|---|---|
| **C1** | `mosquitto.conf` — Dual Listener TCP + WebSocket. | [x] Done | `allow_anonymous false`, persistence, resource limits. |
| **C2** | `acl` — RBAC policy cho 3 roles. | [x] Done | Principle of Least Privilege. ESP32 không được ghi vào topic lạ. |

### TRACK D — NestJS Backend Placeholder

| Task ID | Mô tả Task | Status | Note |
|---|---|---|---|
| **D1** | `aeroponics-backend/Dockerfile` — Multi-Stage Build. | [x] Done | Builder → Runner, pnpm frozen-lockfile. |
| **D2** | `package.json` — Dependencies NestJS 11, TypeORM, mqtt, tuyapi. | [x] Done | Loại bỏ InfluxDB & Redis. |
| **D3** | `src/main.ts`, `app.module.ts` — Placeholder + `/health` endpoint. | [x] Done | Modular Architecture, AppConfigModule, no x-powered-by. |

### TRACK E — Environment Config

| Task ID | Mô tả Task | Status | Note |
|---|---|---|---|
| **E1** | `.env.example` — 12-Factor Config template. | [x] Done | Không commit secret. CHANGE_ME placeholders rõ ràng. |

### TRACK F — DevOps Scripts

| Task ID | Mô tả Task | Status | Note |
|---|---|---|---|
| **F1** | `scripts/setup.sh` — 1-click infra init. | [x] Done | `set -euo pipefail`, idempotent, cross-platform macOS/Linux. |
| **F2** | `scripts/health-check.sh` — Verification suite. | [x] Done | PASS/FAIL terminal report, 4 checks: containers, MQTT auth, DB, REST. |

---

## ✅ Sprint 1 — Core Edge Engine & Hardware Fail-safe (Firmware) — COMPLETED

> Sprint 1 đã hoàn thành và được duyệt. Lịch sử giữ lại để truy xuất kiểm toán.

> **Output bàn giao:** Firmware ESP32-S3 biên dịch được, khởi động an toàn (không glitch relay), đọc/ghi NVS, đồng bộ RTC, vận hành state machine phun/cooldown Ngày/Đêm cho 4 relay độc lập — **hoàn toàn offline, không cần MQTT**.

---

### TRACK A — Cấu hình & Build System

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **A1** | Khởi tạo `aeroponics-firmware/platformio.ini` | [x] Done | QA duyệt 2026-07-31. |
| **A2** | Khởi tạo `aeroponics-firmware/partitions.csv` | [x] Done | QA duyệt 2026-07-31. |
| **A3** | Khởi tạo `aeroponics-firmware/include/config.h` | [x] Done | QA duyệt 2026-07-31. |

---

### TRACK B — NVS Driver

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **B1** | Khởi tạo `aeroponics-firmware/include/nvs_storage.h` | [x] Done | QA duyệt 2026-07-31. |
| **B2** | Implement `aeroponics-firmware/src/nvs_storage.cpp` | [x] Done | QA duyệt 2026-07-31. |

---

### TRACK C — RTC Driver

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **C1** | Khởi tạo `aeroponics-firmware/include/rtc_manager.h` | [x] Done | QA duyệt 2026-07-31. |
| **C2** | Implement `aeroponics-firmware/src/rtc_manager.cpp` | [x] Done | QA duyệt 2026-07-31. |

---

### TRACK D — Relay Controller

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **D1** | Khởi tạo `aeroponics-firmware/include/relay_controller.h` | [x] Done | QA duyệt 2026-07-31. |
| **D2** | Implement `aeroponics-firmware/src/relay_controller.cpp` | [x] Done | QA duyệt 2026-07-31. |

---

### TRACK E — Schedule Manager

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **E1** | Khởi tạo `aeroponics-firmware/include/schedule_manager.h` | [x] Done | QA duyệt 2026-07-31. |
| **E2** | Implement `aeroponics-firmware/src/schedule_manager.cpp` | [x] Done | QA duyệt 2026-07-31. |

---

### TRACK F — main.cpp Orchestrator

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **F1** | Implement `aeroponics-firmware/src/main.cpp` | [x] Done | Đã QA duyệt ngày 2026-07-31. |

---

## 🚀 Sprint 2 — MQTT Protocol & Remote Control (Firmware)

> **Bắt đầu:** 2026-07-31T20:24:29+07:00 | **Agent thực thi:** Gemini
>
> **Phụ thuộc:** Sprint 1 hoàn thành — `NvsStorage`, `RtcManager`, `RelayController`, `ScheduleManager` hoạt động ổn định.
>
> **Output bàn giao:** ESP32-S3 kết nối Mosquitto Docker, publish heartbeat 10s, nhận remote command thay đổi schedule, MQTT LWT báo offline khi mất điện/mạng.

---

### TRACK A — MQTT Client Header (`mqtt_client.h`)

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **A1** | Khởi tạo `aeroponics-firmware/include/mqtt_client.h` — Khai báo `struct MqttConfig { const char* broker_host; uint16_t broker_port; const char* username; const char* password; const char* device_id; }` và class `MqttClient` với 6 public methods: `begin(MqttConfig, ScheduleManager*, RelayController*)`, `connect() → bool`, `loop()`, `publishHeartbeat()`, `publishRelayTelemetry(uint8_t relay_id, const RelayRuntimeState& state)`, `isConnected() const → bool`. | [ ] QA Review | **Đã sửa theo QA 2026-08-05; chờ QA Review lần 2.** |
| **A2** | Bổ sung MQTT constants vào `aeroponics-firmware/include/config.h` (SSOT): `MQTT_HEARTBEAT_INTERVAL_MS=10000`, `MQTT_RECONNECT_BASE_S=1`, `MQTT_RECONNECT_MAX_S=60`, `MQTT_BUFFER_SIZE=2048`, `MQTT_KEEPALIVE_S=30`, `MQTT_TASK_STACK_SIZE=8192`, `MQTT_TASK_PRIORITY=2`, `MQTT_TASK_CORE=0`, `MQTT_HEARTBEAT_DOC_SIZE=512`, `MQTT_COMMAND_DOC_SIZE=1024`. Topic prefix: `MQTT_TOPIC_BASE="aeroponics/device"`. | [ ] QA Review | **Đã sửa theo QA 2026-08-05; PubSubClient publish QoS 0 được ghi rõ theo API thực tế; chờ QA Review lần 2.** |

---

### TRACK B — MQTT Client Implementation (`mqtt_client.cpp`)

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **B1** | Implement `mqtt_client.cpp` — method `connect()`: (1) Build LWT JSON `{"status":"offline","device_id":"...","timestamp_utc":null}` dùng `StaticJsonDocument<256>`. (2) Gọi `_pubsub.setServer()`, `setCallback(_onMessage)`, `setBufferSize(MQTT_BUFFER_SIZE)`, `setKeepAlive(MQTT_KEEPALIVE_S)`. (3) `_pubsub.connect(clientId, user, pass, lwt_topic, 1, true, lwt_payload)`. (4) Nếu success: `publishHeartbeat()` → subscribe 2 wildcard topics QoS=1. Return `true/false`. | [ ] QA Review | **Đã sửa theo QA 2026-08-05; broker healthy, retained LWT và ACL evidence đã chạy; chờ QA Review lần 2.** |
| **B2** | Implement `publishHeartbeat()` — Build JSON: `status="online"`, `device_id`, `uptime_s` (millis()/1000), `rssi_dbm` (WiFi.RSSI()), `free_heap_b` (ESP.getFreeHeap()), `ntp_synced`, `rtc_valid`, `timestamp_utc`. Publish `aeroponics/device/{id}/status` QoS=0, Retain=false. Cập nhật `_last_heartbeat_ms`. | [ ] QA Review | **Đã sửa theo QA 2026-08-05; RTC hợp lệ nhưng NTP chưa sync phát `timestamp_utc: null`; chờ QA Review lần 2.** |
| **B3** | Implement `publishRelayTelemetry(uint8_t relay_id, const RelayRuntimeState& state)` — Build JSON: `relay_id`, `state` ("SPRAYING"/"COOLING_DOWN"), `phase_remaining_s`, `mode` ("day"/"night"), `override_active`, `timestamp_utc`. Publish `aeroponics/device/{id}/telemetry/relay/{relay_id}` QoS=0, Retain=false. | [ ] QA Review | **Đã sửa theo QA 2026-08-05; timestamp chỉ ISO-8601 UTC đã xác minh hoặc null; chờ QA Review lần 2.** |
| **B4** | Implement `_onMessage(char* topic, byte* payload, unsigned int length)` — (1) Validate `length <= MQTT_BUFFER_SIZE - 1` → `payload[length] = '\0'`. (2) Parse topic → `command_type` + `relay_id` qua `_parseRelayId()`. (3) `StaticJsonDocument<MQTT_COMMAND_DOC_SIZE>` deserialize. (4) Validate `relay_id [1,4]`. (5) Route: `/schedule` → `updateProfile()` → `publishRelayTelemetry()`; `/override` → `startManualOverride()` / `cancelOverride()`. | [ ] QA Review | **Đã sửa theo QA 2026-08-05; reject `length >= MQTT_BUFFER_SIZE`, regression 2047/2048; chờ QA Review lần 2.** |

---

### TRACK C — Tích hợp vào `main.cpp`

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **C1** | Thêm global `MqttClient mqtt_client;` và `MqttConfig mqtt_config` vào `main.cpp`. Đọc MQTT credentials từ NVS (hoặc `config_secret.h` với `#ifndef PRODUCTION` guard) sau bước WiFi connect. Gọi `mqtt_client.begin(mqtt_config, &schedule_manager, &relay_controller)`. | [ ] QA Review | **Đã sửa theo QA 2026-08-05; provisioning fail-closed, credentials local nằm ngoài Git; chờ QA Review lần 2.** |
| **C2** | Tạo `mqttTask()` FreeRTOS function: `LOOP: IF WiFi.isConnected() → IF !isConnected() → connect() + Exponential Backoff (backoff_s = min(backoff_s*2, MQTT_RECONNECT_MAX_S), reset=1 khi success) → ELSE → loop(). IF millis()-last_hb > MQTT_HEARTBEAT_INTERVAL_MS → publishHeartbeat(). vTaskDelay(100ms)`. Đăng ký: `xTaskCreatePinnedToCore(mqttTask, "mqtt_task", MQTT_TASK_STACK_SIZE, NULL, MQTT_TASK_PRIORITY, NULL, MQTT_TASK_CORE)`. | [ ] QA Review | **Đã sửa theo QA 2026-08-05; `setup()` còn 37 dòng, MQTT task Core 0/WDT giữ nguyên; chờ QA Review lần 2.** |

---

## 🛡️ QA Gateways — Sprint 2 (MQTT Firmware)

| Rule ID | Tiêu chí PASS / FAIL | Severity |
|---|---|---|
| **S2-MQTT-01** | LWT được set làm tham số của `connect()` (QoS=1, Retain=true). KHÔNG phải `setWill()` riêng lẻ. Broker PHẢI nhận LWT trước bất kỳ publish nào khác. | 🔴 BLOCKER |
| **S2-MQTT-02** | Mọi `deserializeJson()` đều check `DeserializationError` trước khi access `doc[]`. `StaticJsonDocument` size lấy từ `config.h` (≥1024 cho command). Payload `length` validate `<= MQTT_BUFFER_SIZE - 1` TRƯỚC null-terminate. | 🔴 BLOCKER |
| **S2-MQTT-03** | MQTT credentials (`username`, `password`, `broker_host`) không hardcode trong source code. Phải từ NVS hoặc `config_secret.h` trong `.gitignore`. | 🔴 BLOCKER |
| **S2-MQTT-04** | Exponential backoff: cap `MQTT_RECONNECT_MAX_S=60`, reset về `MQTT_RECONNECT_BASE_S=1` sau connect success. `esp_task_wdt_reset()` gọi trong mỗi iteration của reconnect loop. | 🔴 BLOCKER |
| **S2-MQTT-05** | MQTT task pin CORE_0. Relay tasks CORE_1. MQTT task KHÔNG gọi `digitalWrite()` trực tiếp. Giao tiếp relay qua `RelayController`/`ScheduleManager` (thread-safe mutex). | 🔴 BLOCKER |

**Build Gate:** `pio run` phải exit code 0, zero errors, zero critical warnings trước khi merge.

**Integration Gate:** Test end-to-end với Mosquitto Docker: `mosquitto_sub` xác nhận LWT `offline` retained khi device mất kết nối; heartbeat JSON parse đúng schema sau 10s; schedule command publish → profile thay đổi và persist qua NVS.

---

## 🛡️ QA Gateways — Sprint 1 (Firmware) — ARCHIVED

| Rule ID | Tiêu chí PASS / FAIL | Severity |
|---|---|---|
| **S1-HW-01** | `digitalWrite(LOW)` đứng TRƯỚC `pinMode(OUTPUT)` cho MỌI relay pin. `initPins()` là lời gọi HW đầu tiên trong `setup()`. | 🔴 BLOCKER |
| **S1-NVS-02** | `saveProfile()` chỉ được gọi từ `updateProfile()` khi có thay đổi config. KHÔNG gọi trong vòng lặp hay task timer. | 🔴 BLOCKER |
| **S1-NVS-03** | Mọi giá trị đọc từ NVS phải validate range trước khi sử dụng: spray `[5-300]`, cooldown `[30-7200]`. | 🔴 BLOCKER |
| **S1-RTC-04** | `isNightMode()` trả về `false` (DAY) khi `is_valid=false`. Không được không log và không fallback. | 🟠 CRITICAL |
| **S1-MUTEX-05** | Mọi read/write `profiles_[]` qua `profile_mutex_`. Không có direct array access từ nhiều tasks. | 🔴 BLOCKER |
| **S1-WDT-06** | `esp_task_wdt_reset()` trong MỖI iteration của task loop. Không có `vTaskDelay()` quá 30s mà không feed WDT. | 🔴 BLOCKER |

*Senior Solution Architect — Cập nhật: 2026-07-31T20:24:29+07:00 | Sprint 2 STARTED*
