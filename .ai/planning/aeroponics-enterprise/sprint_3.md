# Sprint 3: Tuya Local Bridge & NestJS Data Ingestion (Backend)

> **Phụ thuộc:** Sprint 2 hoàn thành — MQTT Broker đang chạy, ESP32 publish heartbeat và relay telemetry.
> **Output bàn giao:** (1) Tuya Bridge Node.js thu thập dữ liệu cảm biến PH-W218 → publish MQTT. (2) NestJS Backend subscribe MQTT, lưu TimescaleDB, expose REST API + WebSocket Gateway real-time cho Frontend.

---

## 1. PHẠM VI & MỤC TIÊU

### 1.1 Modules / Components bị tác động trực tiếp

**Tuya Bridge (`aeroponics-tuya-bridge/`):**

| File | Mô tả |
|---|---|
| `package.json` | Dependencies: tuyapi, mqtt, dotenv, winston |
| `src/index.ts` | Entry point, orchestrator |
| `src/tuya-device.ts` | Kết nối và polling thiết bị Tuya PH-W218 qua Local Key |
| `src/dp-parser.ts` | Parse Data Points (DP101-DP106) thành structured JSON |
| `src/mqtt-publisher.ts` | Publish sensor data lên Mosquitto |
| `src/config.ts` | Load env vars, validate |
| `Dockerfile` | Build image cho Docker Compose |

**NestJS Backend (`aeroponics-backend/`):**

| Module / File | Mô tả |
|---|---|
| `src/app.module.ts` | Root module, import tất cả sub-modules |
| `src/mqtt/mqtt.module.ts` | MQTT subscriber module, nhận telemetry từ ESP32 & Tuya Bridge |
| `src/mqtt/mqtt.service.ts` | Service xử lý MQTT messages, route đến đúng sub-service |
| `src/relay/relay.module.ts` | Relay management module |
| `src/relay/relay.service.ts` | Business logic: lấy trạng thái, gửi command, lưu history |
| `src/relay/relay.controller.ts` | REST endpoints cho relay |
| `src/relay/entities/relay-event.entity.ts` | TypeORM entity cho relay state events |
| `src/water-quality/water-quality.module.ts` | Water quality sensor module |
| `src/water-quality/water-quality.service.ts` | Lưu và truy vấn sensor readings |
| `src/water-quality/water-quality.controller.ts` | REST endpoints cho sensor data |
| `src/water-quality/entities/sensor-reading.entity.ts` | TypeORM entity cho TimescaleDB hypertable |
| `src/gateway/events.gateway.ts` | NestJS WebSocket Gateway (Socket.IO) |
| `src/config/database.config.ts` | TypeORM + TimescaleDB connection config |
| `src/common/filters/http-exception.filter.ts` | Global exception filter |
| `src/common/interceptors/logging.interceptor.ts` | Request logging |

### 1.2 Mục tiêu cụ thể của Sprint 3

- [ ] Tuya Bridge kết nối PH-W218 qua Local Key, poll mỗi 10s, publish sensor JSON lên MQTT.
- [ ] DP101–DP106 được parse đúng thành: pH, EC, TDS, Temperature, Salinity, ORP.
- [ ] NestJS subscribe MQTT `aeroponics/device/+/telemetry/#` và `aeroponics/sensor/+/reading`.
- [ ] Relay events được lưu vào TimescaleDB hypertable `relay_events`.
- [ ] Sensor readings được lưu vào TimescaleDB hypertable `sensor_readings`.
- [ ] WebSocket Gateway push real-time khi nhận dữ liệu mới.
- [ ] REST API: GET relay history, GET sensor readings (time range), POST relay command.

---

## 2. KIẾN TRÚC & LUỒNG DỮ LIỆU

### 2.1 Kiến Trúc Tổng Quan Sprint 3

