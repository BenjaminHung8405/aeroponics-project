# Sprint 1: Core Edge Engine & Hardware Fail-safe (Firmware)

> **Phụ thuộc:** Không có (Sprint đầu tiên, Bottom-Up)
> **Output bàn giao:** Firmware ESP32-S3 có thể biên dịch, khởi động an toàn (không glitch relay), đọc/ghi NVS, đồng bộ RTC, và vận hành state machine phun/cooldown Ngày/Đêm cho 4 relay độc lập — **hoàn toàn offline, không cần MQTT**.

---

## 1. PHẠM VI & MỤC TIÊU

### 1.1 Modules / Components bị tác động trực tiếp

| Module | Mô tả |
|---|---|
| **`config.h`** | Định nghĩa tập trung toàn bộ pinout, hằng số phần cứng, thông số mặc định |
| **`nvs_storage.h/cpp`** | Driver đọc/ghi NVS, bảo vệ flash khỏi ghi thừa (wear levelling) |
| **`rtc_manager.h/cpp`** | Driver DS3231 I2C, đồng bộ NTP↔RTC, cung cấp thời gian hệ thống tin cậy |
| **`relay_controller.h/cpp`** | Điều khiển mức thấp: bật/tắt relay an toàn, manual override, calibration flush |
| **`schedule_manager.h/cpp`** | State machine FreeRTOS: phân biệt Ngày/Đêm, Phun/Cooldown cho 4 relay |
| **`main.cpp`** | Orchestrator: khởi động theo đúng thứ tự an toàn, tạo FreeRTOS tasks |
| **`platformio.ini`** | Cấu hình build: board, framework, libraries, partition table |
| **`partitions.csv`** | Custom partition: NVS 20KB, OTA, app |

### 1.2 Mục tiêu cụ thể của Sprint 1

- [ ] GPIO relay được set `LOW` trước khi `pinMode(OUTPUT)` → không bao giờ glitch kích relay lúc boot.
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
[Step 1] Khởi tạo Serial (115200 baud) — trước mọi thứ để có log
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
         → Kiểm tra DS3231 có phản hồi không
        │
        ▼
[Step 6] Wi-Fi Connect (non-blocking, 30s timeout)
         → Nếu kết nối thành công: đồng bộ NTP → cập nhật DS3231
         → Nếu timeout: log WARNING, dùng giờ từ DS3231 (fallback)
        │
        ▼
[Step 7] Tạo 4 FreeRTOS Task (1 task / relay):
         xTaskCreatePinnedToCore(relayScheduleTask_1, ..., CORE_1)
         xTaskCreatePinnedToCore(relayScheduleTask_2, ..., CORE_1)
         xTaskCreatePinnedToCore(relayScheduleTask_3, ..., CORE_1)
         xTaskCreatePinnedToCore(relayScheduleTask_4, ..., CORE_1)
        │
        ▼
[Step 8] loop() chỉ chạy Watchdog feed + Serial command handler (debug)
```

### 2.3 Luồng State Machine của Mỗi Relay Task

```
[FreeRTOS Task: relayScheduleTask_N]
        │
        ▼
┌──────────────────────────────────────────────┐
│  Kiểm tra Override Flag                      │
│  (relay_controller.isOverrideActive(id))     │
└──────────────────────────────────────────────┘
        │
  [Override Active?]
   YES ──▶ Giữ trạng thái manual, vDelay(1000ms), lặp lại
   NO  ──▶
        │
        ▼
┌──────────────────────────────────────────────┐
│  Lấy giờ hiện tại: rtc_manager.getTime()    │
│  Xác định mode: isNightMode(hour)            │
│    → NIGHT: 18:00 – 05:59 (UTC+7)           │
│    → DAY:   06:00 – 17:59 (UTC+7)           │
└──────────────────────────────────────────────┘
        │
        ▼
┌──────────────────────────────────────────────┐
│  Đọc Profile từ RAM:                        │
│    DAY mode:   spray_s = profile.spray_day   │
│                cool_s  = profile.cooldown_day│
│    NIGHT mode: spray_s = profile.spray_night │
│                cool_s  = profile.cooldown_night│
└──────────────────────────────────────────────┘
        │
        ▼
