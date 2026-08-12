# Sprint 1: Core Edge Engine & Hardware Fail-safe (Firmware)

> **⚠️ PROTOTYPE SCOPE — Đã hoàn thành, KHÔNG phải production architecture**
> Sprint 1 đã hoàn thành với 4 relay GPIO trực tiếp trên rig thí nghiệm. Boot-safe, RTC, NVS, WDT và state machine phun/cooldown vẫn là foundation quan trọng cho Sprint 2 production. Tuy nhiên:
> - `RelayController` và `RELAY_PIN_1–4` chỉ dùng cho rig prototype, KHÔNG đưa vào firmware production.
> - Production firmware (sau Sprint 1.5 PASS) sẽ dùng `RfTransport`, `PumpNodeController`, `GroupScheduler` theo [`sprint_2.md`](./sprint_2.md).
> - **QA gate và go/no-go:** [`sprint_1_5.md`](./sprint_1_5.md).

> **Phụ thuộc:** Không có (Sprint firmware đầu tiên, Bottom-Up).  
> **Output bàn giao:** Firmware ESP32-S3 biên dịch được, khởi động an toàn (không glitch relay), đọc/ghi NVS, đồng bộ RTC, vận hành state machine phun/cooldown Ngày/Đêm cho 4 relay độc lập — **hoàn toàn offline, không cần MQTT**.

> **Điều chỉnh 2026-08-10:** Boot-safe/RTC/NVS/WDT vẫn giữ nguyên, nhưng production phải phục vụ 4 timer group fan-out tới 12 node RF, không phải 4 relay GPIO trực tiếp. Xem [PROJECT_ALIGNMENT_2026-08-10.md](./PROJECT_ALIGNMENT_2026-08-10.md).

---

## 1. PHẠM VI & MỤC TIÊU

### 1.1 Modules / Components

| Module | Mô tả |
|---|---|
| **`config.h`** | Định nghĩa tập trung pinout, hằng số phần cứng, thông số mặc định |
| **`nvs_storage.h/cpp`** | Driver đọc/ghi NVS, bảo vệ flash khỏi ghi thừa |
| **`rtc_manager.h/cpp`** | Driver DS3231 I2C, đồng bộ NTP↔RTC, cung cấp thời gian tin cậy |
| **`relay_controller.h/cpp`** | Điều khiển mức thấp: bật/tắt relay an toàn, manual override |
| **`schedule_manager.h/cpp`** | State machine FreeRTOS: phân biệt Ngày/Đêm, Phun/Cooldown 4 relay |
| **`main.cpp`** | Orchestrator: khởi động đúng thứ tự an toàn, tạo FreeRTOS tasks |
| **`platformio.ini`** | Cấu hình build: board, framework, libraries, partition table |
| **`partitions.csv`** | Custom partition: NVS 20KB, OTA, app |

### 1.2 Mục tiêu cụ thể Sprint 1

- [ ] GPIO relay được set `LOW` trước `pinMode(OUTPUT)` → không bao giờ glitch kích relay lúc boot.
- [ ] NVS ghi/đọc thành công profile cấu hình (spray_day, cooldown_day, spray_night, cooldown_night) cho 4 relay.
- [ ] RTC DS3231 đồng bộ từ NTP khi có Wi-Fi, fallback đọc DS3231 khi không có Wi-Fi.
- [ ] State machine Phun/Cooldown Ngày/Đêm hoạt động chính xác theo đồng hồ thực.
- [ ] Manual override tạm thời ngắt auto-timer, sau khi hết override tự tiếp tục auto.
- [ ] Firmware biên dịch không lỗi với `pio run`.

---

## 2. KIẾN TRÚC & LUỒNG DỮ LIỆU

### 2.1 Sơ đồ Dependency của các Module Firmware