```
[Tuya PH-W218]                      [ESP32-S3]
  (UDP/TCP Local)                    (MQTT Client)
       │                                   │
       ▼                                   ▼
[aeroponics-tuya-bridge]         [Mosquitto Broker]
  - tuyapi connect                         │
  - DP parse                               ├──▶ aeroponics/device/+/status
  - MQTT publish                           ├──▶ aeroponics/device/+/telemetry/relay/+
       │                                   └──▶ aeroponics/sensor/+/reading
       └──────────────────┬────────────────┘
                          │
                    [Mosquitto]
                          │
                          ▼
                  [NestJS Backend]
                  mqtt.service.ts (subscriber)
                          │
              ┌───────────┼────────────────┐
              │           │                │
              ▼           ▼                ▼
        relay.service  water-quality   events.gateway
              │          .service           │
              │              │              │ WebSocket push
              ▼              ▼              ▼
        [TimescaleDB]   [TimescaleDB]  [Next.js UI]
        relay_events   sensor_readings  (Sprint 4)
              │
              ▼
        [REST API]
        /api/relay/*
        /api/sensor/*
```

### 2.2 Luồng Tuya Bridge — Data Collection

```
[Tuya Bridge Startup]
        │
        ▼
1. Load config: TUYA_DEVICE_IP, TUYA_DEVICE_ID, TUYA_LOCAL_KEY từ .env
2. Kết nối Mosquitto: mqtt-publisher.connect()
3. Khởi tạo tuyapi device: { ip, id, key, version: '3.3' }
4. device.connect()
   → Nếu fail: retry sau 30s với backoff
        │
        ▼
5. Polling loop (setInterval 10000ms):
   a. device.get({ schema: true }) → nhận object DPS
   b. dp-parser.parse(dps) → SensorReading struct
   c. mqtt-publisher.publish(
        topic: "aeroponics/sensor/{sensor_id}/reading",
        payload: JSON.stringify(reading)
      )
        │
        ▼
6. device.on('error', ...) → log + reconnect
7. device.on('disconnect', ...) → reconnect với backoff
```

### 2.3 Tuya DP (Data Point) Mapping PH-W218

```
DP Code → Field         → TypeScript Type → Unit
────────────────────────────────────────────────
DP 101  → ph_value      → number (float)  → pH (0-14)
DP 102  → ec_value      → number (int)    → µS/cm
DP 103  → tds_value     → number (int)    → ppm
DP 104  → temperature   → number (float)  → °C
DP 105  → salinity      → number (int)    → ppt
DP 106  → orp_value     → number (int)    → mV
DP 107  → turbidity     → number (int)    → NTU (nếu có)

Multiplier: Tuya thường gửi integer × 10 → chia 10 để ra float
Ví dụ: DP101 = 68 → pH = 6.8
        DP104 = 225 → Temp = 22.5°C
```

### 2.4 Luồng NestJS MQTT → Database → WebSocket

```
[Mosquitto] ──MQTT──▶ [mqtt.service.ts onMessage()]
                              │
              ┌───────────────┼──────────────────────┐
              │               │                      │
   topic: status       topic: telemetry       topic: sensor/reading
              │               │                      │
              ▼               ▼                      ▼
   deviceStatus         relay.service          water-quality.service
   update (Redis?)      .handleTelemetry()     .saveReading()
                              │                      │
                        relay_events           sensor_readings
                        .save(event)           .save(reading)
                              │                      │
                              └──────────┬───────────┘
                                         │
                                events.gateway
                                .emit('relay_update', data)
                                .emit('sensor_update', data)
                                         │
                                   [Socket.IO]
                                    → Frontend
```

### 2.5 TimescaleDB Schema Design

**`relay_events` (hypertable, time column = `occurred_at`):**
```sql
relay_id         SMALLINT NOT NULL        -- 1, 2, 3, 4
device_id        VARCHAR(64) NOT NULL
state            VARCHAR(32) NOT NULL     -- SPRAYING|COOLING_DOWN|MANUAL_ON|...
phase_remaining_s INT
mode             VARCHAR(8)               -- day|night
override_active  BOOLEAN DEFAULT FALSE
occurred_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
```

