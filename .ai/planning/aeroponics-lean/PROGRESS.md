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
| **A1** | Khởi tạo `aeroponics-firmware/platformio.ini`: board `esp32-s3-devkitc-1`, framework `arduino`, `monitor_speed = 115200`, lib_deps (RTClib, PubSubClient, ArduinoJson ≥7.x), `build_flags = -DCORE_DEBUG_LEVEL=3`, `board_build.partitions = partitions.csv`. | [ ] QA Review | • **Design Pattern:** Build-as-Code, Reproducible Environment.<br>• **Anti-Technical Debt:** Cố định phiên bản libs bằng `@` syntax (e.g. `RTClib@^2.1.0`) — KHÔNG dùng `latest`. Đảm bảo `espressif32` platform version ≥ 6.x để có ESP-IDF v5 NVS API.<br>• **Security:** Không commit credential trong `platformio.ini`. WiFi creds chỉ nằm trong `config.h` với flag `#ifndef CONFIG_H_GUARD`. |
| **A2** | Khởi tạo `aeroponics-firmware/partitions.csv`: NVS 20KB (`0x5000`), otadata 8KB, app0 OTA_0 1.875MB, app1 OTA_1 1.875MB. | [ ] QA Review | • **Rule:** Tổng dung lượng phải ≤ flash size của board (4MB). Cột `SubType` phải đúng chuẩn ESP-IDF: `nvs`, `ota`, `ota_0`, `ota_1`.<br>• **Anti-Technical Debt:** Dành riêng slot OTA ngay từ đầu, dù Sprint 1 chưa dùng OTA — tránh phải flash lại partition table về sau.<br>• **Validation:** Sau khi tạo, chạy `pio run` để verify partition table hợp lệ, không bị cảnh báo overlap. |
| **A3** | Khởi tạo `aeroponics-firmware/include/config.h` — **Single Source of Truth** cho toàn bộ hằng số firmware: Pinout (`RELAY_PIN_1..4 = 1,2,3,4`; `RTC_SDA_PIN=21`; `RTC_SCL_PIN=22`), Schedule Defaults (`DEFAULT_SPRAY_DAY_S=30`, `DEFAULT_COOLDOWN_DAY_S=300`, `DEFAULT_SPRAY_NIGHT_S=30`, `DEFAULT_COOLDOWN_NIGHT_S=600`, `DAY_START_HOUR=6`, `NIGHT_START_HOUR=18`), FreeRTOS constants (`RELAY_TASK_STACK_SIZE=8192`, `RELAY_TASK_PRIORITY=3`, `RELAY_TASK_CORE=1`, `WDT_TIMEOUT_S=30`). | [ ] Pending | • **Design Pattern:** Single Responsibility, Configuration as Constants — TOÀN BỘ magic number phải nằm ở đây, KHÔNG được hardcode ở bất kỳ `.cpp` nào khác.<br>• **Anti-Technical Debt:** Dùng `#pragma once` thay `#ifndef` guards. Tất cả hằng số dùng `constexpr` thay `#define` để có type-safety.<br>• **Hardware Note:** `LED_STATUS_PIN` để comment `// TODO: confirm with hardware` — KHÔNG đặt giá trị ngẫu nhiên, tránh gây xung đột GPIO.<br>• **Naming:** SCREAMING_SNAKE_CASE cho tất cả constants (`RELAY_PIN_1`, không phải `relayPin1`). |

---

### TRACK B — NVS Driver

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **B1** | Khởi tạo `aeroponics-firmware/include/nvs_storage.h` — Khai báo struct `RelayProfile` (`uint32_t spray_day_s`, `cooldown_day_s`, `spray_night_s`, `cooldown_night_s`) và interface class `NvsStorage` với 5 public methods: `begin()`, `loadProfile()`, `saveProfile()`, `loadAllProfiles()`, `factoryReset()`. | [ ] Pending | • **Design Pattern:** Repository Pattern — `NvsStorage` là lớp duy nhất biết cách đọc/ghi NVS. KHÔNG module nào khác được gọi NVS API trực tiếp.<br>• **Anti-Technical Debt:** Header chỉ chứa interface (declaration). KHÔNG implementation trong `.h`. Dùng `#pragma once`.<br>• **Type Safety:** `relay_id` dùng `uint8_t`, range `[0, 3]`. Mọi function `bool` return type để caller kiểm tra lỗi bắt buộc. |
| **B2** | Implement `aeroponics-firmware/src/nvs_storage.cpp` — `begin()`: `nvs_flash_init()` → `nvs_open()`, handle `ERR_NO_FREE_PAGES` bằng erase + reinit. `loadProfile()`: `nvs_get_u32()` với fallback default + range validation. `saveProfile()`: validate → `nvs_set_u32` x4 → `nvs_commit()`. `factoryReset()`: `nvs_erase_all()`. | [ ] Pending | • **Rule S1-NVS-02 (CỨNG):** `saveProfile()` KHÔNG được gọi trong vòng lặp hoặc task timer — chỉ được gọi khi có thay đổi config thực sự từ lệnh ngoài.<br>• **Rule S1-NVS-03 (CỨNG):** Validate range bắt buộc TRƯỚC KHI dùng bất kỳ giá trị nào đọc từ NVS: `spray ∈ [5, 300]`, `cooldown ∈ [30, 7200]`. Giá trị ngoài range → dùng default, LOG WARNING.<br>• **Anti-Technical Debt:** Dùng ESP-IDF NVS API trực tiếp (`nvs_handle_t`), KHÔNG dùng Arduino `Preferences` library — để có full control error handling.<br>• **Error Logging:** Mọi `ESP_ERROR_CHECK_WITHOUT_ABORT` phải kèm `ESP_LOGW(TAG, ...)` với tag rõ ràng. |

