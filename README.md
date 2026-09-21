# 🌿 Aeroponics Lab — IoT High-Pressure Aeroponics Automation System

> **Hệ thống tự động hóa và giám sát khí canh áp suất cao (High-Pressure Aeroponics)** phục vụ nghiên cứu thực nghiệm nông nghiệp công nghệ cao. Hệ thống kết hợp mạng cảm biến/cơ cấu chấp hành không dây (WSN/IoT), cơ chế bảo vệ an toàn phần cứng đa tầng, lưu trữ chuỗi thời gian phân tích và bảng điều khiển thời gian thực.

---

## 1. Tổng Quan Dự Án (Project Overview)

Dự án **Aeroponics Lab** được thiết kế nhằm giải quyết bài toán tưới khí canh chính xác trong điều kiện phòng thí nghiệm và nông nghiệp công nghệ cao:
- **Tưới chính xác & chu kỳ lặp lại nghiêm ngặt**: Điều khiển phun sương kích thước hạt micron ở chu kỳ giây (ví dụ: phun 15s, nghỉ 300s) phân tách ngày/đêm.
- **Vận hành tự chủ & An toàn tuyệt đối (Fail-Safe)**: Loại bỏ rủi ro hư hỏng cây trồng hoặc cháy bơm do rớt mạng hoặc treo vi điều khiển thông qua kiến trúc phân tán với cơ chế Deadman Lease và phản hồi đa tầng (Multi-tier Hardware Feedback).
- **Thu thập dữ liệu nghiên cứu chuyên sâu**: Toàn bộ sự kiện chu kỳ bơm, lưu lượng thực tế phân phối theo mL, dòng tiêu thụ động cơ, và thông số dung dịch (pH, EC, TDS, ORP) được đồng bộ hóa và lưu trữ phục vụ phân tích đối soát mùa vụ.

---

## 2. Kiến Trúc Hệ Thống (System Architecture)

Hệ thống hoạt động theo mô hình phân tầng khép kín từ phần cứng chấp hành đến giao diện người dùng:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            HỆ THỐNG MẠNG & PHẦN CỨNG                        │
│                                                                             │
│  [Node 01: ATmega8]   [Node 02: ATmega8]   [Node 03: ATmega8]   [Node 04: ATmega8] │
│  (Bơm 1 + Flow + ACS) (Bơm 2 + Flow + ACS) (Bơm 3 + Flow + ACS) (Bơm 4 + Flow + ACS) │
│           ▲                    ▲                    ▲                    ▲  │
│           └────────────────────┼────────────────────┼────────────────────┘  │
│                     LoRa 433 MHz (Ebyte E32-433T20D)                        │
│                        Bảo mật HMAC-SHA256 & Anti-Replay                    │
│                                        ▼                                    │
│                         [ESP32-S3 Gateway Controller]                       │
│                         - DS3231 RTC & FreeRTOS Tasks                       │
│                         - Điều phối RF & Chuyển đổi MQTT                    │
└────────────────────────────────────────┬────────────────────────────────────┘
                                         │ WiFi (WPA2) / MQTT TLS
                                         ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      HẠ TẦNG DOCKER (DOCKER COMPOSE)                        │
│                                                                             │
│                     [Mosquitto MQTT Broker :11883 / :19001]                 │
│                                        ▲                                    │
│                                        │ MQTT Telemetry / Commands          │
│                                        ▼                                    │
│                     [aero-backend: NestJS 11 Engine :3001]                  │
│                     ├── Ingestion & Command Dispatcher                      │
│                     ├── Tuya Local Bridge (PH-W218 pH/EC/ORP Sensor)        │
│                     ├── REST API & Native WebSocket Gateway (/ws)           │
│                     └── TypeORM Entity Layer                                │
│                            ▲                   │                            │
│                            │                   ▼                            │
│                            │        [TimescaleDB (PostgreSQL 15) :5432]     │
│                            │        ├── Regular Relational Tables           │
│                            │        ├── 5 Time-Series Hypertables           │
│                            │        └── Analytical SQL Views (p50/p95)      │
│                            │                                                │
│                     [aero-ui: Next.js 15 App Router :3000]                  │
│                     ├── Zustand Realtime Cache + TanStack Query             │
│                     └── JWT Cookie httpOnly Authentication                  │
│                            ▲                                                │
│                            │ HTTP Proxy / WS Upgrade                        │
│                     [Nginx Reverse Proxy :6003]                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Các Phân Hệ Chính (Core Components)