**`sensor_readings` (hypertable, time column = `recorded_at`):**
```sql
sensor_id        VARCHAR(64) NOT NULL
ph_value         NUMERIC(4,2)            -- 0.00 – 14.00
ec_value         INT                     -- µS/cm
tds_value        INT                     -- ppm
temperature      NUMERIC(5,2)            -- °C
salinity         NUMERIC(6,3)            -- ppt
orp_value        INT                     -- mV
turbidity        NUMERIC(8,2)            -- NTU
recorded_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
```

**`device_status` (regular table):**
```sql
device_id        VARCHAR(64) PRIMARY KEY
status           VARCHAR(16)             -- online|offline
uptime_s         BIGINT
rssi_dbm         SMALLINT
free_heap_b      INT
ntp_synced       BOOLEAN
rtc_valid        BOOLEAN
last_seen_at     TIMESTAMPTZ
```

---

## 3. PHÂN RÃ CHI TIẾT TÁC VỤ

### TRACK A — Tuya Local Bridge (Node.js)

---

#### Task A-1: Khởi tạo project và cấu hình
**File tạo mới:** `aeroponics-tuya-bridge/package.json`

**Dependencies:**
```json
{
  "dependencies": {
    "tuyapi": "^7.5.x",
    "mqtt": "^5.x",
    "dotenv": "^16.x",
    "winston": "^3.x"
  },
  "devDependencies": {
    "typescript": "^5.x",
    "@types/node": "^20.x",
    "ts-node": "^10.x",
    "tsx": "^4.x"
  }
}
```

---

#### Task A-2: Định nghĩa config module
**File tạo mới:** `aeroponics-tuya-bridge/src/config.ts`

**Hàm cần triển khai:**
- `loadConfig(): BridgeConfig` — Đọc và validate env vars:
  - `TUYA_DEVICE_IP` (required)
  - `TUYA_DEVICE_ID` (required)
  - `TUYA_LOCAL_KEY` (required, length = 16)
  - `TUYA_SENSOR_ID` (default: mac address hash)
  - `MQTT_BROKER_URL` (required)
  - `MQTT_USERNAME` (required)
  - `MQTT_PASSWORD` (required)
  - `POLL_INTERVAL_MS` (default: 10000)
- Ném `Error` có message rõ ràng nếu required field thiếu.

---

#### Task A-3: DP Parser
**File tạo mới:** `aeroponics-tuya-bridge/src/dp-parser.ts`

**Interface cần định nghĩa:**
```typescript
interface SensorReading {
  sensor_id: string;
  ph_value: number | null;
  ec_value: number | null;
  tds_value: number | null;
  temperature: number | null;
  salinity: number | null;
  orp_value: number | null;
  turbidity: number | null;
  raw_dps: Record<string, unknown>;   // Raw DPS cho debug
  recorded_at: string;                // ISO 8601 UTC
}
```

**Hàm cần triển khai:**
- `parseDps(dps: Record<string, number>, sensorId: string): SensorReading`
  - Ánh xạ DP code → field name theo bảng mapping (Task C trong Sprint 2 doc).
  - Áp dụng scale factor (÷10) khi cần.
  - Validate range: pH [0–14], EC [0–10000], Temp [-10–50].
  - Nếu DP không tồn tại trong response → set `null` (không throw).
  - Log WARNING nếu giá trị out of range.

---

#### Task A-4: MQTT Publisher Module
**File tạo mới:** `aeroponics-tuya-bridge/src/mqtt-publisher.ts`

**Hàm cần triển khai:**
- `connect(config: MqttConfig): Promise<void>` — Kết nối với retry.
- `publishSensorReading(reading: SensorReading): Promise<void>`
  - Topic: `aeroponics/sensor/{sensor_id}/reading`
  - QoS: 0 (sensor data, high frequency, loss acceptable)
  - Payload: `JSON.stringify(reading)`
- `disconnect(): Promise<void>` — Graceful disconnect.
- `isConnected(): boolean`

---

#### Task A-5: Tuya Device Manager
**File tạo mới:** `aeroponics-tuya-bridge/src/tuya-device.ts`