```
main.cpp
  │
  ├──▶ config.h           (Pure header: Pinout, defaults — không phụ thuộc ai)
  │
  ├──▶ nvs_storage.h/cpp  (Phụ thuộc: ESP-IDF NVS API, config.h)
  │       └── Cung cấp: load/save RelayProfile struct
  │
  ├──▶ rtc_manager.h/cpp  (Phụ thuộc: RTClib, Wire, configTime, nvs_storage)
  │       └── Cung cấp: getReliableTime() → struct tm
  │
  ├──▶ relay_controller.h/cpp  (Phụ thuộc: config.h)
  │       └── Cung cấp: setRelay(id, state), startManualOverride(id, duration_s)
  │
  └──▶ schedule_manager.h/cpp  (Phụ thuộc: rtc_manager, relay_controller, nvs_storage)
          └── Cung cấp: 4x FreeRTOS Task (1 task / relay)
```

### 2.2 Luồng Boot An Toàn (Safe Boot Sequence)

```
[Power ON / Reset]
        │
        ▼
[Step 1] Serial.begin(115200) — trước mọi thứ để có log
        │
        ▼
[Step 2] HARDWARE SAFE INIT:
         Với mỗi Relay Pin (RELAY_PIN_1..4):
           - digitalWrite(pin, LOW)   ← BẮT BUỘC: set LOW TRƯỚC
           - pinMode(pin, OUTPUT)     ← Sau đó mới set OUTPUT
         → Relay chắc chắn ở trạng thái OFF ngay từ đầu
        │
        ▼
[Step 3] NVS Init: nvs_storage.begin()
         → Nếu fail: load default config vào RAM, log ERROR, tiếp tục
        │
        ▼
[Step 4] Load Profile từ NVS vào RAM: nvs_storage.loadAllProfiles()
         → Nếu fail: dùng default (spray_day=30s, cooldown_day=300s, v.v.)
        │
        ▼
[Step 5] I2C Init: Wire.begin(SDA_PIN, SCL_PIN)
         RTC Init: rtc_manager.begin()
        │
        ▼
[Step 6] Wi-Fi Connect (non-blocking, 30s timeout)
         → Nếu kết nối: đồng bộ NTP → cập nhật DS3231
         → Nếu timeout: log WARNING, dùng giờ từ DS3231 (fallback)
        │
        ▼
[Step 7] Tạo 4 FreeRTOS Task (1 task / relay):
         xTaskCreatePinnedToCore(relayScheduleTask_N, ..., CORE_1)
        │
        ▼
[Step 8] loop() chỉ chạy Watchdog feed + Serial command handler
```

### 2.3 Luồng State Machine của Mỗi Relay Task

```
[FreeRTOS Task: relayScheduleTask_N]

LOOP vĩnh viễn:
  1. Feed Watchdog
  2. Kiểm tra Override Flag → nếu active: tick countdown, delay 1s, continue
  3. Lấy giờ → xác định DAY/NIGHT mode
  4. Đọc profile từ RAM (thread-safe qua mutex)
  5. PHASE SPRAYING:
       setRelay(ON)
       Đếm ngược spray_s giây (1s/tick, check override mỗi tick)
  6. PHASE COOLING_DOWN:
       setRelay(OFF)
       Đếm ngược cool_s giây (1s/tick, check override mỗi tick)
  7. GOTO LOOP
```

---

## 3. PHÂN RÃ CHI TIẾT TÁC VỤ

### TRACK A — Cấu hình & Build System

#### Task A-1: `platformio.ini`
- board = esp32-s3-devkitc-1
- framework = arduino
- monitor_speed = 115200
- lib_deps: RTClib, PubSubClient, ArduinoJson
- build_flags: -DCORE_DEBUG_LEVEL=3
- board_build.partitions = partitions.csv

#### Task A-2: `partitions.csv`
| Partition | Type | Size |
|---|---|---|
| nvs | data/nvs | 20KB (0x5000) |
| otadata | data/ota | 8KB |
| app0 | app/ota_0 | 1.875MB |
| app1 | app/ota_1 | 1.875MB |

#### Task A-3: `config.h` — Single Source of Truth

