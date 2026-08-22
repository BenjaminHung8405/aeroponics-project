# 🌿 Aeroponics Lab — Lean Planning Context

> **Vai trò tài liệu này:** **Index và tổng quan** cho kế hoạch dự án Aeroponics thí nghiệm. Mọi Agent thực thi PHẢI đọc tài liệu này trước khi bắt đầu bất kỳ Sprint nào.
>
> **⚠️ Thứ tự ưu tiên SSOT (cao → thấp):**
> 1. [`PROJECT_ALIGNMENT_2026-08-10.md`](./PROJECT_ALIGNMENT_2026-08-10.md) — kiến trúc và domain production bắt buộc
> 2. [`sprint_1_5.md`](./sprint_1_5.md) — acceptance contract POC/go-no-go bắt buộc
> 3. [`sprint_2.md`](./sprint_2.md) trở đi — kế hoạch production
> 4. **File này** — chỉ là index/tổng quan; khi có mâu thuẫn, các tài liệu trên thắng.

> **Điều chỉnh phạm vi ngày 2026-08-22:** Baseline thực thi hiện tại là **01 ESP32-S3 gateway + 01 RF module 433 MHz + 04 node MEGA8**, mỗi node có RF module riêng. MEGA8 giữ schedule/timer nội bộ; ESP32 chỉ override tạm thời, monitor và lưu telemetry đã parse. Các tài liệu lịch sử đề cập 12 node không còn là scope hiện tại và phải được đọc theo baseline trong [`PROGRESS.md`](./PROGRESS.md).

---

## 1. BÀI TOÁN THỰC TẾ (Ground Truth)