---

### TRACK C — RTC Driver

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **C1** | Khởi tạo `aeroponics-firmware/include/rtc_manager.h` — Khai báo struct `SystemTime` (`uint8_t hour, minute, second; bool is_valid`) và class `RtcManager` với 4 methods: `begin()`, `syncFromNtp()`, `getTime()`, `isNightMode()`. | [ ] Pending | • **Design Pattern:** Adapter Pattern — `RtcManager` bọc `RTClib::DS3231` và hệ thống time của ESP-IDF. Caller không cần biết nguồn time là DS3231 hay NTP.<br>• **Anti-Technical Debt:** `SystemTime` là struct thuần (Plain Old Data), không kế thừa, không virtual — để tối ưu stack cho FreeRTOS task. |
| **C2** | Implement `aeroponics-firmware/src/rtc_manager.cpp` — `syncFromNtp()`: `configTime()` → poll `getLocalTime()` tối đa 10s → `rtc.adjust()`. `getTime()`: Priority 1 DS3231, Priority 2 system time, Priority 3 `is_valid=false`. `isNightMode()`: `hour >= 18 || hour < 6`, nếu `is_valid=false` → default về DAY mode. | [ ] Pending | • **Rule S1-RTC-04 (CỨNG):** `isNightMode()` PHẢI trả về `false` (DAY mode) khi `is_valid=false`. DAY mode là trạng thái fail-safe vì chu kỳ phun ngắn hơn, an toàn hơn cho cây khi không xác định được giờ.<br>• **Anti-Technical Debt:** NTP polling dùng `getLocalTime(&timeinfo, 10000)` với timeout, KHÔNG dùng `delay()` blocking. Log rõ nguồn time đang dùng mỗi khi `getTime()` được gọi lần đầu hoặc nguồn thay đổi.<br>• **Timezone:** Set `configTime(7*3600, 0, "pool.ntp.org")` — UTC+7. Hardcode timezone offset vào `config.h` dưới tên `TIMEZONE_OFFSET_S = 25200`. |

---

### TRACK D — Relay Controller

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **D1** | Khởi tạo `aeroponics-firmware/include/relay_controller.h` — Khai báo `enum RelayState { RELAY_OFF=0, RELAY_ON=1 }`, struct `RelayOverrideState { bool active; uint32_t remaining_s; RelayState forced_state; }`, và class `RelayController` với 6 methods: `initPins()`, `setRelay()`, `getRelayState()`, `startManualOverride()`, `cancelOverride()`, `isOverrideActive()`, `tickOverride()`. | [ ] Pending | • **Design Pattern:** Low-Level Hardware Abstraction (HAL) — `RelayController` là lớp duy nhất được phép gọi `digitalWrite()` cho relay pins. KHÔNG module nào khác gọi `digitalWrite` trực tiếp.<br>• **Anti-Technical Debt:** `RelayState` dùng `enum` (không `bool`) để code rõ ràng và tránh nhầm lẫn logic. Cache trạng thái relay trong array nội bộ để `getRelayState()` không phải `digitalRead()` mỗi lần. |
| **D2** | Implement `aeroponics-firmware/src/relay_controller.cpp` — `initPins()`: Với mỗi relay pin: `digitalWrite(pin, LOW)` → sau đó mới `pinMode(pin, OUTPUT)`. `setRelay()`: Map relay_id `[0-3]` → GPIO pin từ `config.h`. Cập nhật cache state. | [ ] Pending | • **Rule S1-HW-01 (TUYỆT ĐỐI, KHÔNG NGOẠI LỆ):** `initPins()` phải là lời gọi ĐẦU TIÊN trong `setup()` sau `Serial.begin()`. `digitalWrite(LOW)` BẮT BUỘC đứng TRƯỚC `pinMode(OUTPUT)` — đây là cơ chế duy nhất ngăn relay bị kích lúc boot (glitch). Bất kỳ Agent nào vi phạm rule này đều bị FAIL QA ngay lập tức.<br>• **Active HIGH Logic:** Relay logic là Active HIGH. `RELAY_ON` → `digitalWrite(pin, HIGH)`. `RELAY_OFF` → `digitalWrite(pin, LOW)`. Ghi rõ comment trên từng dòng.<br>• **Override Safety:** `startManualOverride()` validate `duration_s ∈ [1, 3600]`. Nếu ngoài range → reject, trả về `false`, LOG ERROR. |