### 3.1. Edge Actuators & Remote Nodes (ATmega8)
- **Kiến trúc tự chủ (Autonomous Scheduler)**: Mỗi node vi điều khiển Microchip ATmega8A vận hành độc lập một cụm bơm và cảm biến. Cấu hình lịch tưới được lưu trữ bền vững trong EEPROM. Node tự động đếm thời gian thực thi chu kỳ phun/nghỉ mà không phụ thuộc vào nhịp tick từ Gateway.
- **Cơ chế Lease Deadman**: Mọi lệnh ghi đè (Override ON) từ Gateway đều đi kèm hạn mức thời gian (`run_lease_ms`). Nếu mất kết nối vô tuyến quá hạn, node lập tức kích hoạt `LEASE_EXPIRED_SAFE_OFF` và khóa chốt an toàn.
- **Phần cứng phản hồi 3 cấp**:
  1. *Driver Gate Feedback*: Optocoupler EL817 kiểm tra mức logic cổng điều khiển MOSFET LR7843.
  2. *Current Sensing*: IC Allegro ACS712ELCTR-05B đo dòng điện tiêu thụ (phát hiện kẹt rotor, đứt dây tải).
  3. *Volumetric Flow*: Cảm biến bánh răng hình oval OF06ZAT (độ phân giải ~1,200 pulses/L) đo lượng chất lỏng thực sự đi qua đầu béc phun.

### 3.2. Production RF Gateway (ESP32-S3)
- **MCU điều phối**: ESP32-S3 Dual-core Xtensa LX7, 8MB Flash.
- **Giao thức RF 433 MHz**: Điều khiển mô-đun Ebyte E32-433T20D qua UART cứng, mã hóa toàn bộ gói tin bằng HMAC-SHA256 (khóa 16-byte tiền cấp phát trong NVS), chống tấn công phát lại (anti-replay với session counter và sequence).
- **Nguyên tắc thiết kế**: Gateway **tuyệt đối không kéo relay trực tiếp bằng chân GPIO**; chỉ đóng vai trò cầu nối dữ liệu giữa MQTT và mạng RF.
- **Bảo toàn Flash (NVS Endurance)**: Không bao giờ ghi dữ liệu telemetry biến động vào bộ nhớ NVS ở chu kỳ vận hành; chỉ lưu cấu hình tĩnh (RF PSK, MQTT credentials, node mask).

### 3.3. Backend Ingestion & Business Logic (NestJS 11)
- **Ngôn ngữ & Nền tảng**: TypeScript, Node.js 20+, NestJS v11 kiến trúc Module hóa tinh gọn.
- **Giao thức thời gian thực**: WebSocket native (`@nestjs/platform-ws`) truyền tải trạng thái bơm, lưu lượng, cảnh báo tức thời lên giao diện với độ trễ < 100ms.
- **Tuya Local Bridge**: Tích hợp trực tiếp module kết nối cảm biến chất lượng nước Tuya PH-W218 qua giao thức cục bộ UDP Discovery + TCP AES (đo theo yêu cầu On-Demand hoặc thời điểm kết thúc vụ; không định kỳ liên tục để bảo vệ tuổi thọ đầu dò).

### 3.4. Cơ Sở Dữ Liệu Thời Gian Thực (TimescaleDB)
- **Engine**: TimescaleDB chạy trên PostgreSQL 15, phân vùng dữ liệu dạng Hypertables (chu kỳ chunk 1 ngày).
- **Bảng quan hệ (Relational Tables)**:
  - `seasons`: Quản lý mùa vụ nuôi trồng (tối đa 120 ngày).
  - `treatments` & `treatment_versions`: Công thức tưới (thời gian phun/nghỉ ngày và đêm). Phiên bản một khi ở trạng thái `PUBLISHED` sẽ bất biến (enforced bởi database trigger).
  - `timer_groups`: 4 nhóm tưới logic độc lập (Group 1..4).
  - `group_node_assignments`: Lịch sử gán node vào nhóm tưới theo mùa vụ.
  - `sensor_calibrations`: Quản lý hệ số xung/lít (`pulses_per_litre`) đã kiểm định cho từng cảm biến.
  - `node_registry`: Danh mục node phần cứng; bắt buộc có calibration `ACTIVE` hợp lệ trước khi cho phép kích hoạt bơm (`assert_pump_on_calibration`).
- **Bảng chuỗi thời gian (Hypertables)**:
  - `pump_commands`: Nhật ký lệnh gửi, thời gian xác nhận ACK, độ trễ và kết quả.
  - `pump_state_events`: Lịch sử chuyển trạng thái (SCHEDULE, MANUAL_OVERRIDE, FAIL_SAFE).
  - `pump_feedback_events`: Dữ liệu điện áp, dòng tiêu thụ mA, cờ sai lệch logic driver.
  - `flow_events`: Lưu lượng LPM, thể tích tích lũy mL, cờ cảnh báo lỗi béc phun.
  - `measurement_readings`: Chỉ số lý hóa môi trường rễ (pH, EC, TDS, ORP, nhiệt độ).
