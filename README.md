# 🌿 Aeroponics Precision Irrigation Gateway

> **Hệ thống Gateway Điều Khiển Tưới Khí Canh Chính Xác (High-Pressure Precision Aeroponics Automation Gateway)**  
> Cấp độ sản xuất (Production-Grade) phục vụ nông nghiệp công nghệ cao và phòng thí nghiệm thực nghiệm sinh học. Hệ thống điều khiển chu kỳ phun sương áp lực cao (hạt micron) chính xác đến từng mili-giây, miễn nhiễm hiện tượng trôi pha chu kỳ (Zero Cycle Skip), điều phối mạng cảm biến và cơ cấu chấp hành không dây nửa song công (Half-Duplex RF 433 MHz) qua cơ chế chống nghẽn và cửa sổ im lặng vô tuyến (Radio Silence Window).

---

## 1. Tổng Quan Dự Án & Badges (Project Overview)

![ESP32-S3](https://img.shields.io/badge/Hardware-ESP32--S3%20Dual--Core%20LX7-red?logo=espressif&style=flat-square)
![FreeRTOS](https://img.shields.io/badge/RTOS-FreeRTOS%20SMP-blue?logo=freertos&style=flat-square)
![PlatformIO](https://img.shields.io/badge/Build-PlatformIO%20Core-orange?logo=platformio&style=flat-square)
![NestJS](https://img.shields.io/badge/Backend-NestJS%2011%20%7C%20TypeORM-EA284E?logo=nestjs&style=flat-square)
![React/Next.js](https://img.shields.io/badge/Frontend-Next.js%2015%20%7C%20React%2019-black?logo=nextdotjs&style=flat-square)
![EMQX v5.x / Mosquitto](https://img.shields.io/badge/Broker-EMQX%20v5.x%20%2F%20Mosquitto%202.0-008080?logo=mqtt&style=flat-square)
![RF 433MHz Modbus](https://img.shields.io/badge/Southbound-RF%20433MHz%20Modbus%20CRC16-purple?style=flat-square)
![Unit Test Passing](https://img.shields.io/badge/Tests-350%2F350%20Passing%20(100%25)-success?style=flat-square)

### Mục Tiêu Giải Pháp Kỹ Thuật
1. **Kiểm Soát Phun Sương Chính Xác & Zero Cycle Skip**: Điều khiển các vòi phun áp lực cao (70–120 PSI) đạt kích thước hạt sương 30–50 $\mu\text{m}$. Thuật toán điều phối pha thời gian thực dựa trên POSIX Clock và Hardware Monotonic Clock loại bỏ hoàn toàn độ trôi tích lũy hoặc hiện tượng nhảy chu kỳ (Cycle Skip).
2. **Điều Phối Vô Tuyến Nửa Song Công Bất Đối Xứng (Half-Duplex RF Arbitration)**: Xử lý triệt để tranh chấp môi trường truyền thông RF 433 MHz khi gateway giao tiếp với các node chấp hành qua cơ chế **RF Arbiter**, **Radio Silence Window ($\pm 1.0\text{s}$)** và **Burst 3x PUMP_OFF**.
3. **An Toàn Phần Cứng & Phòng Ngừa Búa Nước (Water Hammer Prevention)**: Ngăn chặn hiện tượng sốc áp suất phá hủy đường ống khi chuyển pha hoặc đổi công thức tưới thông qua **Safe Schedule Hot-Reload (Double Buffer)** với thời gian nghỉ tối thiểu `MINIMUM_DWELL_TIME_S = 10s`.
4. **Giám Sát Khép Kín 3 Cấp (Multi-Tier Closed-Loop Feedback)**: Đối soát trạng thái lệnh qua Optocoupler Driver Gate, cảm biến dòng tải động cơ bơm Allegro ACS712 và lưu lượng kế bánh răng hình oval OF06ZAT (xác nhận lượng nước phân phối theo mL).

---

## 2. Sơ Đồ Kiến Trúc Hệ Thống (System Architecture)

### 2.1. Luồng Giao Tiếp Đa Tầng (End-to-End Multi-Tier Communication Flow)

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                                CLIENT / PRESENTATION TIER                              │
│                                                                                        │
│   [Web Dashboard: Next.js 15 + React 19] ◄──► [Reverse Proxy: Nginx :6003]            │
│   - Zustand Realtime Store (WS Authoritative)                                          │
│   - TanStack Query 5 (REST Caching)                                                    │
└────────────────────────────────────────┬───────────────────────────────────────────────┘
                                         │ WebSocket (/ws) & REST API (/api)
                                         ▼
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                              APPLICATION & INGESTION TIER                              │
│                                                                                        │
│   [Backend Engine: NestJS 11 :3001]                                                    │
│   ├── Ingestion Dispatcher & Event Router                                             │
│   ├── Tuya Local Bridge Service (PH-W218 pH/EC/TDS/ORP Sensor)                         │
│   └── TypeORM Entity Layer ◄──► [TimescaleDB / PostgreSQL 15 :5432]                    │
│                                 (Hypertables: pump_commands, flow_events, telemetry)   │
└────────────────────────────────────────┬───────────────────────────────────────────────┘
                                         │ MQTT TLS (v3.1.1 / v5.0)
                                         ▼
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                                MESSAGE BROKER TIER                                     │
│                                                                                        │
│   [EMQX v5.x / Mosquitto 2.0 Multi-Tenant Broker :11883 / :1883 / :19001 (WS)]        │
│   ├── Client ID Isolation & Dynamic ACL Policy Engine                                  │
│   └── Topic Hierarchies: aeroponics/v1/node/{id}/*, aeroponics/device/{id}/*           │
└────────────────────────────────────────┬───────────────────────────────────────────────┘
                                         │ MQTT over Wi-Fi 2.4GHz
                                         ▼
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                          EDGE CONTROLLER & GATEWAY TIER                                │
│                                                                                        │
│   [ESP32-S3 Gateway Controller]                                                        │
│   ├── FreeRTOS SMP Tasks:                                                              │
│   │   ├── Core 0: Drift Compensation Task (RTC Sync mỗi 6h) & MQTT Worker              │
│   │   └── Core 1: GroupScheduler FSM (1Hz Tick) & RF Arbiter Dispatcher                │
│   ├── Phase 1: Cached POSIX Clock (Zero I2C reads hàng giây)                           │
│   ├── Phase 2: RF Arbiter, Radio Silence Window (±1.0s) & Burst 3x PUMP_OFF            │
│   └── Phase 3: Hardware Monotonic Cooldown Engine & Double Buffer Hot-Reload           │
└────────────────────────────────────────┬───────────────────────────────────────────────┘
                                         │ RF 433 MHz Half-Duplex (Ebyte E32 UART)
                                         │ 5-byte Modbus CRC16 Frame: [Len][Opcode][Node][CRC16]
                                         ▼
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        ACTUATION & DISTRIBUTED FIELD NODES                             │
│                                                                                        │
│   [Node 01: ATmega8]    [Node 02: ATmega8]    [Node 03: ATmega8]    [Node 04: ATmega8] │
│   ├── Deadman Watchdog  ├── Deadman Watchdog  ├── Deadman Watchdog  ├── Deadman Watchdog│
│   ├── Optocoupler Gate  ├── Optocoupler Gate  ├── Optocoupler Gate  ├── Optocoupler Gate│
│   ├── ACS712 Current    ├── ACS712 Current    ├── ACS712 Current    ├── ACS712 Current  │
│   └── OF06ZAT Flow      └── OF06ZAT Flow      └── OF06ZAT Flow      └── OF06ZAT Flow    │
│           │                     │                     │                     │          │
│           ▼                     ▼                     ▼                     ▼          │
│   [Relay & Pump 1]      [Relay & Pump 2]      [Relay & Pump 3]      [Relay & Pump 4]   │
│   (High-Pressure 12V)   (High-Pressure 12V)   (High-Pressure 12V)   (High-Pressure 12V)│
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

### 2.2. Chi Tiết 3 Phase Tối Ưu Cốt Lõi (Core Production Enhancements)

#### 🔹 Phase 1: Cached POSIX Clock cho [`RtcManager`](file:///Users/benjaminhung8405/Code/aeroponics-project/aeroponics-firmware/include/rtc_manager.h)
* **Vấn đề giải quyết**: Trong kiến trúc cũ, `GroupScheduler` truy vấn thanh ghi DS1307/DS3231 qua giao tiếp I2C mỗi giây ($1\text{Hz}$) để xác định chu kỳ tưới. Giao tiếp I2C chiếm bus blocking, dễ gây trễ ngắt (jitter) và có nguy cơ treo vi điều khiển nếu đường bus I2C bị nhiễu do xung điện từ động cơ bơm (EMI).
* **Cơ chế hoạt động**:
  - Tại thời điểm khởi động (`begin()`), ESP32-S3 chỉ đọc phần cứng RTC đúng **1 lần duy nhất** để lấy epoch UTC hợp lệ và nạp vào clock nhân hệ điều hành qua hàm POSIX `settimeofday()`.
  - Mọi hàm gọi `getTime()` và `getUtcEpochSeconds()` trong chu kỳ vận hành $1\text{Hz}$ đều đọc trực tiếp từ bộ nhớ RAM (POSIX internal clock) với thời gian thực thi $< 1\,\mu\text{s}$ và **Zero I2C reads**.
  - Khởi tạo một FreeRTOS Task độc lập chạy ngầm với độ ưu tiên thấp (`priority 1`), ghim tại **Core 0**, định kỳ **6 giờ một lần** mới kích hoạt để đọc RTC, bù trừ độ trôi đồng hồ (Drift Compensation) và cập nhật lại POSIX clock một cách mượt mà.

#### 🔹 Phase 2: RF Arbiter, Radio Silence Window ($\pm 1.0\text{s}$) & Burst 3x PUMP_OFF
* **Vấn đề giải quyết**: Môi trường không dây RF 433 MHz là kênh nửa song công (Half-Duplex). Nếu một lệnh thăm dò trạng thái định kỳ (`PING` hoặc `GET_PUMP_STATE`) đang chiếm kênh truyền tại thời điểm chu kỳ tưới chuyển pha, lệnh `PUMP_OFF` có thể bị nghẽn, dẫn đến tưới quá liều hoặc rơ-le không ngắt kịp thời.
* **Cơ chế hoạt động**:
  - **RF Bus Arbiter**: Sử dụng FreeRTOS Mutex (`xSemaphoreCreateMutex`) có cơ chế kế thừa độ ưu tiên (Priority Inheritance) bọc toàn bộ giao dịch vô tuyến. Phân loại traffic theo thứ tự ưu tiên: `PUMP_CRITICAL` > `PUMP_NORMAL` > `PING` > `TELEMETRY`.
  - **Radio Silence Window ($\pm 1.0\text{s}$)**: Trước và sau thời điểm chuyển pha đúng $1000\text{ms}$ (`RF_RADIO_SILENCE_BEFORE_PHASE_MS` và `RF_RADIO_SILENCE_AFTER_PHASE_MS`), Arbiter kích hoạt cửa sổ im lặng vô tuyến. Mọi gói tin telemetry, PING hoặc bảo trì đều bị hoãn (deferred); kênh truyền được dành quyền ưu tiên tuyệt đối cho lệnh bơm.
  - **Burst 3x PUMP_OFF**: Để loại trừ hoàn toàn nguy cơ rớt gói khiến bơm chạy quá thời gian định mức, lệnh ngắt bơm `PUMP_OFF` nhóm được phát liên tiếp **3 khung tin giống hệt nhau** với khoảng cách giữa các khung là $25 - 35\text{ms}$ (`RF_PUMP_OFF_BURST_GAP_MS = 30ms`).

#### 🔹 Phase 3: Relative Hardware Monotonic Cooldown Engine, Decoupled Flow Sensor & Safe Schedule Hot-Reload
* **Vấn đề giải quyết**: Khi người dùng cập nhật công thức tưới mới (Treatment Version) khi bơm đang phun sương, việc áp dụng ngay lập tức có thể gây ngắt bơm đột ngột, gây búa nước (Water Hammer) làm vỡ đường ống hoặc làm hỏng rơ-le do đóng ngắt quá nhanh.
* **Cơ chế hoạt động**:
  - **Relative Hardware Monotonic Cooldown Engine**: Sử dụng bộ đếm thời gian vi sai đơn điệu phần cứng (`esp_timer_get_time()`) để đo lường chính xác thời lượng làm mát (cooldown duration), hoàn toàn miễn nhiễm với việc đồng hồ hệ thống bị điều chỉnh lùi do NTP/RTC sync.
  - **Safe Schedule Hot-Reload (Double Buffer)**: Khi cấu hình lịch trình mới được gửi xuống từ backend, nếu nhóm tưới đang trong pha phun (`spray_active == true`), cấu hình mới sẽ được nạp vào bộ đệm phụ `pending_profile`. Hệ thống duy trì chu kỳ phun hiện tại đến hết thời gian dự kiến, sau đó mới hoán đổi sang lịch trình mới khi bước vào pha làm mát, đồng thời ép khoảng nghỉ tối thiểu `MINIMUM_DWELL_TIME_S = 10s` để giải phóng áp suất thủy lực.
  - **Decoupled Flow Sensor Evaluation**: Tách biệt logic xác nhận lưu lượng nước khỏi vòng lặp điều khiển cơ cấu chấp hành, đối soát xung lưu lượng qua Finite State Machine (FSM) 4 trạng thái để phát hiện chính xác lỗi rách màng bơm, nghẹt đầu béc phun hoặc tụt áp.

---

## 3. Cấu Trúc Thư Mục Dự Án (Repository Structure)

```
aeroponics-project/
├── aeroponics-firmware/          # Mã nguồn nhúng điều khiển ESP32-S3 & node ATmega8 (PlatformIO)
│   ├── platformio.ini           # Cấu hình môi trường PlatformIO (esp32-s3-devkitc-1, native, atmega8)
│   ├── partitions.csv           # Sơ đồ phân vùng flash ESP32-S3 (nvs, otadata, app0, app1, coredump)
│   ├── include/                 # Khai báo tiêu đề: rtc_manager.h, agu_legacy_rf_host.h, group_scheduler.h
│   ├── src/                     # Cốt lõi logic: main.cpp, agu_legacy_codec.cpp, rtc_manager.cpp, scheduler
│   └── test/                    # Bộ unit test C++ native (350 test cases: test_crc16, test_rf_arbiter,...)
├── aeroponics-backend/           # Máy chủ ứng dụng & điều phối trung tâm (NestJS 11, Node.js 20+)
│   ├── src/
│   │   ├── auth/                # Xác thực JWT đăng nhập người dùng & quản trị viên
│   │   ├── database/            # Khởi tạo kết nối TypeORM & quản lý thực thể quan hệ / Hypertables
│   │   ├── mqtt/                # MQTT client, MQTT router, hằng số topic và bộ đồng bộ lịch trình
│   │   ├── season/              # Quản lý vòng đời mùa vụ canh tác & công thức tưới (Treatments)
│   │   ├── node/                # Quản lý đăng ký node, trạng thái FSM, điều khiển bơm override
│   │   ├── flow/                # Ingestion sự kiện đo lưu lượng & thuật toán hiệu chuẩn cảm biến
│   │   ├── tuya-bridge/         # Dịch vụ tích hợp cảm biến chất lượng nước Tuya PH-W218
│   │   └── websocket/           # WebSocket Gateway truyền dữ liệu thời gian thực lên Web UI
│   └── Dockerfile               # Định nghĩa môi trường container hóa backend
├── aeroponics-ui/                # Giao diện người dùng thời gian thực (Next.js 15 App Router, React 19)
│   ├── src/
│   │   ├── app/                 # Next.js App Router: /(auth)/login, /(dashboard)
│   │   ├── components/          # Thư viện thành phần UI: NodeCard, GroupCard, ControlSlotCard, Modals
│   │   ├── hooks/               # Custom hooks: useWebSocket, useControlSlots, useNodes, useAuth
│   │   └── store/               # Bộ quản lý trạng thái thời gian thực bằng Zustand
│   └── Dockerfile               # Định nghĩa môi trường container hóa frontend standalone
├── aeroponics-tuya-bridge/       # Dịch vụ độc lập chạy tại biên kết nối thiết bị đo chất lượng nước Tuya
├── database/                     # Hạ tầng lưu trữ dữ liệu chuỗi thời gian (TimescaleDB / PostgreSQL)
│   ├── schema.sql               # Cấu trúc bảng quan hệ, hypertables, trigger toàn vẹn và analytical views
│   └── 001_production_domain_migration.sql # Script chuyển đổi di trú dữ liệu domain
├── mosquitto/                    # Cấu hình dự phòng & ACL rules cho Eclipse Mosquitto Broker
├── nginx/                        # Cấu hình Nginx Ingress Reverse Proxy & WebSocket Upgrades
├── docs/                         # Toàn bộ tài liệu đặc tả giao thức, FSM, báo cáo kiểm thử & QA
│   ├── interface-wire-contract.md # Đặc tả chi tiết khung truyền thông northbound & southbound
│   ├── RF_PROTOCOL.md           # Đặc tả chi tiết mã hóa vô tuyến RF 433 MHz
│   └── STATE_MACHINE_MATRIX.md  # Ma trận trạng thái FSM an toàn cơ cấu chấp hành
├── scripts/                      # Kịch bản tự động hóa DevOps & kiểm tra an toàn
│   ├── health-check.sh          # Kịch bản kiểm tra toàn diện 10 tiêu chuẩn hạ tầng hệ thống
│   └── setup.sh                 # Kịch bản khởi tạo môi trường ban đầu
└── docker-compose.yml            # Khởi chạy toàn bộ stack dịch vụ sản xuất
```

---

## 4. Giao Thức Truyền Thông & Khung Bản Tin (Wire Contract & Protocol Spec)

### 4.1. Đặc Tả Khung Truyền RF 433MHz Modbus CRC16 (Southbound Boundary)

Giao diện truyền thông không dây giữa Gateway ESP32-S3 và các node chấp hành ATmega8 sử dụng cấu trúc đóng gói khung truyền **AGU Legacy SCI / Modbus CRC16** chuẩn 5-byte cho các lệnh cơ bản:

```text
Khung Lệnh Điều Khiển 5-Byte Chuẩn:
+------------+------------+---------------+------------+------------+
| Length (1B)| Opcode (1B)| Target_ID (1B)| CRC_Lo (1B)| CRC_Hi (1B)|
+------------+------------+---------------+------------+------------+
|    0x04    | 0x06 / 0x07|    1 .. 4     | uint8_t    | uint8_t    |
+------------+------------+---------------+------------+------------+
```

* **Quy ước Byte Length**: `Length = Payload_Length + 2` (bao gồm 2 byte CRC trailer). Đối với lệnh 2 byte payload (`[Opcode][Target_ID]`), trường `Length` luôn là `0x04`.
* **Phạm vi tính toán CRC**: Thuật toán CRC16-Modbus được tính toán trên chuỗi byte `[Length][Opcode][Params...]`.
* **Thông số thuật toán CRC16-Modbus**:
  - Đa thức (Polynomial): `0xA001` (Reflected form của `0x8005`)
  - Giá trị khởi tạo (Initial Value): `0xFFFF`
  - Đảo bit vào/ra (RefIn / RefOut): `true` / `true`
  - Giá trị XOR ngõ ra (XorOut): `0x0000`
  - Thứ tự sắp xếp byte: Little-Endian (`[CRC_Lo]` đứng trước, `[CRC_Hi]` đứng sau).
  - Kiểm tra tính hợp lệ: Khung nhận hợp lệ khi `CRC16_MODBUS([Length][Opcode][Params][CRC_Lo][CRC_Hi]) == 0x0000`.

### 4.2. Bảng Mã Opcodes Chuẩn (Command & Control Opcodes)

| Opcode | Tên Lệnh | Kích Thước Khung | Cấu Trúc Payload Chi Tiết | Phản Hồi Từ Node (Response) |
|:---:|:---|:---:|:---|:---|
| `0x06` | **PUMP_ON** | 5 bytes | `[0x04][0x06][Target_ID][CRC_Lo][CRC_Hi]` | Byte đơn `0x5A` (ASCII `'Z'`) xác nhận lệnh |
| `0x07` | **PUMP_OFF** | 5 bytes | `[0x04][0x07][Target_ID][CRC_Lo][CRC_Hi]` | Byte đơn `0x5A` (ASCII `'Z'`) xác nhận lệnh |
| `0x08` | **GET_PUMP_STATE**| 5 bytes | `[0x04][0x08][Target_ID][CRC_Lo][CRC_Hi]` | Khung trạng thái mạch logic & rơ-le |
| `0x05` | **PING** | 6 bytes | `[0x05][0x05][0xA5][Target_ID][CRC_Lo][CRC_Hi]` | Byte phản hồi xác nhận hoạt động |
| `0x0E` | **READ_RAM_BURST**| 8 bytes | `[0x07][0x0E][Addr_Lo][Addr_Hi][0x08][Node][CRC]` | Khung 11-byte chứa 8 bytes RAM telemetry |

> [!NOTE]
> Phản hồi `0x5A` từ vi điều khiển ATmega8 chỉ xác nhận node đã nhận lệnh qua UART RF. Gateway **không tự động coi bơm đã hoạt động hoặc lưu lượng đã chảy** cho đến khi nhận được xác nhận từ cảm biến dòng tải ACS712 và cảm biến lưu lượng OF06ZAT.

### 4.3. Danh Mục MQTT Topics Chuẩn (Northbound Contract)

Hệ thống phân cấp topic MQTT theo chuẩn kiến trúc Multi-Tenant phân định rõ ràng giữa Gateway, Node và Cảm biến:

| Danh Mục | Cấu Trúc Topic | Chiều Dữ Liệu | QoS | Mục Đích Sử Dụng |
|:---|:---|:---:|:---:|:---|
| **Node Command** | `aeroponics/v1/node/{nodeId}/command` | BE $\rightarrow$ GW | 1 | Gửi lệnh can thiệp thủ công (Override ON/OFF/PULSE) |
| **Node ACK** | `aeroponics/v1/node/{nodeId}/ack` | GW $\rightarrow$ BE | 1 | Xác nhận lệnh từ node RF đã hoàn tất hoặc timeout |
| **Node Telemetry** | `aeroponics/v1/node/{nodeId}/telemetry` | GW $\rightarrow$ BE | 0 | Đồng bộ trạng thái rơ-le, dòng điện (mA), điện áp logic |
| **Node Flow** | `aeroponics/v1/node/{nodeId}/flow` | GW $\rightarrow$ BE | 1 | Dữ liệu lưu lượng thực tế (LPM), thể tích phân phối (mL) |
| **Node Fault** | `aeroponics/v1/node/{nodeId}/fault` | GW $\rightarrow$ BE | 1 | Cảnh báo chốt lỗi phần cứng (kẹt bơm, rò rỉ, mất kết nối) |
| **Gateway Heartbeat** | `aeroponics/v1/gateway/{gatewayId}/heartbeat` | GW $\rightarrow$ BE | 0 | Báo cáo nhịp tim định kỳ, bộ nhớ heap, RSSI Wi-Fi |
| **Treatment Config**| `aeroponics/device/{deviceId}/command/config/treatment` | BE $\rightarrow$ GW | 1 | Đồng bộ công thức tưới đã publish xuống bộ nhớ Gateway |
| **Clock Sync** | `aeroponics/device/{deviceId}/command/config/clock` | BE $\rightarrow$ GW | 1 | Đồng bộ thời gian chuẩn từ máy chủ xuống Gateway |
| **Tuya Water State** | `aeroponics/sensors/{sensorId}/state` | Bridge $\rightarrow$ BE | 0 | Chỉ số lý hóa môi trường nước (pH, EC, TDS, ORP, Temp) |
| **Tuya Water Trigger**| `aeroponics/sensors/{sensorId}/command/trigger` | BE $\rightarrow$ Bridge | 1 | Lệnh kích hoạt đo đạc chất lượng nước theo yêu cầu |

---

## 5. Hướng Dẫn Thiết Lập & Khởi Chạy (Getting Started & Setup Guide)

### 5.1. Yêu Cầu Môi Trường (Prerequisites)
- **Node.js**: Phiên bản LTS $\ge 20.x$ cùng trình quản lý gói `pnpm` (khuyến nghị) hoặc `npm`.
- **Python**: Phiên bản $3.10+$ cùng các gói phụ trợ `esptool`, `pyserial`.
- **PlatformIO Core**: `pio` CLI phiên bản $\ge 6.1.15$.
- **Docker & Docker Compose**: Docker Engine $\ge 24.0$ và Compose v2.

### 5.2. Cấu Hình Biến Môi Trường
Sao chép file cấu hình mẫu và điền thông tin môi trường thực tế:
```bash
cp .env.example .env
```
Các thông số quan trọng cần lưu ý trong file [`.env`](file:///Users/benjaminhung8405/Code/aeroponics-project/.env.example):
```ini
# Cơ sở dữ liệu TimescaleDB
DB_USER=aeroponics_user
DB_PASS=YourSecureDatabasePassword123!
DB_NAME=aeroponics

# Kết nối MQTT Broker (EMQX v5.x hoặc Mosquitto)
MQTT_HOST=localhost
MQTT_PORT=1883
MQTT_ADMIN_USER=mqtt_admin
MQTT_ADMIN_PASS=YourSecureMqttPassword123!
MQTT_DEVICE_USER=esp32_device
MQTT_DEVICE_PASS=DevicePassword123!
MQTT_DEVICE_ID=esp32_device
MQTT_BACKEND_USER=aero_backend
MQTT_BACKEND_PASS=BackendPassword123!

# Xác thực người dùng NestJS
JWT_SECRET=super_secret_jwt_key_at_least_32_characters_length_required
BACKEND_PORT=3001
CORS_ORIGIN=http://localhost:3000
```

### 5.3. Khởi Chạy Hạ Tầng Bằng Docker Compose
Khởi động cụm dịch vụ cơ sở dữ liệu TimescaleDB và MQTT Broker:
```bash
# Khởi động dịch vụ cơ bản (TimescaleDB và Mosquitto/EMQX)
docker compose up -d timescaledb mosquitto

# Hoặc khởi động toàn bộ hệ thống gồm cả Backend và UI containerized:
docker compose up -d --build
```
Kiểm tra tình trạng sức khỏe của hạ tầng:
```bash
bash scripts/health-check.sh
```

### 5.4. Chạy Backend & Frontend Ở Môi Trường Development

#### Khởi chạy Backend ([`aeroponics-backend`](file:///Users/benjaminhung8405/Code/aeroponics-project/aeroponics-backend)):
```bash
cd aeroponics-backend
npm install
npm run start:dev
```
* Backend API: `http://localhost:3001/api`
* Health Check Endpoint: `http://localhost:3001/health`
* WebSocket Server: `ws://localhost:3001/ws`

#### Khởi chạy Frontend ([`aeroponics-ui`](file:///Users/benjaminhung8405/Code/aeroponics-project/aeroponics-ui)):
```bash
cd aeroponics-ui
npm install
npm run dev
```
* Web Dashboard: `http://localhost:3000` (hoặc qua Nginx Reverse Proxy tại cổng `http://localhost:6003`).

---

## 6. Quy Trình Biên Dịch, Kiểm Thử & Nạp Firmware (Firmware Operations)

Toàn bộ thao tác với mã nguồn nhúng được thực hiện tại thư mục [`aeroponics-firmware`](file:///Users/benjaminhung8405/Code/aeroponics-project/aeroponics-firmware).

### 6.1. Chạy Unit Test Native (Khẳng Định Suite 350 Tests)
Bộ kiểm thử chạy trực tiếp trên môi trường máy chủ phát triển (Native Host) mô phỏng chính xác logic FSM, Modbus CRC16, RtcManager cached clock, RF Arbiter và GroupScheduler:
```bash
cd aeroponics-firmware
pio test -e native
```
> [!TIP]
> Kết quả tiêu chuẩn đạt được:
> `================ 350 test cases: 350 succeeded in ~9.2 seconds ===============`  
> Bao gồm: `test_crc16`, `test_fsm`, `test_phase3_scheduler`, `test_production`, `test_rf_address`, `test_rf_arbiter`, `test_rtc_manager`.

### 6.2. Biên Dịch Firmware Đích ESP32-S3
Biên dịch mã nguồn nhúng cho bo mạch phần cứng ESP32-S3 DevKit-C:
```bash
cd aeroponics-firmware
pio run -e esp32-s3-devkitc-1
```
Tập tin nhị phân sau biên dịch được lưu tại `.pio/build/esp32-s3-devkitc-1/`.

### 6.3. Nạp Firmware Qua Cáp USB-OTG / Serial Trực Tiếp
Khi kết nối ESP32-S3 trực tiếp với máy tính qua cổng USB-OTG (hoặc UART Serial):
```bash
cd aeroponics-firmware
pio run -e esp32-s3-devkitc-1 --target upload
```

### 6.4. Nạp Firmware Thủ Công / Qua SBC Từ Xa (Orange Pi Zero / Linux Gateway)
Trong trường hợp nạp firmware từ xa thông qua SBC (Orange Pi Zero) kết nối với chân nạp của ESP32-S3, sử dụng công cụ `esptool.py` với các offset bộ nhớ Flash chuẩn theo [partitions.csv](file:///Users/benjaminhung8405/Code/aeroponics-project/aeroponics-firmware/partitions.csv):

```bash
esptool.py --chip esp32s3 \
  -p /dev/esp32s3_otg -b 921600 \
  --before default_reset --after hard_reset \
  write_flash -z \
  --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x0000   .pio/build/esp32-s3-devkitc-1/bootloader.bin \
  0x8000   .pio/build/esp32-s3-devkitc-1/partitions.bin \
  0xd000   ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000  .pio/build/esp32-s3-devkitc-1/firmware.bin
```

| Phân Vùng | Offset Bắt Đầu | Kích Thước | Tệp Tin Tương Ứng |
|:---|:---:|:---:|:---|
| **Bootloader** | `0x0000` | $32\text{ KB}$ | `bootloader.bin` |
| **Partition Table** | `0x8000` | $4\text{ KB}$ | `partitions.bin` |
| **OTA Data** | `0xd000` | $8\text{ KB}$ | `boot_app0.bin` |
| **App Slot 0 (`app0`)** | `0x10000` | $1.875\text{ MB}$ | `firmware.bin` |

### 6.5. Bắt Log Serial & Giám Sát Chu Kỳ Tưới
Mở giao diện giám sát serial để kiểm tra chu kỳ vận hành của hệ thống:
```bash
python3 -m serial.tools.miniterm /dev/esp32s3_otg 115200 --raw
# Hoặc sử dụng PlatformIO Device Monitor:
pio device monitor -b 115200
```

---

## 7. Các Quy Tắc An Toàn Phần Cứng & Vận Hành (Safety & Operational Rules)

### 7.1. Quy Tắc Safe Schedule Hot-Reload & Chống Búa Nước
Hệ thống đường ống khí canh hoạt động dưới áp lực lớn (vọt áp tức thời có thể đạt $> 150\text{ PSI}$ khi đóng mở van nhanh). Để loại bỏ hiện tượng búa nước (Water Hammer) và bảo vệ tiếp điểm rơ-le:
1. **Không Ngắt Bơm Đột Ngột**: Khi người dùng hoặc backend gửi cấu hình lịch trình mới, nếu nhóm tưới đang trong chu kỳ phun sương (`PHASE_SPRAYING`), Gateway sẽ ghi nhớ cấu hình vào vùng đệm `pending_profile` và tiếp tục chạy hết thời lượng phun còn lại.
2. **Ép Thời Gian Nghỉ Tối Thiểu (`MINIMUM_DWELL_TIME_S = 10s`)**: Mọi chu kỳ chuyển tiếp giữa hai lần kích hoạt bơm bắt buộc phải duy trì trạng thái nghỉ làm mát ít nhất $10\text{ giây}$. Ngay cả khi lịch trình mới yêu cầu phun ngay lập tức, bộ điều khiển vẫn cưỡng chế duy trì trạng thái ngắt cho đến khi bộ đếm monotonic đạt đủ ngưỡng $10\text{s}$.
3. **Tuân Thủ Giới Hạn Cấu Hình An Toàn**:
   - Thời gian phun ban ngày (`spray_day_s`): $5\text{s} \le t \le 300\text{s}$.
   - Thời gian nghỉ ban ngày (`cooldown_day_s`): $30\text{s} \le t \le 7200\text{s}$.
   - Thời gian phun ban đêm (`spray_night_s`): $5\text{s} \le t \le 300\text{s}$.
   - Thời gian nghỉ ban đêm (`cooldown_night_s`): $30\text{s} \le t \le 7200\text{s}$.

### 7.2. Cơ Chế Deadman Watchdog Nội Bộ Trên Node ATmega8
Để ngăn ngừa thảm họa rễ cây bị ngập úng hoặc bơm cháy do Gateway gặp sự cố mất nguồn hay treo vi điều khiển:
* Mỗi lệnh kích hoạt bơm `PUMP_ON` từ Gateway đều gửi kèm hạn mức thời gian hoạt động tối đa (`run_lease_ms`, mặc định $15 - 30\text{s}$).
* Node vi điều khiển ATmega8 duy trì một bộ đếm Deadman Timer độc lập trong firmware.
* Nếu hết thời hạn `run_lease_ms` mà node không nhận được lệnh gia hạn (Heartbeat / Keep-Alive) từ Gateway qua sóng RF, mạch ngắt an toàn nội bộ sẽ tự động cưỡng chế kích hoạt `LEASE_EXPIRED_SAFE_OFF`, ngắt rơ-le động cơ bơm ngay lập tức và đưa node về trạng thái an toàn.

---

## 8. Xử Lý Sự Cố & Câu Hỏi Thường Gặp (Troubleshooting & FAQ)

### 8.1. Hiện Tượng Flapping / Client ID Collision Trên MQTT Broker
* **Hiện tượng**: Broker liên tục ghi nhận log ngắt kết nối và kết nối lại sau mỗi vài giây (`Client already connected, closing old client`).
* **Nguyên nhân**: Hai tiến trình (ví dụ: Gateway thật và Simulator giả lập, hoặc hai phiên bản backend cùng chạy) đang sử dụng chung một `Client ID` (`MQTT_DEVICE_ID` hoặc `MQTT_BACKEND_USER`). Theo chuẩn MQTT, broker sẽ đá kết nối cũ khi kết nối mới cùng ID xuất hiện, tạo ra vòng lặp kết nối - ngắt liên tục.
* **Cách khắc phục**:
  1. Kiểm tra danh sách kết nối đang hoạt động trên EMQX Dashboard (`http://localhost:18083`) hoặc Mosquitto log.
  2. Đảm bảo mỗi thiết bị Gateway nhúng sử dụng Client ID gắn liền với địa chỉ MAC phần cứng (ví dụ: `aeroponics_gw_b81f3fbbcf3c`).
  3. Kiểm tra biến môi trường [`.env`](file:///Users/benjaminhung8405/Code/aeroponics-project/.env.example) và dừng các container chạy trùng lặp: `docker ps | grep aero`.

### 8.2. Nhận Biết Node Bị Rớt Gói RF Hoặc Rơi Vào Cửa Sổ Radio Silence
* **Hiện tượng**: Bấm nút điều khiển tay (Manual Override) trên Web Dashboard nhưng giao diện hiển thị trạng thái `PENDING` ("Đang gửi lệnh...") quá 2 giây hoặc báo lỗi Timeout.
* **Nguyên nhân & Cách kiểm tra**:
  1. **Kiểm tra Radio Silence Window**: Nếu lệnh gửi đến rơi vào khoảng thời gian $\pm 1.0\text{s}$ trước hoặc sau khi nhóm tưới chuyển pha phun/nghỉ, RF Arbiter sẽ chủ động hoãn lệnh để nhường bus cho lệnh điều khiển pha sống còn. Đây là hành vi bảo vệ bình thường.
  2. **Kiểm tra va chạm RF hoặc ngoài vùng phủ**: Quan sát log Serial của Gateway. Nếu log xuất hiện `RF Arbiter: Unicast timeout after 3 retries for Node ID X`, kiểm tra nguồn cấp $3.3\text{V}/5\text{V}$ cho mô-đun RF Ebyte E32 và ăng-ten 433 MHz tại node mục tiêu.
  3. **Kiểm tra trạng thái FSM Lockout**: Nếu một node trước đó bị sự cố mất dòng hoặc kẹt lưu lượng, hệ thống sẽ đưa node vào trạng thái `FAULT_LOCKED`. Khi đó, mọi lệnh bật bơm đều bị từ chối ở tầng Gateway cho đến khi sự cố phần cứng được xóa lỗi thủ công qua lệnh Reset.

---

## 9. Đội Ngũ Phát Triển & Bản Quyền (Authors & License)

- **Đơn vị phát triển**: Aeroponics Automation Engineering Team.
- **Tiêu chuẩn thiết kế**: Industrial IIoT System Architecture, Clean Architecture & Safe Embedded FreeRTOS Systems.
- **Giấy phép**: Proprietary — Sử dụng nội bộ trong dự án phòng thí nghiệm khí canh công nghệ cao.