**Pinout:**
```cpp
RELAY_PIN_1 = 1, RELAY_PIN_2 = 2, RELAY_PIN_3 = 3, RELAY_PIN_4 = 4
RTC_SDA_PIN = 21, RTC_SCL_PIN = 22
LED_STATUS_PIN = (xác nhận với hardware thực tế)
```

**Schedule Defaults:**
```cpp
DEFAULT_SPRAY_DAY_S    = 30
DEFAULT_COOLDOWN_DAY_S = 300
DEFAULT_SPRAY_NIGHT_S  = 30
DEFAULT_COOLDOWN_NIGHT_S = 600
DAY_START_HOUR  = 6
NIGHT_START_HOUR = 18
```

**FreeRTOS:**
```cpp
RELAY_TASK_STACK_SIZE = 8192
RELAY_TASK_PRIORITY   = 3
RELAY_TASK_CORE       = 1
WDT_TIMEOUT_S         = 30
```

---

### TRACK B — NVS Driver

#### Task B-1: `nvs_storage.h`

**Struct:**
```cpp
struct RelayProfile {
  uint32_t spray_day_s;
  uint32_t cooldown_day_s;
  uint32_t spray_night_s;
  uint32_t cooldown_night_s;
};
```

**Interface:**
| Hàm | Signature | Mô tả |
|---|---|---|
| `begin` | `bool begin()` | Mở NVS namespace |
| `loadProfile` | `bool loadProfile(uint8_t relay_id, RelayProfile& out)` | Đọc, nếu không có → default |
| `saveProfile` | `bool saveProfile(uint8_t relay_id, const RelayProfile& p)` | Ghi khi có thay đổi |
| `loadAllProfiles` | `bool loadAllProfiles(RelayProfile profiles[4])` | Đọc 4 profiles 1 lần |
| `factoryReset` | `bool factoryReset()` | Xóa toàn bộ NVS namespace |

#### Task B-2: `nvs_storage.cpp`

- `begin()`: `nvs_flash_init()` → `nvs_open()`. Nếu `ERR_NO_FREE_PAGES` → erase + init lại.
- `loadProfile()`: `nvs_get_u32()`. Nếu `NOT_FOUND` → dùng default. Validate range [5,300] cho spray, [30,7200] cho cooldown.
- `saveProfile()`: validate → `nvs_set_u32` x4 → `nvs_commit`.

---

### TRACK C — RTC Driver

#### Task C-1: `rtc_manager.h`

```cpp
struct SystemTime {
  uint8_t hour, minute, second;
  bool    is_valid;
};
```

| Hàm | Signature |
|---|---|
| `begin` | `bool begin()` |
| `syncFromNtp` | `bool syncFromNtp()` |
| `getTime` | `SystemTime getTime()` |
| `isNightMode` | `bool isNightMode()` |

#### Task C-2: `rtc_manager.cpp`

- `syncFromNtp()`: `configTime()` → poll `getLocalTime()` tối đa 10s → `rtc.adjust()`.
- `getTime()`: Priority 1: DS3231; Priority 2: system time (NTP synced); Priority 3: `is_valid = false`.
- `isNightMode()`: `hour >= 18 || hour < 6`. Nếu `is_valid = false` → default DAY (safer).

---

### TRACK D — Relay Controller

#### Task D-1: `relay_controller.h`

```cpp
enum RelayState { RELAY_OFF = 0, RELAY_ON = 1 };
struct RelayOverrideState {
  bool active;
  uint32_t remaining_s;
  RelayState forced_state;
};
```

| Hàm | Signature |
|---|---|
| `initPins` | `void initPins()` — LOW trước, OUTPUT sau |
| `setRelay` | `void setRelay(uint8_t id, RelayState state)` |
| `getRelayState` | `RelayState getRelayState(uint8_t id)` |
| `startManualOverride` | `bool startManualOverride(uint8_t id, RelayState state, uint32_t duration_s)` |
| `cancelOverride` | `void cancelOverride(uint8_t id)` |
| `isOverrideActive` | `bool isOverrideActive(uint8_t id)` |
| `tickOverride` | `void tickOverride(uint8_t id)` — giảm remaining_s, tự hủy khi = 0 |