- **Analytical Views**: Thống kê hiệu năng lệnh (`v_command_performance_analytics`), độ ổn định dòng chảy (`v_flow_stability_and_volume_analytics`), và đối soát lịch trình (`v_schedule_override_mismatch_analytics`).

### 3.5. Bảng Điều Khiển Người Dùng (Next.js 15 UI)
- **Công nghệ**: Next.js 15 (App Router, SSR + Client Components), React 19, Tailwind CSS 3.
- **Quản lý trạng thái**:
  - `Zustand 5`: Quản lý state thời gian thực (trạng thái 4 node, 4 nhóm tưới) cập nhật qua WebSocket.
  - `TanStack Query 5`: Cache và đồng bộ dữ liệu REST API (Mùa vụ, công thức tưới, lịch sử đo đạc).
- **Bảo mật**: Xác thực người dùng bằng JWT Token lưu trong `httpOnly` cookie, bảo vệ route bằng Next.js `middleware.ts`.

---

## 4. Mô Hình Quản Lý & Vận Hành (Domain Model)

Hệ thống tổ chức cấu trúc canh tác theo phân cấp chặt chẽ:

```
[MÙA VỤ (Season)] (Tối đa 120 ngày)
  └── [CÔNG THỨC TƯỚI (Treatment)]
        └── [PHIÊN BẢN (Treatment Version)] (Bất biến khi Published: Spray Day/Night, Cooldown Day/Night)
              └── [NHÓM TƯỚI (Timer Group 1..4)]
                    └── [NODE PHẦN CỨNG (Node 1..4)] (Yêu cầu Cảm biến đã Calibrate ACTIVE)
```

1. **Khởi tạo Mùa Vụ**: Thiết lập mục tiêu EC, pH và thời gian bắt đầu.
2. **Soạn thảo Công Thức Tưới**: Định nghĩa thời gian phun (5..300s) và thời gian nghỉ (30..7200s). Sau khi phát hành (PUBLISHED), công thức được khóa để đảm bảo tính toàn vẹn số liệu khoa học.
3. **Phân bổ Nhóm & Gán Node**: Gán công thức vào Nhóm tưới (Group 1..4), sau đó gán các Node (1..4) vào Nhóm tương ứng.
4. **Kiểm định Cảm Biến**: Mỗi cảm biến lưu lượng OF06ZAT phải trải qua tối thiểu 3 lần chạy mẫu để tính hệ số xung/lít (`pulses_per_litre`) và độ lặp lại (`repeatability_pct <= 5.00%`). Chỉ khi trạng thái đạt `ACTIVE`, hệ thống mới mở chốt an toàn cho phép kích hoạt bơm.

---

## 5. Bảng Thông Số Kỹ Thuật (Tech Stack Summary)

| Phân hệ | Thành phần / Thư viện | Vai trò |
|---|---|---|
| **Node Firmware** | ATmega8A (AVR C++17, Bare-metal/Arduino) | Bộ điều khiển tự chủ cục bộ 4 vỉ phun |
| **Gateway Firmware**| ESP32-S3 (PlatformIO, Arduino ESP32, FreeRTOS)| Gateway vô tuyến RF LoRa 433 MHz ↔ MQTT |
| **RF Transceiver** | Ebyte E32-433T20D (SX1278 433MHz UART) | Kênh truyền thông tầm phủ 1000m trong nhà màng |
| **Flow Sensor** | OF06ZAT Oval Gear Pulse Sensor | Cảm biến lưu lượng chính xác cao ($1,200\text{ pulses/L}$) |
| **Current Sensor** | Allegro ACS712ELCTR-05B | Đo dòng tải bơm 12VDC ($185\text{ mV/A}$) |
| **Timekeeping** | DS3231M Precision RTC (I2C) | Đồng hồ thời gian thực độ trôi thấp ($\pm 2\text{ ppm}$) |
| **Message Broker** | Eclipse Mosquitto 2.0 (Docker) | MQTT v3.1.1/v5.0 Broker kèm xác thực ACL |
| **Database** | TimescaleDB (PostgreSQL 15) | Cơ sở dữ liệu quan hệ kết hợp Hypertables chuỗi thời gian |
| **Backend API** | NestJS 11, TypeORM, WebSockets, Node.js 20+ | REST API, WebSocket realtime, Tuya Local Bridge |
| **Frontend UI** | Next.js 15, React 19, Tailwind CSS, Zustand | Giao diện điều khiển & giám sát đa nền tảng |
| **Proxy / Ingress**| Nginx Alpine (Docker) | Định tuyến Reverse Proxy & WebSocket Upgrade |