┌──────────────────────────────────────────────┐
│  STATE: SPRAYING                             │
│  relay_controller.setRelay(id, ON)           │
│  Đếm ngược spray_s giây (vDelay 1s x spray_s)│
│  Trong mỗi vDelay: kiểm tra Override Flag    │
└──────────────────────────────────────────────┘
        │
        ▼
┌──────────────────────────────────────────────┐
│  STATE: COOLING_DOWN                         │
│  relay_controller.setRelay(id, OFF)          │
│  Đếm ngược cool_s giây (vDelay 1s x cool_s) │
│  Trong mỗi vDelay: kiểm tra Override Flag    │
└──────────────────────────────────────────────┘
        │
        └──────────────── Lặp lại từ đầu ────────────────────
```

### 2.4 Luồng Đọc/Ghi NVS

```
[MQTT Command nhận được / UI action]  ← (Sprint 2+)
        │
        ▼
schedule_manager.updateProfile(relay_id, new_profile)
        │
        ├──▶ Cập nhật ngay vào RAM (immediate effect)
        │
        └──▶ nvs_storage.saveProfile(relay_id, new_profile)
                    │
                    ▼
             [NVS Namespace: "aeroponics"]
             Key: "r1_spray_d", "r1_cool_d", "r1_spray_n", "r1_cool_n"
             Key: "r2_spray_d", ...  (tương tự cho relay 2,3,4)
```

---

## 3. PHÂN RÃ CHI TIẾT TÁC VỤ

### TRACK A — Tầng Cấu Hình & Build System

---

#### Task A-1: Tạo file cấu hình PlatformIO
**File tạo mới:** `aeroponics-firmware/platformio.ini`

```ini
; Nội dung mô tả (không phải code thực):
; - board = esp32-s3-devkitc-1
; - framework = arduino
; - monitor_speed = 115200
; - upload_speed = 921600
; - lib_deps: RTClib, PubSubClient, ArduinoJson, ESP32 Arduino core
; - build_flags: -DCORE_DEBUG_LEVEL=3 (DEBUG log)
; - extra_scripts: scripts/pre_build.py (optional: inject git hash)
; - partition_table_offset pointing to partitions.csv
```

**Hàm/Logic cần triển khai:**
- Cấu hình `board_build.partitions = partitions.csv` để dùng custom partition.
- `build_flags = -DARDUINO_USB_CDC_ON_BOOT=1` để CDC Serial hoạt động.

---

#### Task A-2: Tạo Custom Partition Table
**File tạo mới:** `aeroponics-firmware/partitions.csv`

| Partition | Type | Subtype | Offset | Size | Mục đích |
|---|---|---|---|---|---|
| `nvs` | data | nvs | 0x9000 | 0x5000 | NVS Storage (20KB) |
| `otadata` | data | ota | 0xe000 | 0x2000 | OTA metadata |
| `app0` | app | ota_0 | 0x10000 | 0x1E0000 | App slot 0 (1.875MB) |
| `app1` | app | ota_1 | 0x1F0000 | 0x1E0000 | App slot 1 (1.875MB) |
| `spiffs` | data | spiffs | 0x3D0000 | 0x30000 | SPIFFS (192KB, optional) |

---

#### Task A-3: Tạo file `config.h` — Single Source of Truth cho Hardware
**File tạo mới:** `aeroponics-firmware/include/config.h`

**Hàm/Hằng số cần định nghĩa:**

```
// Pinout
RELAY_PIN_1, RELAY_PIN_2, RELAY_PIN_3, RELAY_PIN_4
RTC_SDA_PIN, RTC_SCL_PIN
LED_STATUS_PIN

// Relay Logic
RELAY_ACTIVE_HIGH = true
RELAY_ON_SIGNAL = HIGH   // Khi RELAY_ACTIVE_HIGH = true
RELAY_OFF_SIGNAL = LOW

