# 🌿 Aeroponics Lab — Lean Planning Context

> **Vai trò tài liệu này:** Nguồn sự thật duy nhất (Single Source of Truth) cho toàn bộ kế hoạch triển khai dự án Aeroponics thí nghiệm. Mọi Agent thực thi PHẢI đọc tài liệu này trước khi bắt đầu bất kỳ Sprint nào.

> **Điều chỉnh kiến trúc ngày 2026-08-10:** ESP32 là gateway Wi-Fi/MQTT và **UART over RF 433 MHz** đến 12 module/cụm bơm; 12 cụm được quản lý bằng 4 group timer. Xem [PROJECT_ALIGNMENT_2026-08-10.md](./PROJECT_ALIGNMENT_2026-08-10.md), tài liệu ưu tiên khi mâu thuẫn với kế hoạch cũ.

---

## 1. BÀI TOÁN THỰC TẾ (Ground Truth)

| Thông số | Giá trị |
|---|---|
| **Số cụm bơm / node RF** | 12 cụm, chia thành 4 group timer |
| **Đường điều khiển** | ESP32 gateway ↔ UART over RF 433 MHz ↔ 12 node; không giả định 4 GPIO relay trực tiếp trong production |
| **Đối tượng theo dõi** | ON/OFF 12 pump, trạng thái/lưu lượng 12 valve, ACK/fault RF và timer group |
| **Treatment và mapping** | Treatment/version do người dùng tạo, clone và tái sử dụng; node được gán động vào tối đa 4 group active |
| **Tần suất ghi dữ liệu** | Event + telemetry flow định kỳ theo node; flow sensor định lượng max 6 L/min, sizing sau POC RF |
| **Mùa vụ** | Tối đa 120 ngày; không xóa tự động trước khi kết thúc mùa vụ và đối soát |
| **Số người dùng Dashboard** | 1–5 người (nhóm nghiên cứu) |
| **Phần cứng chạy server** | Raspberry Pi 4 hoặc máy tính lab |
| **Mục tiêu** | Thu thập số liệu nghiên cứu ổn định |

---

## 2. TECH STACK (Lean + Reuse từ `mushroom-cp`)

### 2.1 Edge / Firmware Layer — **KHÔNG THAY ĐỔI**

| Thành phần | Công nghệ | Version |
|---|---|---|
| **MCU** | ESP32-S3 (Dual-core Xtensa LX7) | — |
| **RTOS** | FreeRTOS (ESP-IDF v5.x) | — |
| **Build System** | PlatformIO | ≥ 6.x |
| **Framework** | Arduino for ESP32 | espressif32 ≥ 6.x |
| **NVS Storage** | ESP-IDF NVS API | built-in |
| **RTC Driver** | RTClib (Adafruit) cho DS3231 | ≥ 2.x |
| **MQTT Client** | PubSubClient | ≥ 2.8 |
| **JSON** | ArduinoJson | ≥ 7.x |

### 2.2 Infrastructure Layer

| Thành phần | Lựa chọn | Lý do |
|---|---|---|
| **MQTT Broker** | Mosquitto (Docker) | Nhẹ, chuẩn, quen thuộc |
| **Database** | **TimescaleDB** (`timescale/timescaledb:latest-pg15`) | Tái dùng y nguyên từ `mushroom-cp`. PostgreSQL + hypertable cho sensor data |
| **Cache** | ❌ Bỏ hoàn toàn | Không cần cho quy mô lab |
| **InfluxDB** | ❌ Bỏ hoàn toàn | Không cần 2 DB. TimescaleDB đủ sức gánh cả relational + time-series |
| **Redis** | ❌ Bỏ hoàn toàn | Không cần queuing/cache cho 4 relay |

### 2.3 Backend Layer

| Thành phần | Lựa chọn | Tái sử dụng từ `mushroom-cp` |
|---|---|---|
| **Runtime** | Node.js ≥ 20 LTS | ✅ |
| **Framework** | **NestJS** ≥ 11.x | ✅ Toàn bộ cấu trúc Module |
| **Language** | TypeScript ≥ 5.x | ✅ |
| **ORM** | TypeORM ≥ 1.0 | ✅ `database.module.ts`, `typeorm.config.ts` |
| **MQTT Client** | `mqtt` npm package | ✅ `mqtt.module.ts`, `mqtt.service.ts` (adapt lại) |
| **WebSocket** | NestJS Gateway (Server-Sent Events / WS) | ✅ Pattern từ `tuning` module |
| **Auth** | JWT + `@nestjs/jwt` | ✅ `auth` module |
| **Validation** | class-validator + class-transformer | ✅ |
| **Package Manager** | pnpm | ✅ |

> **Chiến lược tái sử dụng:** Copy các module sau từ `mushroom-backend` và adapt cho domain Aeroponics:
> - `database/` → TypeORM config + DatabaseModule
> - `mqtt/mqtt.module.ts` + `mqtt.service.ts` → Adapt sang topic aeroponics
> - `auth/` → Giữ nguyên (JWT authentication)
> - `config/` → AppConfigModule