**Hàm cần triển khai:**
- `connect(): Promise<void>` — Init tuyapi, connect với retry backoff.
- `startPolling(intervalMs: number, onData: (dps: Record<string, number>) => void): void`
  - Dùng `setInterval` + `device.get({ schema: true })`.
  - Catch error trong callback, log + continue (không crash process).
- `stopPolling(): void` — Clear interval.
- `disconnect(): Promise<void>`
- Private `reconnectWithBackoff(): Promise<void>` — Backoff: 5s → 10s → 30s → 60s.

---

#### Task A-6: Entry Point Orchestrator
**File tạo mới:** `aeroponics-tuya-bridge/src/index.ts`

**`main()` function logic:**
```
1. loadConfig() → config
2. Khởi tạo winston logger
3. mqtt-publisher.connect(config.mqtt)
4. tuya-device.connect()
5. tuya-device.startPolling(config.pollIntervalMs, async (dps) => {
     reading = dp-parser.parseDps(dps, config.sensorId)
     await mqtt-publisher.publishSensorReading(reading)
     logger.info("Published reading", { ph: reading.ph_value, temp: reading.temperature })
   })
6. Process signal handlers:
   SIGINT / SIGTERM → graceful shutdown:
     tuya-device.stopPolling()
     tuya-device.disconnect()
     mqtt-publisher.disconnect()
     process.exit(0)
```

---

### TRACK B — NestJS Backend Core Setup

---

#### Task B-1: NestJS Project Scaffold & Database Config
**File tạo mới:** `aeroponics-backend/src/config/database.config.ts`

**`createTypeOrmOptions(): TypeOrmModuleOptions` hàm cần triển khai:**
- Đọc từ `ConfigService`: `DB_HOST`, `DB_PORT`, `DB_USER`, `DB_PASS`, `DB_NAME`.
- `type: 'postgres'`
- `synchronize: false` (dùng migrations trong production)
- `entities: [__dirname + '/../**/*.entity{.ts,.js}']`
- `migrations: [__dirname + '/../migrations/*{.ts,.js}']`
- `logging: process.env.NODE_ENV !== 'production'`

---

#### Task B-2: Tạo TimescaleDB Migrations
**File tạo mới:** `aeroponics-backend/src/migrations/001_create_relay_events.ts`
**File tạo mới:** `aeroponics-backend/src/migrations/002_create_sensor_readings.ts`
**File tạo mới:** `aeroponics-backend/src/migrations/003_create_device_status.ts`

**Mỗi migration phải:**
- `up()`: CREATE TABLE với đúng schema (xem Section 2.5).
- `up()` cho `relay_events` và `sensor_readings`: Gọi `SELECT create_hypertable('table_name', 'time_column')`.
- `down()`: DROP TABLE (và `drop_chunks` nếu là hypertable).

---

### TRACK C — NestJS MQTT Module

---

#### Task C-1: MQTT Module Setup
**File tạo mới:** `aeroponics-backend/src/mqtt/mqtt.module.ts`

- Dùng `@nestjs/microservices` MQTT transport hoặc `mqtt` npm package trực tiếp.
- Import `RelayModule` và `WaterQualityModule` để inject services.

---

#### Task C-2: MQTT Service — Message Router
**File tạo mới:** `aeroponics-backend/src/mqtt/mqtt.service.ts`

**Hàm cần triển khai:**

| Hàm | Signature | Mô tả |
|---|---|---|
| `onModuleInit` | `async onModuleInit(): Promise<void>` | Kết nối MQTT, subscribe topics |
| `onModuleDestroy` | `async onModuleDestroy(): Promise<void>` | Graceful disconnect |
| `handleMessage` | `private handleMessage(topic: string, payload: Buffer): Promise<void>` | Router chính |
| `handleRelayTelemetry` | `private handleRelayTelemetry(deviceId: string, relayId: number, data: RelayTelemetryDto): Promise<void>` | Lưu relay event + emit websocket |
| `handleDeviceStatus` | `private handleDeviceStatus(deviceId: string, data: DeviceStatusDto): Promise<void>` | Cập nhật device_status table |
| `handleSensorReading` | `private handleSensorReading(sensorId: string, data: SensorReadingDto): Promise<void>` | Lưu sensor reading + emit websocket |