// NVS Keys
NVS_NAMESPACE = "aeroponics"
NVS_KEY_R1_SPRAY_DAY = "r1_spray_d"    // uint32_t, đơn vị giây
NVS_KEY_R1_COOL_DAY  = "r1_cool_d"
NVS_KEY_R1_SPRAY_NIGHT = "r1_spray_n"
NVS_KEY_R1_COOL_NIGHT  = "r1_cool_n"
// ... tương tự r2, r3, r4

// Default Schedule Values
DEFAULT_SPRAY_DAY_S   = 30      // 30 giây
DEFAULT_COOLDOWN_DAY_S = 300    // 5 phút
DEFAULT_SPRAY_NIGHT_S  = 30     // 30 giây
DEFAULT_COOLDOWN_NIGHT_S = 600  // 10 phút

// Day/Night Boundary (giờ UTC+7)
DAY_START_HOUR = 6    // 06:00
NIGHT_START_HOUR = 18 // 18:00

// NTP Configuration
NTP_SERVER_1 = "pool.ntp.org"
NTP_SERVER_2 = "time.google.com"
TIMEZONE_OFFSET_SEC = 25200   // UTC+7

// FreeRTOS Config
RELAY_TASK_STACK_SIZE = 8192
RELAY_TASK_PRIORITY   = 3
RELAY_TASK_CORE       = 1

// WiFi
WIFI_CONNECT_TIMEOUT_MS = 30000
WIFI_RECONNECT_INTERVAL_MS = 60000