---

## 6. Cấu Trúc Thư Mục Dự Án (Repository Structure)

```
aeroponics-project/
├── aeroponics-firmware/          # Mã nguồn firmware ESP32-S3 & ATmega8
│   ├── platformio.ini           # Cấu hình môi trường PlatformIO (esp32-s3, atmega8-node, native)
│   ├── include/                 # File định nghĩa (.h) cấu hình, giao thức RF, MQTT, NVS
│   ├── src/                     # Code chính: main, rf_frame_codec, mqtt_client, node_registry
│   └── test/                    # Bộ kiểm thử native C++ và tích hợp
├── aeroponics-backend/           # Dịch vụ backend NestJS
│   ├── src/
│   │   ├── auth/                # Xác thực JWT đăng nhập
│   │   ├── database/            # Cấu hình TypeORM & thực thể cơ sở dữ liệu
│   │   ├── season/              # Quản lý Mùa vụ, Công thức tưới (Treatment) & Nhóm
│   │   ├── node/                # Quản lý Node, lệnh ghi đè (Override), đồng bộ RF
│   │   ├── flow/                # Quản lý sự kiện lưu lượng & hiệu chuẩn (Calibration)
│   │   ├── tuya-bridge/         # Cầu nối cục bộ Tuya PH-W218 (on-demand)
│   │   └── websocket/           # WebSocket Gateway thời gian thực
│   └── Dockerfile               # Docker container backend
├── aeroponics-ui/                # Ứng dụng giao diện người dùng Next.js 15
│   ├── src/
│   │   ├── app/                 # Next.js App Router: /(auth)/login, /(dashboard)
│   │   ├── components/          # UI Components (NodeCard, GroupCard, Panels, Modals)
│   │   ├── hooks/               # Custom hooks: useWebSocket, useAuth, TanStack hooks
│   │   └── store/               # State store thời gian thực bằng Zustand
│   └── Dockerfile               # Docker container UI standalone
├── database/                     # Cấu hình và migration cơ sở dữ liệu
│   ├── schema.sql               # Toàn bộ cấu trúc bảng, hypertables, trigger và views
│   └── 001_production_domain_migration.sql # Script chuyển giao từ bản thử nghiệm
├── mosquitto/                    # Cấu hình Mosquitto MQTT Broker (conf, passwd, acl)
├── nginx/                        # Cấu hình reverse proxy Nginx cho các dịch vụ
├── docs/                         # Toàn bộ tài liệu kỹ thuật, kiến trúc, giao thức RF, BOM
├── scripts/                      # Các kịch bản kiểm tra an toàn & sức khỏe hệ thống
│   ├── health-check.sh          # Script kiểm tra toàn diện 10 tiêu chuẩn hạ tầng
│   └── rehearse_production_migration.sh # Kiểm tra khả năng tương thích migration
└── docker-compose.yml            # Khởi chạy đồng bộ toàn bộ cụm dịch vụ (TimescaleDB, MQTT, Backend, UI, Nginx)
```

---

## 7. Hướng Dẫn Khởi Chạy (Quickstart)

### Yêu Cầu Tiên Quyết
- Docker & Docker Compose (v2.20+)
- Node.js ≥ 20 & pnpm
- Python 3 & PlatformIO Core (dành cho phát triển firmware)

### 7.1. Khởi chạy toàn bộ hạ tầng phần mềm (Docker)
1. Tạo file môi trường từ mẫu:
   ```bash
   cp .env.example .env
   # Điền mật khẩu an toàn cho DB_PASS, MQTT_ADMIN_PASS, JWT_SECRET
   ```
2. Khởi động các container:
   ```bash
   docker compose up -d --build
   ```
3. Chạy script kiểm tra chất lượng & sức khỏe hệ thống:
   ```bash
   bash scripts/health-check.sh
   ```
4. Truy cập giao diện người dùng:
   - **Dashboard UI**: `http://localhost:6003` (hoặc cổng `3000`)
   - **Backend REST API**: `http://localhost:6003/api/` (hoặc cổng `3001`)
   - **Health Endpoint**: `http://localhost:3001/health`

### 7.2. Biên dịch & Kiểm thử Firmware
1. Chạy bộ kiểm thử C++ Native (228+ unit tests):
   ```bash
   cd aeroponics-firmware
   pio test -e native
   ```
2. Biên dịch firmware Gateway ESP32-S3:
   ```bash
   pio run -e esp32-s3-devkitc-1
   ```
3. Biên dịch firmware Node ATmega8:
   ```bash
   pio run -e atmega8-node
   ```