**Topic routing logic trong `handleMessage()`:**
```typescript
// Pattern matching:
if (topic.match(/aeroponics\/device\/.+\/status/))               → handleDeviceStatus
if (topic.match(/aeroponics\/device\/.+\/telemetry\/relay\/\d+/)) → handleRelayTelemetry
if (topic.match(/aeroponics\/sensor\/.+\/reading/))               → handleSensorReading
```

---

### TRACK D — Relay & Water Quality Modules

---

#### Task D-1: Relay TypeORM Entity
**File tạo mới:** `aeroponics-backend/src/relay/entities/relay-event.entity.ts`

**Decorator mapping:**
```typescript
@Entity('relay_events')
export class RelayEvent {
  @PrimaryGeneratedColumn('uuid')
  id: string;

  @Column({ type: 'smallint' })
  relay_id: number;

  @Column({ length: 64 })
  device_id: string;

  @Column({ length: 32 })
  state: string;         // RelayState enum

  @Column({ nullable: true })
  phase_remaining_s: number;

  @Column({ length: 8, nullable: true })
  mode: string;          // 'day' | 'night'

  @Column({ default: false })
  override_active: boolean;

  @Column({ type: 'timestamptz', default: () => 'NOW()' })
  occurred_at: Date;
}
```

---

#### Task D-2: Relay Service
**File tạo mới:** `aeroponics-backend/src/relay/relay.service.ts`

**Hàm cần triển khai:**

| Hàm | Signature | Mô tả |
|---|---|---|
| `saveRelayEvent` | `async saveRelayEvent(dto: RelayTelemetryDto): Promise<RelayEvent>` | Lưu event vào TimescaleDB |
| `getRelayHistory` | `async getRelayHistory(relayId: number, from: Date, to: Date, limit?: number): Promise<RelayEvent[]>` | Query lịch sử |
| `getCurrentRelayState` | `async getCurrentRelayState(relayId: number): Promise<RelayEvent>` | Lấy event mới nhất |
| `sendRelayCommand` | `async sendRelayCommand(relayId: number, dto: RelayCommandDto): Promise<void>` | Publish MQTT command đến ESP32 |

---

#### Task D-3: Relay Controller (REST API)
**File tạo mới:** `aeroponics-backend/src/relay/relay.controller.ts`

**Endpoints:**

| Method | Path | Handler | Mô tả |
|---|---|---|---|
| `GET` | `/api/relay/:id/history` | `getHistory(id, from, to, limit)` | Lịch sử relay theo khoảng thời gian |
| `GET` | `/api/relay/:id/state` | `getCurrentState(id)` | Trạng thái hiện tại |
| `POST` | `/api/relay/:id/schedule` | `updateSchedule(id, dto)` | Gửi command thay đổi schedule |
| `POST` | `/api/relay/:id/override` | `override(id, dto)` | Gửi lệnh manual override |

---

#### Task D-4: Water Quality Entity
**File tạo mới:** `aeroponics-backend/src/water-quality/entities/sensor-reading.entity.ts`

- TypeORM entity ánh xạ đúng schema `sensor_readings` (Section 2.5).
- Dùng `@Column({ type: 'numeric', precision: 4, scale: 2, nullable: true })` cho pH.

---

#### Task D-5: Water Quality Service
**File tạo mới:** `aeroponics-backend/src/water-quality/water-quality.service.ts`

**Hàm cần triển khai:**

| Hàm | Signature | Mô tả |
|---|---|---|
| `saveReading` | `async saveReading(dto: SensorReadingDto): Promise<SensorReading>` | Lưu vào TimescaleDB |
| `getLatestReading` | `async getLatestReading(sensorId: string): Promise<SensorReading>` | Reading mới nhất |
| `getReadingHistory` | `async getReadingHistory(sensorId: string, from: Date, to: Date): Promise<SensorReading[]>` | History query |
| `getAggregated` | `async getAggregated(sensorId: string, bucketInterval: string): Promise<AggregatedReading[]>` | TimescaleDB time_bucket aggregation |