// Watchdog
WDT_TIMEOUT_S = 30
```

---

### TRACK B — Tầng Dữ Liệu (NVS Driver)

---

#### Task B-1: Định nghĩa Interface `NvsStorage` trong Header
**File tạo mới:** `aeroponics-firmware/include/nvs_storage.h`

**Struct cần định nghĩa:**
```cpp
struct RelayProfile {
  uint32_t spray_day_s;       // Thời gian phun ban ngày (giây)
  uint32_t cooldown_day_s;    // Thời gian nghỉ ban ngày (giây)
  uint32_t spray_night_s;     // Thời gian phun ban đêm (giây)
  uint32_t cooldown_night_s;  // Thời gian nghỉ ban đêm (giây)
};
```

**Class `NvsStorage` — Public Interface:**

| Hàm | Signature | Mô tả |
|---|---|---|
| `begin` | `bool begin()` | Mở NVS namespace, trả về false nếu fail |
| `loadProfile` | `bool loadProfile(uint8_t relay_id, RelayProfile& out_profile)` | Đọc profile 1 relay từ NVS vào out_profile. Nếu key không tồn tại, điền default. |
| `saveProfile` | `bool saveProfile(uint8_t relay_id, const RelayProfile& profile)` | Ghi profile vào NVS. Chỉ gọi hàm này khi có thay đổi từ user. |
| `loadAllProfiles` | `bool loadAllProfiles(RelayProfile profiles[4])` | Đọc tất cả 4 profile 1 lần |
| `factoryReset` | `bool factoryReset()` | Xóa toàn bộ NVS namespace, điền lại default |
| `close` | `void close()` | Đóng NVS handle |

---

#### Task B-2: Triển khai `NvsStorage` trong Source
**File tạo mới:** `aeroponics-firmware/src/nvs_storage.cpp`

**Logic chi tiết của từng hàm:**

- **`begin()`:**
  - Gọi `nvs_flash_init()`.
  - Gọi `nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle)`.
  - Nếu `ESP_ERR_NVS_NO_FREE_PAGES` hoặc `ESP_ERR_NVS_NEW_VERSION_FOUND` → gọi `nvs_flash_erase()` rồi `nvs_flash_init()` lại.
  - Return `true` nếu thành công.

- **`loadProfile(relay_id, out_profile)`:**
  - Tạo key string dựa trên relay_id (vd: `r1_spray_d`).
  - Gọi `nvs_get_u32(handle, key, &value)` cho từng field.
  - Nếu `ESP_ERR_NVS_NOT_FOUND` → set giá trị DEFAULT tương ứng (không phải error).
  - Validate range: `spray_s` phải trong [5, 300], `cooldown_s` trong [30, 3600].
  - Nếu giá trị out of range → reset về default, log WARNING.

- **`saveProfile(relay_id, profile)`:**
  - Validate input range trước khi ghi (như loadProfile).
  - Gọi `nvs_set_u32` cho từng field.
  - Gọi `nvs_commit` sau tất cả set.
  - Return `false` và log ERROR nếu bất kỳ step nào fail.

- **`factoryReset()`:**
  - Gọi `nvs_erase_all(handle)` rồi `nvs_commit`.
  - Gọi `loadAllProfiles` để nạp lại default vào RAM (relay_controller sẽ pick up).

---

### TRACK C — Tầng Dịch Vụ Thời Gian (RTC Driver)

---

#### Task C-1: Định nghĩa Interface `RtcManager` trong Header
**File tạo mới:** `aeroponics-firmware/include/rtc_manager.h`

**Struct cần định nghĩa:**
```cpp
struct SystemTime {
  uint16_t year;
  uint8_t  month;    // 1-12
  uint8_t  day;      // 1-31
  uint8_t  hour;     // 0-23 (UTC+7)
  uint8_t  minute;   // 0-59
  uint8_t  second;   // 0-59
  bool     is_valid; // false nếu DS3231 chưa set hoặc lost power
};
```

**Class `RtcManager` — Public Interface:**

| Hàm | Signature | Mô tả |
|---|---|---|
| `begin` | `bool begin()` | Khởi tạo I2C, kiểm tra DS3231 có phản hồi không |
| `syncFromNtp` | `bool syncFromNtp()` | Lấy giờ từ NTP server, cập nhật DS3231 và system time |
| `getTime` | `SystemTime getTime()` | Trả về giờ từ DS3231 (ưu tiên) hoặc system time |
| `isNightMode` | `bool isNightMode()` | Trả về true nếu giờ hiện tại trong khoảng đêm |
| `isRtcValid` | `bool isRtcValid()` | Kiểm tra DS3231 có pin, không bị reset |
| `setTimeManually` | `bool setTimeManually(const SystemTime& t)` | Cho phép set giờ thủ công (debug/provisioning) |

---

#### Task C-2: Triển khai `RtcManager` trong Source
**File tạo mới:** `aeroponics-firmware/src/rtc_manager.cpp`

**Logic chi tiết của từng hàm:**

- **`begin()`:**
  - Gọi `Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN)`.
  - Khởi tạo `RTC_DS3231 rtc` (RTClib).
  - Gọi `rtc.begin()` — trả về false nếu DS3231 không tìm thấy trên I2C bus.
  - Kiểm tra `rtc.lostPower()`: nếu true → log WARNING "DS3231 lost power, time may be invalid".
  - Lưu trạng thái `rtc_available_` vào member variable.

- **`syncFromNtp()`:**
  - ĐIỀU KIỆN: Chỉ gọi khi WiFi đã connected.
  - Gọi `configTime(TIMEZONE_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2)`.
  - Chờ tối đa 10s với polling `getLocalTime(&timeinfo)` mỗi 500ms.
  - Nếu thành công: gọi `rtc.adjust(DateTime(timeinfo))` để cập nhật DS3231 hardware.
  - Log INFO: "NTP sync OK, DS3231 updated".
  - Nếu timeout: log WARNING "NTP sync failed, using DS3231".
  - Return bool thành công.

- **`getTime()`:**
  - Priority 1: Nếu `rtc_available_ && !rtc.lostPower()` → đọc từ DS3231 (chính xác nhất, độc lập Wi-Fi).
  - Priority 2: Nếu NTP đã sync → dùng `getLocalTime(&timeinfo)`.
  - Priority 3: Fallback → trả về SystemTime với `is_valid = false`, log ERROR.

- **`isNightMode()`:**
  - Gọi `getTime()`.
  - Trả về `true` nếu `hour >= NIGHT_START_HOUR || hour < DAY_START_HOUR`.
  - Nếu `is_valid == false` → default về DAY mode (safer cho rễ).

---

### TRACK D — Tầng Điều Khiển Phần Cứng (Relay Controller)

---

#### Task D-1: Định nghĩa Interface `RelayController` trong Header
**File tạo mới:** `aeroponics-firmware/include/relay_controller.h`

**Enum cần định nghĩa:**
```cpp
enum RelayState { RELAY_OFF = 0, RELAY_ON = 1 };
enum OverrideType { NO_OVERRIDE, MANUAL_ON, CALIBRATION_FLUSH };
```

**Struct cần định nghĩa:**
```cpp
struct RelayOverrideState {
  bool active;
  OverrideType type;
  uint32_t remaining_s;   // Giây còn lại của override (RAM only, không ghi NVS)
  RelayState forced_state;
};
```

**Class `RelayController` — Public Interface:**

| Hàm | Signature | Mô tả |
|---|---|---|
| `initPins` | `void initPins()` | Set LOW trước, sau đó pinMode OUTPUT — gọi TRƯỚC mọi thứ |
| `setRelay` | `void setRelay(uint8_t relay_id, RelayState state)` | Bật/tắt relay vật lý |
| `getRelayState` | `RelayState getRelayState(uint8_t relay_id)` | Đọc trạng thái hiện tại (từ cache, không đọc GPIO) |
| `startManualOverride` | `bool startManualOverride(uint8_t relay_id, RelayState state, uint32_t duration_s)` | Tạm ngắt auto-scheduler, giữ relay ở state trong duration_s |
| `startCalibrationFlush` | `bool startCalibrationFlush(uint8_t relay_id, uint32_t flush_s)` | Bật relay liên tục để vệ sinh béc phun |
| `cancelOverride` | `void cancelOverride(uint8_t relay_id)` | Hủy override, trả control cho scheduler |
| `isOverrideActive` | `bool isOverrideActive(uint8_t relay_id)` | Kiểm tra relay có đang trong override không |
| `getOverrideState` | `RelayOverrideState getOverrideState(uint8_t relay_id)` | Trả về chi tiết override (cho MQTT publish) |
| `tickOverride` | `void tickOverride(uint8_t relay_id)` | Giảm remaining_s, tự hủy khi = 0. Gọi mỗi 1s bởi task |

---

#### Task D-2: Triển khai `RelayController` trong Source
**File tạo mới:** `aeroponics-firmware/src/relay_controller.cpp`

**Logic chi tiết:**

- **`initPins()`:** (GỌI ĐẦU TIÊN TRONG SETUP)
  ```
  for relay_pin in [RELAY_PIN_1..4]:
    digitalWrite(relay_pin, RELAY_OFF_SIGNAL)  // LOW = OFF cho Active HIGH
    pinMode(relay_pin, OUTPUT)
  ```
  → Đây là biện pháp chống glitch boot cứng nhắc nhất.

- **`setRelay(relay_id, state)`:**
  - Map relay_id (1-4) sang GPIO pin từ config.h.
  - Nếu `RELAY_ACTIVE_HIGH`:
    - `RELAY_ON` → `digitalWrite(pin, HIGH)`
    - `RELAY_OFF` → `digitalWrite(pin, LOW)`
  - Cập nhật `relay_state_cache_[relay_id]`.
  - Log: "Relay N set to ON/OFF".

- **`startManualOverride(relay_id, state, duration_s)`:**
  - Set `override_states_[relay_id].active = true`.
  - Set `override_states_[relay_id].remaining_s = duration_s`.
  - Gọi `setRelay(relay_id, state)` ngay lập tức.
  - Return `false` nếu relay_id không hợp lệ.

- **`tickOverride(relay_id)`:**
  - Nếu `override_states_[relay_id].active && remaining_s > 0` → decrement.
  - Nếu `remaining_s == 0` → gọi `cancelOverride(relay_id)`.

---

### TRACK E — Tầng Nghiệp Vụ (Schedule Manager)

---

#### Task E-1: Định nghĩa Interface `ScheduleManager` trong Header
**File tạo mới:** `aeroponics-firmware/include/schedule_manager.h`

**Struct trạng thái Runtime (chỉ RAM, không NVS):**
```cpp
enum SchedulePhase { PHASE_SPRAYING, PHASE_COOLING_DOWN, PHASE_STOPPED };