#### Task D-2: `relay_controller.cpp`

- `initPins()`: **GỌI ĐẦU TIÊN TRONG SETUP** — `digitalWrite(pin, LOW)` trước `pinMode(pin, OUTPUT)`.
- `setRelay()`: Map relay_id → GPIO. `HIGH` = ON cho Active HIGH. Cập nhật cache.

---

### TRACK E — Schedule Manager

#### Task E-1: `schedule_manager.h`

```cpp
enum SchedulePhase { PHASE_SPRAYING, PHASE_COOLING_DOWN };
struct RelayRuntimeState {
  SchedulePhase phase;
  uint32_t phase_remaining_s;
  RelayProfile current_profile;
  bool is_night_mode;
};
```

| Hàm | Mô tả |
|---|---|
| `begin` | Inject NvsStorage*, RtcManager*, RelayController* |
| `startAllTasks` | Tạo 4 FreeRTOS tasks, pin Core 1 |
| `updateProfile` | Thread-safe (mutex): cập nhật RAM + ghi NVS |
| `getRuntimeState` | Trả về state hiện tại (cho MQTT publish) |

#### Task E-2: `schedule_manager.cpp`

- `updateProfile()`: `xSemaphoreTake(mutex)` → update RAM → `xSemaphoreGive()` → `nvs->saveProfile()`.
- Task function: Feed WDT → check override → get time → choose profile → SPRAYING loop → COOLDOWN loop → repeat.

---

### TRACK F — main.cpp

**`setup()` — Thứ tự bắt buộc:**
```
1. Serial.begin(115200)
2. relay_controller.initPins()  ← FIRST ALWAYS
3. nvs_storage.begin()
4. nvs_storage.loadAllProfiles()
5. Wire.begin(RTC_SDA, RTC_SCL)
6. rtc_manager.begin()
7. WiFi.begin() → chờ 30s
8. IF connected: rtc_manager.syncFromNtp()
9. schedule_manager.begin() + loadProfiles() + startAllTasks()
10. LOG "Boot Complete"
```

**`loop()` — Chỉ 3 việc:**
1. `esp_task_wdt_reset()`
2. Serial debug commands (status / override / factory)
3. WiFi reconnect check mỗi 60s

---

## 4. TIÊU CHUẨN RÀ SOÁT CỨNG (Sprint 1)

### Rule S1-HW-01: Boot GPIO Safety Order
```
PASS: digitalWrite(LOW) đứng TRƯỚC pinMode(OUTPUT) cho mọi relay pin
FAIL: initPins() được gọi sau bất kỳ logic nào khác trong setup()
```

### Rule S1-NVS-02: Strict NVS Write Policy
```
PASS: saveProfile() chỉ được gọi từ updateProfile() khi có thay đổi từ MQTT
FAIL: NVS write trong vòng lặp, task delay, hoặc định kỳ
```

### Rule S1-NVS-03: NVS Value Range Validation
```
Spray range: [5s, 300s]
Cooldown range: [30s, 7200s]
FAIL: Dùng giá trị NVS raw không validated
```

### Rule S1-RTC-04: Time Source Fallback
```
PASS: Khi DS3231 không khả dụng → log WARNING + default về DAY mode
FAIL: Trả về is_valid=false mà không log, không default
```

### Rule S1-MUTEX-05: Thread-Safe Profile Access
```
PASS: Mọi đọc/ghi profiles_[] qua profile_mutex_
FAIL: Direct access profiles_[] từ nhiều tasks
```

### Rule S1-WDT-06: Watchdog Feed
```
PASS: esp_task_wdt_reset() trong MỖI iteration của task loop
FAIL: Bất kỳ delay > WDT_TIMEOUT_S mà không feed WDT
```

---

*Sprint 1 Planning — direct-relay là prototype; production RF-node requirements cập nhật 2026-08-10.*