### 2.4 Tuya Bridge Layer

**Gom vào trong NestJS Backend** — không tạo container riêng biệt.

| Thành phần | Lựa chọn |
|---|---|
| **Protocol** | Tuya Local Key (UDP Discovery + TCP AES) |
| **Library** | `tuyapi` npm package |
| **Tích hợp** | NestJS `TuyaBridgeModule` (chạy trong cùng process với Backend) |

### 2.5 Frontend Layer

| Thành phần | Lựa chọn | Lý do |
|---|---|---|
| **Framework** | **Vanilla HTML + CSS + JS** (single file) | Lab dashboard. Không cần build pipeline. Serve trực tiếp từ NestJS static. |
| **Charts** | Chart.js (CDN) | Không cần npm install |
| **Real-time** | Native WebSocket API | Đủ cho 1–5 người dùng |

---

## 3. SO SÁNH STACK: `mushroom-cp` vs Aeroponics Lab

| | mushroom-cp | Aeroponics Lab |
|---|---|---|
| **Containers** | 5 (DB + MQTT + InfluxDB + Backend + UI) | **3** (TimescaleDB + MQTT + Backend) |
| **Database** | PostgreSQL + InfluxDB v2 | **TimescaleDB only** |
| **Cache** | ❌ (không có Redis) | ❌ (không cần) |
| **Backend** | NestJS (Node.js) | **NestJS (Node.js)** — tái dùng boilerplate |
| **Tuya Bridge** | Không có | **Tích hợp vào Backend** |
| **Frontend** | Next.js (container riêng) | **Static HTML** (serve từ NestJS) |
| **RAM ước tính** | ~600–800MB | **~300–400MB** |

---

## 4. KIẾN TRÚC TỔNG QUAN

```
HOST MACHINE (Raspberry Pi 4 / Lab PC)
│
├── Port 1883 ──────────────────▶ [mosquitto]        (MQTT TCP)
├── Port 9001 ──────────────────▶ [mosquitto]        (MQTT WebSocket)
├── Port 3001 ──────────────────▶ [aero-backend]     (NestJS REST + WebSocket)
│
│   ┌──────────── Docker Network: aero_net (bridge) ─────────────┐
│   │                                                             │
│   │  [mosquitto]  ◀── pub/sub ──▶ [aero-backend:NestJS]       │
│   │       ▲                              │                      │
│   │       │                     TypeORM │                      │
│   │  [ESP32-S3]                          ▼                      │
│   │  (WiFi MQTT)              [timescaledb] :5432              │
│   │                           (internal only)                   │
│   └─────────────────────────────────────────────────────────────┘
│
│  [aero-backend] cũng tích hợp:
│  ├── TuyaBridgeModule  → poll PH-W218 mỗi 10s → MQTT publish
│  ├── MqttModule        → subscribe relay telemetry, heartbeat
│  ├── RelayModule       → REST API + send MQTT command
│  ├── SensorModule      → lưu TimescaleDB + serve history
│  └── EventsGateway     → WebSocket push realtime
│
└── Static Dashboard → NestJS serves /public/index.html
```

---

## 5. CẤU TRÚC THƯ MỤC

```
aeroponics-project/
├── docker-compose.yml              ← 3 services: timescaledb + mosquitto + aero-backend
├── .env.example
├── .env                            ← gitignore
├── .gitignore
│
├── mosquitto/
│   ├── config/
│   │   ├── mosquitto.conf
│   │   ├── passwd                  ← generated, gitignore
│   │   └── acl
│   └── data/                       ← gitignore
│
├── database/
│   └── schema.sql                  ← TimescaleDB init script
│
├── aeroponics-backend/             ← NestJS (Sprint 3)
│   ├── Dockerfile
│   ├── package.json
│   ├── pnpm-lock.yaml
│   ├── tsconfig.json
│   ├── nest-cli.json
│   └── src/
│       ├── main.ts
│       ├── app.module.ts
│       ├── config/                 ← AppConfigModule (from mushroom-cp)
│       ├── database/               ← DatabaseModule + TypeORM (from mushroom-cp)
│       │   ├── database.module.ts
│       │   ├── database.service.ts
│       │   ├── typeorm.config.ts
│       │   └── migrations/
│       ├── mqtt/                   ← MqttModule adapted (from mushroom-cp)
│       │   ├── mqtt.module.ts
│       │   └── mqtt.service.ts
│       ├── relay/                  ← NEW: Relay management
│       │   ├── relay.module.ts
│       │   ├── relay.service.ts
│       │   ├── relay.controller.ts
│       │   └── entities/
│       │       └── relay-event.entity.ts
│       ├── sensor/                 ← NEW: Water quality sensor
│       │   ├── sensor.module.ts
│       │   ├── sensor.service.ts
│       │   ├── sensor.controller.ts
│       │   └── entities/
│       │       └── sensor-reading.entity.ts
│       ├── device/                 ← Device status (adapted from mushroom-cp)
│       │   └── ...
│       ├── tuya-bridge/            ← NEW: Tuya local polling
│       │   ├── tuya-bridge.module.ts
│       │   └── tuya-bridge.service.ts
│       └── events/                 ← WebSocket Gateway
│           └── events.gateway.ts
│
├── aeroponics-ui/                  ← Static HTML Dashboard (Sprint 4)
│   └── index.html
│
├── aeroponics-firmware/            ← ESP32-S3 firmware (Sprint 1 + 2)
│   ├── platformio.ini
│   ├── partitions.csv
│   ├── include/
│   └── src/
│
├── scripts/
│   ├── setup.sh
│   └── health-check.sh
│
└── docs/
    ├── ARCHITECTURE.md
    ├── HARDWARE_PINOUT.md
    ├── MQTT_TOPICS.md
    └── TUYA_PH_W218_SPEC.md
```