struct RelayRuntimeState {
  SchedulePhase phase;
  uint32_t phase_remaining_s;   // RAM only
  RelayProfile current_profile; // Bản sao từ NVS khi boot, cập nhật từ MQTT
  bool is_night_mode;
};
```

**Class `ScheduleManager` — Public Interface:**

| Hàm | Signature | Mô tả |
|---|---|---|
| `begin` | `void begin(NvsStorage* nvs, RtcManager* rtc, RelayController* relay)` | Inject dependencies |
| `startAllTasks` | `void startAllTasks()` | Tạo 4 FreeRTOS tasks, pin to Core 1 |
| `updateProfile` | `bool updateProfile(uint8_t relay_id, const RelayProfile& profile)` | Cập nhật RAM ngay + lệnh ghi NVS. Hiệu lực ngay sau chu kỳ hiện tại. |
| `getProfile` | `RelayProfile getProfile(uint8_t relay_id)` | Đọc profile hiện tại từ RAM |
| `getRuntimeState` | `RelayRuntimeState getRuntimeState(uint8_t relay_id)` | Trả về phase, remaining_s (cho MQTT publish / UI) |
| `forceRestartCycle` | `void forceRestartCycle(uint8_t relay_id)` | Restart về đầu chu kỳ SPRAY ngay lập tức |
| `stopAll` | `void stopAll()` | Dừng tất cả relay (emergency stop) |

---

#### Task E-2: Triển khai 4 FreeRTOS Tasks trong Source
**File tạo mới:** `aeroponics-firmware/src/schedule_manager.cpp`

**Logic chi tiết của `relayScheduleTask(void* param)`:**

```
[FreeRTOS Task Function — 1 instance per relay]