---

### TRACK E — Schedule Manager

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **E1** | Khởi tạo `aeroponics-firmware/include/schedule_manager.h` — Khai báo `enum SchedulePhase { PHASE_SPRAYING, PHASE_COOLING_DOWN }`, struct `RelayRuntimeState { SchedulePhase phase; uint32_t phase_remaining_s; RelayProfile current_profile; bool is_night_mode; }`, và class `ScheduleManager` với 4 methods: `begin()` (inject dependencies), `startAllTasks()`, `updateProfile()`, `getRuntimeState()`. | [ ] Pending | • **Design Pattern:** Dependency Injection — `begin()` nhận con trỏ `NvsStorage*`, `RtcManager*`, `RelayController*`. KHÔNG dùng global instance hoặc singleton — để unit-testable và tránh hidden coupling.<br>• **Anti-Technical Debt:** `RelayRuntimeState` là read-only snapshot (trả về by value), không expose con trỏ vào internal state. Caller không thể mutate state từ bên ngoài. |
| **E2** | Implement `aeroponics-firmware/src/schedule_manager.cpp` — `startAllTasks()`: Tạo 4 `xTaskCreatePinnedToCore()` pinned CORE_1, stack 8192, priority 3. Task loop: Feed WDT → check override → get time → choose profile (day/night) → SPRAYING phase (relay ON, countdown) → COOLDOWN phase (relay OFF, countdown) → repeat. `updateProfile()`: Mutex-guarded RAM update + NVS save. | [ ] Pending | • **Rule S1-MUTEX-05 (CỨNG):** Mọi đọc/ghi `profiles_[]` array PHẢI qua `xSemaphoreTake(profile_mutex_, portMAX_DELAY)` / `xSemaphoreGive()`. KHÔNG được direct-access array từ nhiều tasks. Mutex phải được tạo trong `begin()` trước `startAllTasks()`.<br>• **Rule S1-WDT-06 (CỨNG):** `esp_task_wdt_reset()` phải là câu lệnh ĐẦU TIÊN trong mỗi iteration của task loop. KHÔNG có `vTaskDelay()` nào quá `WDT_TIMEOUT_S=30s` mà không feed WDT trước đó.<br>• **Countdown Granularity:** Mỗi tick của countdown là `vTaskDelay(pdMS_TO_TICKS(1000))` — 1 giây. Trong mỗi tick phải check override flag để có thể phản hồi nhanh khi user trigger manual override, không phải chờ hết phase.<br>• **Profile Hot-Reload:** Khi `updateProfile()` được gọi, task đang chạy phase hiện tại vẫn hoàn tất phase đó rồi mới áp dụng profile mới ở phase tiếp theo — tránh abort đột ngột giữa chu kỳ phun. |

---

### TRACK F — main.cpp Orchestrator

| Task ID | Mô tả Task | Status | Note (Chỉ thị kỹ thuật cấp cao — Senior Solution Architect) |
|---|---|---|---|
| **F1** | Implement `aeroponics-firmware/src/main.cpp` — `setup()` theo đúng thứ tự: 1. `Serial.begin(115200)`, 2. `relay_controller.initPins()`, 3. `nvs_storage.begin()`, 4. `nvs_storage.loadAllProfiles()`, 5. `Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN)`, 6. `rtc_manager.begin()`, 7. WiFi connect (30s timeout), 8. NTP sync nếu connected, 9. `schedule_manager.begin()` + `startAllTasks()`, 10. LOG "Boot Complete". `loop()`: feed WDT + Serial debug commands + WiFi reconnect check mỗi 60s. | [ ] Pending | • **Rule S1-HW-01 ENFORCEMENT (CỨNG):** Bước 2 (`relay_controller.initPins()`) phải là lệnh gọi HW đầu tiên sau Serial — KHÔNG được chen bất kỳ logic nào vào giữa.<br>• **WiFi Non-Blocking:** WiFi connect dùng `WiFi.begin()` + vòng lặp `while(!WiFi.isConnected() && millis() - start < 30000)` với `delay(500)` mỗi tick. KHÔNG dùng `WiFi.waitForConnectResult()` blocking vô thời hạn.<br>• **Error Resilience:** Mỗi bước init thất bại (NVS, RTC, WiFi) phải LOG WARNING và TIẾP TỤC với fallback — KHÔNG được `while(true)` halt. Hệ thống phải luôn cố gắng khởi động đến `startAllTasks()`.<br>• **Serial Debug Commands:** `loop()` parse Serial input: `"status"` → print relay states; `"override <id> <on|off> <seconds>"` → trigger `startManualOverride()`; `"factory"` → confirm prompt + `factoryReset()`.<br>• **Anti-Technical Debt:** `loop()` KHÔNG chứa bất kỳ blocking code nào. Mọi task phức tạp đã chạy trong FreeRTOS task riêng. `loop()` chỉ là lightweight maintenance. |

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
