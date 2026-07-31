# Aeroponics Lean — Progress Tracker

---

## 📌 Started

| Field | Value |
|---|---|
| **Thời gian khởi tạo** | 2026-07-30T19:19:56+07:00 |
| **Sprint hiện tại bắt đầu** | 2026-07-30T19:43:57+07:00 |
| **Agent thực thi (Execution Agent)** | Gemini |

---

## 🎯 Reference Plan

- **Thư mục kế hoạch:** `.ai/planning/aeroponics-lean/`
- **Sprint tham chiếu hiện tại:** [sprint_1.md](file:///Users/benjaminhung8405/Code/aeroponics-project/.ai/planning/aeroponics-lean/sprint_1.md) — *Core Edge Engine & Hardware Fail-safe (Firmware)*

---

## 📝 Addition Plan

- **Các yêu cầu phát sinh:** Chưa có. Mặc định tuân thủ 100% phạm vi và quy chuẩn trong `sprint_1.md`. Mọi thay đổi scope phải được ghi thêm tại đây trước khi Agent thực thi.

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

## 🚀 Sprint 1 — Core Edge Engine & Hardware Fail-safe (Firmware)

> **Output bàn giao:** Firmware ESP32-S3 biên dịch được, khởi động an toàn (không glitch relay), đọc/ghi NVS, đồng bộ RTC, vận hành state machine phun/cooldown Ngày/Đêm cho 4 relay độc lập — **hoàn toàn offline, không cần MQTT**.

---

### TRACK A — Cấu hình & Build System

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **A1** | Khởi tạo `aeroponics-firmware/platformio.ini`: board `esp32-s3-devkitc-1`, framework `arduino`, `monitor_speed = 115200`, lib_deps (RTClib, PubSubClient, ArduinoJson ≥7.x), `build_flags = -DCORE_DEBUG_LEVEL=3`, `board_build.partitions = partitions.csv`. | [x] Done | QA duyệt 2026-07-31: platform/libs đều version-pinned, không chứa credential; build ESP32-S3 SUCCESS. |
| **A2** | Khởi tạo `aeroponics-firmware/partitions.csv`: NVS 20KB (`0x5000`), otadata 8KB, app0 OTA_0 1.875MB, app1 OTA_1 1.875MB. | [x] Done | QA duyệt 2026-07-31: partition hợp lệ, không overlap; build ESP32-S3 SUCCESS. |
| **A3** | Khởi tạo `aeroponics-firmware/include/config.h` — **Single Source of Truth** cho toàn bộ hằng số firmware: Pinout (`RELAY_PIN_1..4 = 1,2,3,4`; `RTC_SDA_PIN=21`; `RTC_SCL_PIN=22`), Schedule Defaults (`DEFAULT_SPRAY_DAY_S=30`, `DEFAULT_COOLDOWN_DAY_S=300`, `DEFAULT_SPRAY_NIGHT_S=30`, `DEFAULT_COOLDOWN_NIGHT_S=600`, `DAY_START_HOUR=6`, `NIGHT_START_HOUR=18`), FreeRTOS constants (`RELAY_TASK_STACK_SIZE=8192`, `RELAY_TASK_PRIORITY=3`, `RELAY_TASK_CORE=1`, `WDT_TIMEOUT_S=30`). | [ ] QA Review | Khắc phục QA lần 2 2026-07-31: tập trung NTP, Wi-Fi, scheduler/mutex và work-budget timings vào `config.h`; `pio test -e native` 20/20 PASS, ESP32-S3 build SUCCESS. |

---

### TRACK B — NVS Driver

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **B1** | Khởi tạo `aeroponics-firmware/include/nvs_storage.h` — Khai báo struct `RelayProfile` (`uint32_t spray_day_s`, `cooldown_day_s`, `spray_night_s`, `cooldown_night_s`) và interface class `NvsStorage` với 5 public methods: `begin()`, `loadProfile()`, `saveProfile()`, `loadAllProfiles()`, `factoryReset()`. | [x] Done | QA duyệt 2026-07-31: interface repository, declaration-only header và type safety đạt yêu cầu. |
| **B2** | Implement `aeroponics-firmware/src/nvs_storage.cpp` — `begin()`: `nvs_flash_init()` → `nvs_open()`, handle `ERR_NO_FREE_PAGES` bằng erase + reinit. `loadProfile()`: `nvs_get_u32()` với fallback default + range validation. `saveProfile()`: validate → `nvs_set_u32` x4 → `nvs_commit()`. `factoryReset()`: `nvs_erase_all()`. | [ ] QA Review | Khắc phục QA lần 2 2026-07-31: chỉ `ESP_ERR_NVS_NOT_FOUND` fallback; read/open error khác log `esp_err_to_name`, trả `false` và gán safe-default. Có regression test load lỗi; native 20/20 PASS. |

---

### TRACK C — RTC Driver

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **C1** | Khởi tạo `aeroponics-firmware/include/rtc_manager.h` — Khai báo struct `SystemTime` (`uint8_t hour, minute, second; bool is_valid`) và class `RtcManager` với 4 methods: `begin()`, `syncFromNtp()`, `getTime()`, `isNightMode()`. | [x] Done | QA duyệt 2026-07-31: Adapter interface, POD time snapshot và dependency boundary đạt yêu cầu. |
| **C2** | Implement `aeroponics-firmware/src/rtc_manager.cpp` — `syncFromNtp()`: `configTime()` → poll `getLocalTime()` tối đa 10s → `rtc.adjust()`. `getTime()`: Priority 1 DS3231, Priority 2 system time, Priority 3 `is_valid=false`. `isNightMode()`: `hour >= 18 || hour < 6`, nếu `is_valid=false` → default về DAY mode. | [ ] QA Review | Khắc phục QA lần 2 2026-07-31: dùng `NTP_POLL_INTERVAL_MS`, `NTP_SYNC_TIMEOUT_MS` và timeout system-time từ `config.h`; polling vẫn hữu hạn. Build ESP32-S3 SUCCESS. |

---

### TRACK D — Relay Controller

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **D1** | Khởi tạo `aeroponics-firmware/include/relay_controller.h` — Khai báo `enum RelayState { RELAY_OFF=0, RELAY_ON=1 }`, struct `RelayOverrideState { bool active; uint32_t remaining_s; RelayState forced_state; }`, và class `RelayController` với 6 methods: `initPins()`, `setRelay()`, `getRelayState()`, `startManualOverride()`, `cancelOverride()`, `isOverrideActive()`, `tickOverride()`. | [x] Done | QA duyệt 2026-07-31: HAL boundary, enum state và snapshot cache đạt yêu cầu. |
| **D2** | Implement `aeroponics-firmware/src/relay_controller.cpp` — `initPins()`: Với mỗi relay pin: `digitalWrite(pin, LOW)` → sau đó mới `pinMode(pin, OUTPUT)`. `setRelay()`: Map relay_id `[0-3]` → GPIO pin từ `config.h`. Cập nhật cache state. | [x] Done | QA duyệt 2026-07-31: GPIO LOW trước OUTPUT, Active HIGH, validation override và fault latch đạt yêu cầu. |

---

### TRACK E — Schedule Manager

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **E1** | Khởi tạo `aeroponics-firmware/include/schedule_manager.h` — Khai báo `enum SchedulePhase { PHASE_SPRAYING, PHASE_COOLING_DOWN }`, struct `RelayRuntimeState { SchedulePhase phase; uint32_t phase_remaining_s; RelayProfile current_profile; bool is_night_mode; }`, và class `ScheduleManager` với 4 methods: `begin()` (inject dependencies), `startAllTasks()`, `updateProfile()`, `getRuntimeState()`. | [x] Done | QA duyệt 2026-07-31: DI qua core interfaces, runtime state trả về by value và lifecycle contract rõ ràng. |
| **E2** | Implement `aeroponics-firmware/src/schedule_manager.cpp` — `startAllTasks()`: Tạo 4 `xTaskCreatePinnedToCore()` pinned CORE_1, stack 8192, priority 3. Task loop: Feed WDT → check override → get time → choose profile (day/night) → SPRAYING phase (relay ON, countdown) → COOLDOWN phase (relay OFF, countdown) → repeat. `updateProfile()`: Mutex-guarded RAM update + NVS save. | [x] Done | QA duyệt 2026-07-31: mutex profile, WDT, fail-safe lifecycle và hot-reload được regression-test. |

---

### TRACK F — main.cpp Orchestrator

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **F1** | Implement `aeroponics-firmware/src/main.cpp` — `setup()` theo đúng thứ tự: 1. `Serial.begin(115200)`, 2. `relay_controller.initPins()`, 3. `nvs_storage.begin()`, 4. `nvs_storage.loadAllProfiles()`, 5. `Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN)`, 6. `rtc_manager.begin()`, 7. WiFi connect (30s timeout), 8. NTP sync nếu connected, 9. `schedule_manager.begin()` + `startAllTasks()`, 10. LOG "Boot Complete". `loop()`: feed WDT + Serial debug commands + WiFi reconnect check mỗi 60s. | [x] Done | Đã QA duyệt ngày 2026-07-31: đóng các regression lifecycle `FAULTED`, S1-WDT-06, boot safety, input validation Serial và giới hạn hàm; xem bản ghi LGTM mới nhất trong `WALKTHROUGH_LOG.md`. |


---

## 🛡️ QA Gateways — Sprint 1 (Firmware)

Mọi Task khi chuyển từ `[ ] In Progress` → `[ ] QA Review` → `[x] Done` phải vượt qua **toàn bộ** 6 cổng kiểm tra sau:

| Rule ID | Tiêu chí PASS / FAIL | Severity |
|---|---|---|
| **S1-HW-01** | `digitalWrite(LOW)` đứng TRƯỚC `pinMode(OUTPUT)` cho MỌI relay pin trong `initPins()`. `initPins()` là lời gọi HW đầu tiên trong `setup()`. | 🔴 BLOCKER |
| **S1-NVS-02** | `saveProfile()` chỉ được gọi từ `updateProfile()` khi có thay đổi config. KHÔNG gọi trong vòng lặp, task timer, hoặc bất kỳ nơi nào khác. | 🔴 BLOCKER |
| **S1-NVS-03** | Mọi giá trị đọc từ NVS phải được validate range trước khi sử dụng: spray `[5-300]`, cooldown `[30-7200]`. | 🔴 BLOCKER |
| **S1-RTC-04** | `isNightMode()` trả về `false` (DAY) khi `is_valid=false`. Không được trả `is_valid=false` mà không log và không fallback. | 🟠 CRITICAL |
| **S1-MUTEX-05** | Mọi read/write `profiles_[]` qua `profile_mutex_`. Không có direct array access từ nhiều tasks. | 🔴 BLOCKER |
| **S1-WDT-06** | `esp_task_wdt_reset()` trong MỖI iteration của task loop. Không có `vTaskDelay()` quá 30s mà không feed WDT. | 🔴 BLOCKER |

**Build Gate:** `pio run` phải exit code 0, zero errors, zero critical warnings trước khi merge.

---

*Senior Solution Architect — Cập nhật: 2026-07-30T19:43:57+07:00 | Sprint 1 CONTINUE*