Input param: relay_id (cast từ void*)

LOOP vĩnh viễn:
  1. Feed Watchdog: esp_task_wdt_reset()

  2. Kiểm tra override:
     IF relay_controller->isOverrideActive(relay_id):
       relay_controller->tickOverride(relay_id)
       vTaskDelay(1000ms)
       CONTINUE (skip auto logic)

  3. Lấy giờ hiện tại: rtc_manager->getTime()
     Xác định night_mode: rtc_manager->isNightMode()

  4. Đọc profile từ RAM: schedule_manager->getProfile(relay_id)
     Chọn spray_s và cool_s theo night_mode

  5. PHASE SPRAYING:
     relay_controller->setRelay(relay_id, RELAY_ON)
     runtime_states_[relay_id].phase = PHASE_SPRAYING
     FOR i = spray_s downto 0:
       runtime_states_[relay_id].phase_remaining_s = i
       IF relay_controller->isOverrideActive(relay_id): BREAK (thoát vòng FOR)
       vTaskDelay(1000ms)

  6. PHASE COOLING_DOWN:
     relay_controller->setRelay(relay_id, RELAY_OFF)
     runtime_states_[relay_id].phase = PHASE_COOLING_DOWN
     FOR i = cool_s downto 0:
       runtime_states_[relay_id].phase_remaining_s = i
       IF relay_controller->isOverrideActive(relay_id): BREAK
       vTaskDelay(1000ms)

  7. GOTO LOOP