---

## 6. QUY TẮC VIẾT CODE TOÀN CỤC

> Mọi Agent thực thi PHẢI tuân theo toàn bộ các quy tắc dưới đây.

### 6.1 Nguyên tắc Tái Sử Dụng từ `mushroom-cp`

- **PHẢI** tham chiếu `/Users/benjaminhung8405/Code/mushroom-cp/mushroom-backend/src/` trước khi viết module mới.
- **PHẢI** giữ nguyên conventions: Module/Controller/Service/Entity structure.
- **CÓ THỂ** copy và adapt, KHÔNG được viết lại từ đầu nếu có sẵn template tương đương.
- **PHẢI** bỏ tất cả dependency liên quan đến InfluxDB (`@influxdata/influxdb-client`, `InfluxModule`).

### 6.2 Firmware (C++) — Giữ nguyên

```
- Hàm: camelCase (initRelayPins, syncRtcFromNtp)
- Biến thành viên: snake_case với prefix_ (spray_duration_s)
- Hằng số: SCREAMING_SNAKE_CASE (RELAY_PIN_1)
- Class: PascalCase (RelayController, NvsStorage)
```

### 6.3 Backend TypeScript (NestJS) — Giữ nguyên convention `mushroom-cp`

```
- Class/Interface/Enum: PascalCase (RelayService, CreateRelayDto)
- Hàm/Phương thức: camelCase (getRelayStatus, updateSchedule)
- File: kebab-case.suffix.ts (relay.service.ts, relay.controller.ts)
- Hằng số module: UPPER_SNAKE_CASE (MQTT_TOPIC_PREFIX)
```

### 6.4 Quy Tắc Bảo Mật

- KHÔNG hardcode credential trong source code.
- Backend đọc credential từ `.env` qua `@nestjs/config`.
- MQTT Broker: `allow_anonymous false` bắt buộc.
- TimescaleDB chỉ expose port nội bộ trong Docker network (không expose ra host).
- Không commit `.env`, `mosquitto/config/passwd`.

### 6.5 Database

- `synchronize: false` — KHÔNG BAO GIỜ bật trong production.
- Mọi schema change qua TypeORM migrations.
- Dùng `DATABASE_URL` connection string (giống `mushroom-cp`).

---

## 7. SPRINT ROADMAP

```
Sprint 0  →  Sprint 1  →  Sprint 2  →  Sprint 3  →  Sprint 4
Infra         Firmware      MQTT          NestJS        HTML
Setup         Core Engine   Protocol      Backend +     Dashboard
(3 container) & HW Safety               Tuya + DB
```

| Sprint | File kế hoạch | Trạng thái |
|---|---|---|
| Sprint 0: Infrastructure Setup | [sprint_0.md](./sprint_0.md) | 🟡 Sẵn sàng thực thi |
| Sprint 1: Core Edge Engine & Hardware Fail-safe | [sprint_1.md](./sprint_1.md) | 🔵 Chờ Sprint 0 |
| Sprint 2: MQTT Protocol & Remote Control | [sprint_2.md](./sprint_2.md) | 🔵 Chờ Sprint 1 |
| Sprint 3: NestJS Backend (MQTT + Tuya + TimescaleDB) | [sprint_3.md](./sprint_3.md) | 🔵 Chờ Sprint 2 |
| Sprint 4: HTML Dashboard UI | [sprint_4.md](./sprint_4.md) | 🔵 Chờ Sprint 3 |

---

## 8. RÀNG BUỘC PHẦN CỨNG

| Ràng buộc | Chi tiết |
|---|---|
| **Relay Logic** | Active HIGH. Pull-down 10kΩ tại chân Signal. |
| **Boot Safety** | `digitalWrite(LOW)` **trước** `pinMode(OUTPUT)`. |
| **RTC Module** | DS3231 trên I2C: SDA = GPIO 21, SCL = GPIO 22. |
| **NVS Write Policy** | Chỉ ghi NVS khi nhận MQTT command thay đổi config. |
| **Watchdog** | Feed WDT đúng hạn trong mỗi FreeRTOS Task loop. |
| **Stack Size** | Relay task ≥ 8192 bytes. |

---

*Tài liệu cập nhật ngày 2026-08-10: Bổ sung 12 node RF, 12 valve/flow và mùa vụ 120 ngày.*