**`getAggregated()` — TimescaleDB Query:**
```sql
SELECT time_bucket($1, recorded_at) AS bucket,
       AVG(ph_value) AS avg_ph,
       AVG(temperature) AS avg_temp,
       AVG(ec_value) AS avg_ec
FROM sensor_readings
WHERE sensor_id = $2
  AND recorded_at >= $3 AND recorded_at <= $4
GROUP BY bucket
ORDER BY bucket DESC
```

---

### TRACK E — WebSocket Gateway

---

#### Task E-1: NestJS WebSocket Gateway
**File tạo mới:** `aeroponics-backend/src/gateway/events.gateway.ts`

**Class `EventsGateway` — Hàm cần triển khai:**

| Hàm | Decorator | Mô tả |
|---|---|---|
| `handleConnection` | `@WebSocketServer()` | Log khi client connect |
| `handleDisconnect` | — | Log khi client disconnect |
| `emitRelayUpdate` | Public method | Emit event 'relay_update' tới tất cả connected clients |
| `emitSensorUpdate` | Public method | Emit event 'sensor_update' tới tất cả connected clients |
| `handleSubscribeRelay` | `@SubscribeMessage('subscribe_relay')` | Client subscribe specific relay_id |

**Event Payload Schema:**
```typescript
// relay_update event
{ relay_id: number, state: string, phase_remaining_s: number, mode: string, timestamp: string }

// sensor_update event
{ sensor_id: string, ph_value: number, ec_value: number, temperature: number, recorded_at: string }
```

---

## 4. TIÊU CHUẨN RÀ SOÁT CỨNG (Sprint 3 Hardened Rules)

### Rule S3-TUYA-01: Local Key Không Lưu Log
```
PASS: TUYA_LOCAL_KEY chỉ đọc từ .env, không in ra log bao giờ
FAIL: Logger.info / console.log in ra giá trị của localKey
FAIL: localKey xuất hiện trong stack trace hoặc error message
```

### Rule S3-DB-02: Không Dùng synchronize:true trong Production
```
PASS: TypeORM config có synchronize: false
PASS: Schema changes chỉ qua TypeORM migrations
FAIL: synchronize: true trong bất kỳ môi trường nào trừ local dev test
```

### Rule S3-HYPER-03: TimescaleDB Hypertable Bắt Buộc
```
PASS: relay_events và sensor_readings được tạo bằng create_hypertable()
PASS: Chunk interval: relay_events = 1 day, sensor_readings = 1 hour
FAIL: Tạo table bình thường mà không gọi create_hypertable
```

### Rule S3-DTO-04: Validation Bắt Buộc cho Mọi DTO
```
PASS: Mọi Controller DTO dùng class-validator: @IsNumber(), @IsString(), @IsDateString()
PASS: ValidationPipe được enable global trong main.ts với whitelist:true, forbidNonWhitelisted:true
FAIL: Controller nhận raw body object mà không validate
```

### Rule S3-WS-05: WebSocket Event Naming Convention
```
PASS: Event names là snake_case string: 'relay_update', 'sensor_update'
PASS: Payload có timestamp field ISO 8601
FAIL: Event emitted mà không có timestamp
FAIL: Dùng dynamic/arbitrary event names không được document trong MQTT_TOPICS.md
```

### Rule S3-ERR-06: NestJS Global Exception Filter
```
PASS: HttpExceptionFilter implement tất cả lỗi, trả về { statusCode, message, timestamp, path }
PASS: Tất cả unhandled Promise rejection được catch và log qua NestJS Logger
FAIL: console.error hoặc console.log trong production code
```

### Rule S3-TUYA-07: Tuya Connection Không Block Process
```
PASS: tuyapi connect trong try/catch, failure trigger backoff retry mà không crash process
PASS: setInterval polling không throw uncaught exception
FAIL: Uncaught error từ tuyapi crash toàn bộ bridge process
```

---

*Sprint 3 Planning — Khởi tạo bởi Baseline Agent ngày 2026-07-30*
*Thực thi: Sprint 3 Implementation Agent (phụ thuộc Sprint 2 complete)*