```

**`updateProfile()` — Thread-safe update:**
- Dùng `SemaphoreHandle_t profile_mutex_` (FreeRTOS Mutex).
- `xSemaphoreTake(profile_mutex_, portMAX_DELAY)`.
- Cập nhật `profiles_[relay_id]`.
- `xSemaphoreGive(profile_mutex_)`.
- Gọi `nvs->saveProfile(relay_id, profile)` (blocking write, chỉ 1 lần).

---

### TRACK F — Orchestrator (main.cpp)

---

#### Task F-1: Triển khai `setup()` và `loop()` trong main.cpp
**File tạo mới:** `aeroponics-firmware/src/main.cpp`

**`setup()` — Thứ tự khởi động bắt buộc:**

```
1. Serial.begin(115200) — LOG: "=== Aeroponics Boot ==="
2. relay_controller.initPins() — FIRST ALWAYS: Set GPIO LOW trước OUTPUT
3. nvs_storage.begin() → fallback nếu fail
4. nvs_storage.loadAllProfiles(profiles) → điền default nếu key missing
5. Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN)
6. rtc_manager.begin() → log trạng thái DS3231
7. WiFi.begin(ssid, password) → chờ 30s (non-blocking loop)
8. IF WiFi connected: rtc_manager.syncFromNtp()
9. schedule_manager.begin(&nvs_storage, &rtc_manager, &relay_controller)
10. schedule_manager.loadProfiles(profiles) — nạp profile vào RAM của scheduler
11. schedule_manager.startAllTasks() — tạo 4 FreeRTOS tasks
12. LOG: "=== Boot Complete ==="
```

**`loop()` — Chỉ làm 2 việc:**
```
1. esp_task_wdt_reset() — feed watchdog của task loop
2. Xử lý Serial debug commands (sprint debug only):
   - "status" → in trạng thái tất cả relay
   - "override 1 on 60" → manual override relay 1 trong 60s
   - "factory" → factory reset NVS
3. WiFi reconnect check: mỗi 60s kiểm tra, nếu mất → reconnect
4. vTaskDelay(100ms) — nhường CPU
```

---

## 4. TIÊU CHUẨN RÀ SOÁT CỨNG (Sprint 1 Hardened Rules)

> Các rule dưới đây là **bắt buộc 100%**. Code review phải từ chối nếu vi phạm bất kỳ rule nào.

### Rule S1-HW-01: Boot GPIO Safety Order
```
PASS: digitalWrite(RELAY_PIN_N, LOW) đứng TRƯỚC pinMode(RELAY_PIN_N, OUTPUT)
FAIL: Bất kỳ trường hợp nào initPins() được gọi sau bất kỳ logic nào khác
```
**Kiểm tra:** Code review thủ công trong `main.cpp setup()` và `relay_controller.cpp initPins()`.

### Rule S1-NVS-02: Strict NVS Write Policy
```
PASS: nvs_storage.saveProfile() chỉ được gọi từ schedule_manager.updateProfile()
FAIL: Bất kỳ NVS write nào trong vòng lặp, trong task delay, hoặc định kỳ
FAIL: NVS write bên trong FreeRTOS Task mà không qua mutex
```
**Kiểm tra:** Grep `saveProfile` trong toàn bộ `src/` — phải chỉ có 1 call site.

### Rule S1-NVS-03: NVS Value Range Validation
```
PASS: Mọi giá trị đọc từ NVS đều qua validation range trước khi dùng
FAIL: Dùng giá trị NVS raw (không validated) — có thể gây relay phun vô hạn
Spray range: [5s, 300s]
Cooldown range: [30s, 7200s]
```

### Rule S1-RTC-04: Time Source Fallback Không Silent Fail
```
PASS: Khi DS3231 không khả dụng, log rõ WARNING và default về DAY mode
FAIL: Trả về thời gian không hợp lệ (is_valid=false) mà không log và không default
```

### Rule S1-OS-05: FreeRTOS Stack Overflow Protection
```
PASS: Mỗi task được tạo với stack >= RELAY_TASK_STACK_SIZE (8192 bytes)
PASS: Bật CONFIG_FREERTOS_USE_TRACE_FACILITY và stack overflow hook
FAIL: Stack < 4096 bytes cho bất kỳ task nào
```

### Rule S1-MUTEX-06: Thread-Safe Profile Access
```
PASS: Mọi đọc/ghi profiles_[] từ nhiều FreeRTOS tasks đều qua profile_mutex_
FAIL: Direct access profiles_[] mà không take mutex
```

### Rule S1-WDT-07: Watchdog Feed Obligation
```
PASS: esp_task_wdt_reset() được gọi trong MỖI iteration của task loop
FAIL: Bất kỳ vTaskDelay() > WDT_TIMEOUT_S giây mà không feed WDT trước
```

---

*Sprint 1 Planning — Khởi tạo bởi Baseline Agent ngày 2026-07-30*
*Thực thi: Sprint 1 Implementation Agent*