| Thông số | Giá trị |
|---|---|
| **Số cụm bơm / node RF** | 04 node MEGA8, mỗi node có RF module 433 MHz riêng |
| **Đường điều khiển** | ESP32-S3 ↔ UART ↔ RF 433 MHz ↔ UART ↔ 04 MEGA8; không dùng GPIO relay trực tiếp trên ESP32 |
| **Đối tượng theo dõi** | ON/OFF 04 pump, relay/driver feedback, flow, volume, ACK/fault RF và schedule-vs-override |
| **Treatment và mapping** | Treatment/version do người dùng tạo, clone và tái sử dụng; node được gán động vào tối đa 4 group active |
| **Tần suất ghi dữ liệu** | Event + telemetry flow định kỳ theo node; chỉ lưu dữ liệu đã parse, flow sensor sizing/calibration sau POC RF |
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
| **Redis** | ❌ Bỏ hoàn toàn | Không cần queuing/cache cho quy mô lab 4 node RF (event-driven, không cần pub/sub broker thứ hai) |

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
│   │  [ESP32 gateway]                     ▼                      │
│   │  (WiFi MQTT + RF 433)     [timescaledb] :5432              │
│   │                           (internal only)                   │
│   └─────────────────────────────────────────────────────────────┘
│
│  [aero-backend] cũng tích hợp:
│  ├── TuyaBridgeModule  → đo on-demand/cuối vụ theo yêu cầu (KHÔNG poll liên tục)
│  ├── MqttModule        → subscribe node telemetry/flow/fault, heartbeat gateway
│  ├── SeasonModule      → REST API season/treatment/version/assignment
│  ├── NodeModule        → registry 4 node MEGA8, override command, RF outcome
│  ├── FlowModule        → flow event, calibration, fault
│  └── EventsGateway     → WebSocket push realtime (node/group/flow/season event)
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
│       ├── relay/                  ← [PROTOTYPE ONLY] Rig 4-relay trực tiếp; không dùng cho production
│       │   ├── relay.module.ts     ← scope: Sprint 1 direct-relay rig, không phải architecture production
│       │   ├── relay.service.ts
│       │   ├── relay.controller.ts
│       │   └── entities/
│       │       └── relay-event.entity.ts
│       ├── season/                 ← NEW: Season + Treatment + Version + Assignment
│       │   └── ...
│       ├── node/                   ← NEW: Node registry (4 node) + override command + RF outcome
│       │   └── ...
│       ├── flow/                   ← NEW: Flow event + calibration + fault
│       │   └── ...
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
- Hàm: camelCase (initRfTransport, syncRtcFromNtp, sendPumpCommand)
- Biến thành viên: snake_case với prefix_ (spray_duration_s, rf_seq_num)
- Hằng số: SCREAMING_SNAKE_CASE (RF_BAUD_RATE, NODE_COUNT_MAX, RELAY_PIN_1 [prototype only])
- Class: PascalCase (RfTransport, PumpNodeController, NvsStorage)
- **Prototype scope:** RelayController, RELAY_PIN_1 chỉ được dùng trong rig Sprint 1 trực tiếp, KHÔNG đưa vào module production.
```

### 6.3 Backend TypeScript (NestJS) — Giữ nguyên convention `mushroom-cp`

```
- Class/Interface/Enum: PascalCase (SeasonService, NodeRegistryService, PumpCommandDto, CreateTreatmentDto)
- Hàm/Phương thức: camelCase (getNodeStatus, sendPumpCommand, assignNodeToGroup)
- File: kebab-case.suffix.ts (season.service.ts, node.controller.ts, flow-event.entity.ts)
- **Prototype scope:** relay.service.ts, relay.controller.ts chỉ dùng cho rig Sprint 1, KHÔNG là contract production.
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
Sprint 0  →  Sprint 1  →  Sprint 1.5  →  Sprint 2  →  Sprint 3  →  Sprint 4
Infra         Prototype      RF + Flow       Production     NestJS        HTML
Setup         Edge safety    POC / QA        RF gateway      Backend +     Dashboard
(3 container)                decision        4 node          DB
```

| Sprint | File kế hoạch | Trạng thái |
|---|---|---|
| Sprint 0: Infrastructure Setup | [sprint_0.md](./sprint_0.md) | ✅ Hoàn thành (cần migration domain ở Sprint 3) |
| Sprint 1: Core Edge Prototype & Hardware Fail-safe | [sprint_1.md](./sprint_1.md) | ✅ Hoàn thành như prototype direct-relay |
| Sprint 1.5: RF + Flow POC & Hardware Decision Gate | [sprint_1_5.md](./sprint_1_5.md) | 🚧 Đang thực hiện — cổng bắt buộc |
| Sprint 2: Production RF Gateway & 4-MEGA8 Node Control | [sprint_2.md](./sprint_2.md) | ⛔ Blocked bởi Sprint 1.5 PASS |
| Sprint 3: NestJS Backend (Season + Group + Node + Flow) | [sprint_3.md](./sprint_3.md) | 🔵 Chờ Sprint 2 Production |
| Sprint 4: HTML Dashboard UI | [sprint_4.md](./sprint_4.md) | 🔵 Chờ Sprint 3 |

---

## 8. RÀNG BUỘC PHẦN CỨNG

| Ràng buộc | Chi tiết |
|---|---|
| **RF protocol** | Chỉ chốt module/baud/pinout sau Sprint 1.5 POC PASS; application frame có version, CRC, sequence và ACK/NACK. |
| **Pump confirmation** | Tưới thành công chỉ sau RF ACK + pump feedback + flow confirmed, không suy ra từ lệnh ON. |
| **Flow measurement** | Pulse sensor định lượng tối đa 6 L/min, calibration `pulses_per_litre` riêng từng node. |
| **Boot Safety** | Default fail-safe OFF. Với rig prototype direct relay: `digitalWrite(LOW)` **trước** `pinMode(OUTPUT)`. |
| **RTC Module** | DS3231 trên I2C: SDA = GPIO 21, SCL = GPIO 22. |
| **NVS Write Policy** | Chỉ ghi NVS khi nhận MQTT command thay đổi config. |
| **Watchdog** | Feed WDT đúng hạn trong mỗi FreeRTOS Task loop. |
| **Stack Size** | Relay task ≥ 8192 bytes. |

---

*Tài liệu cập nhật ngày 2026-08-22: Baseline thực thi là 4 node MEGA8; các tham chiếu 12 node trong tài liệu lịch sử không thuộc scope hiện tại.*
