# Sprint 3: NestJS Backend (MQTT + Tuya Bridge + TimescaleDB)

> **Phụ thuộc:** Sprint 2 hoàn thành — Mosquitto đang chạy, ESP32 publish heartbeat + relay telemetry.  
> **Output bàn giao:** NestJS Backend hoàn chỉnh: (1) Subscribe MQTT + lưu TimescaleDB, (2) Tuya Bridge poll PH-W218, (3) REST API đầy đủ, (4) WebSocket Gateway push realtime.

> **Chiến lược:** Tái sử dụng tối đa boilerplate từ `mushroom-cp/mushroom-backend/src/`:
> - ✅ `database/` → TypeORM config + DatabaseModule (copy & adapt)
> - ✅ `mqtt/mqtt.module.ts` → Adapt sang MQTT topics Aeroponics
> - ✅ `auth/` → Giữ nguyên pattern JWT
> - ✅ `config/` → AppConfigModule pattern
> - 🆕 `relay/` → Module mới (thay thế `batch/` + `tuning/`)
> - 🆕 `sensor/` → Module mới (thay thế `telemetry/` + `influx/`)
> - 🆕 `tuya-bridge/` → Module mới (tích hợp `tuyapi`)
> - ❌ Bỏ: `influx/`, `analytics/`, `offline-sync/`, `batch/`, `tuning/`

---

## 1. PHẠM VI & MỤC TIÊU

### 1.1 Files bị tác động

| File | Loại | Mô tả |
|---|---|---|
| `src/app.module.ts` | App | Import đủ modules |
| `src/main.ts` | App | Bootstrap NestJS với ServeStatic |
| `src/config/config.module.ts` | Config | Env validation với @nestjs/config |
| `src/database/database.module.ts` | DB | TypeORM → TimescaleDB (from mushroom-cp) |
| `src/database/database.service.ts` | DB | Connection helper (from mushroom-cp) |
| `src/database/typeorm.config.ts` | DB | DataSource config (from mushroom-cp, adapt paths) |
| `src/database/migrations/` | DB | TypeORM migrations (nếu không dùng schema.sql) |
| `src/mqtt/mqtt.module.ts` | MQTT | Module adapter (from mushroom-cp) |
| `src/mqtt/mqtt.service.ts` | MQTT | Subscribe + route messages (adapt từ mushroom-cp) |
| `src/relay/relay.module.ts` | Relay | Module |
| `src/relay/relay.service.ts` | Relay | Business logic + TimescaleDB queries |
| `src/relay/relay.controller.ts` | Relay | REST endpoints |
| `src/relay/entities/relay-event.entity.ts` | Relay | TypeORM entity |
| `src/relay/entities/relay-profile.entity.ts` | Relay | TypeORM entity |
| `src/sensor/sensor.module.ts` | Sensor | Module |
| `src/sensor/sensor.service.ts` | Sensor | TimescaleDB queries |
| `src/sensor/sensor.controller.ts` | Sensor | REST endpoints |
| `src/sensor/entities/sensor-reading.entity.ts` | Sensor | TypeORM entity |
| `src/device/device.module.ts` | Device | Module |
| `src/device/device.service.ts` | Device | Upsert device_status |
| `src/device/device.controller.ts` | Device | GET /device/:id/status |
| `src/device/entities/device-status.entity.ts` | Device | TypeORM entity |
| `src/tuya-bridge/tuya-bridge.module.ts` | Tuya | Module |
| `src/tuya-bridge/tuya-bridge.service.ts` | Tuya | tuyapi polling loop |
| `src/events/events.gateway.ts` | WS | WebSocket gateway |
| `src/events/events.module.ts` | WS | Module |

### 1.2 Mục tiêu Sprint 3

- [ ] TypeORM kết nối TimescaleDB thành công, entities sync với schema.
- [ ] MQTT subscribe nhận relay telemetry từ ESP32 → lưu `relay_events`.
- [ ] MQTT subscribe nhận device heartbeat → upsert `device_status`.
- [ ] Tuya Bridge poll PH-W218 mỗi 10s → parse DPs → lưu `sensor_readings`.
- [ ] WebSocket Gateway push `relay_update`, `sensor_update`, `device_status` realtime.
- [ ] REST API đầy đủ (xem Mục 3).
- [ ] Serve `aeroponics-ui/index.html` từ `/`.

---

## 2. KIẾN TRÚC & LUỒNG DỮ LIỆU

### 2.1 Module Dependency Graph

```
AppModule
  │
  ├── AppConfigModule          (env validation)
  │
  ├── DatabaseModule           (TypeORM → TimescaleDB)
  │       └── Export: DataSource
  │
  ├── MqttModule               (MQTT client + router)
  │       ├── Inject: RelayService, SensorService, DeviceService, EventsGateway
  │       └── on_message → route → save + broadcast
  │
  ├── RelayModule
  │       ├── RelayController  (REST)
  │       ├── RelayService     (DB + MQTT publish command)
  │       └── Entities: RelayEvent, RelayProfile
  │
  ├── SensorModule
  │       ├── SensorController (REST)
  │       ├── SensorService    (DB queries)
  │       └── Entities: SensorReading
  │
  ├── DeviceModule
  │       ├── DeviceController (REST)
  │       ├── DeviceService    (upsert device_status)
  │       └── Entities: DeviceStatus
  │
  ├── TuyaBridgeModule
  │       └── TuyaBridgeService (tuyapi polling → MQTT publish)
  │
  └── EventsModule
          └── EventsGateway   (WebSocket @WebSocketGateway)
```

### 2.2 Luồng Tuya → MQTT → TimescaleDB → WebSocket

```
[Tuya PH-W218] ──Local Network──▶ TuyaBridgeService
                                         │
                                  device.get('dps')
                                  mỗi TUYA_POLL_INTERVAL_MS
                                         │
                                  parse_dps() → SensorReadingDto
                                         │
                            mqtt.publish('aeroponics/sensor/ph-w218-01/reading')
                                         │
                                   [Mosquitto]
                                         │
                                  MqttService.onMessage()
                                         │
                              SensorService.saveReading()
                                         │
                        ┌───────────────┴────────────────┐
                        ▼                                ▼
                 [TimescaleDB]              EventsGateway.emit('sensor_update')
                 sensor_readings                         │
                                               [Dashboard WebSocket clients]
```

### 2.3 Luồng ESP32 Telemetry → Database → WebSocket

```
[ESP32] ──MQTT──▶ [Mosquitto] ──▶ MqttService.handleRelayTelemetry()
                                           │
                                  RelayService.saveEvent()
                                           │
                       ┌───────────────────┴──────────────────┐
                       ▼                                       ▼
                [TimescaleDB]                    EventsGateway.emit('relay_update')
                relay_events                                   │
                                                    [Dashboard WebSocket clients]
```

### 2.4 Tuya DP Mapping PH-W218

```
DP Code → Field      → Transform  → Unit    → Range
──────────────────────────────────────────────────────
DP 101  → ph_value   → ÷ 10      → pH      → [0, 14]
DP 102  → ec_value   → × 1       → µS/cm   → [0, 10000]
DP 103  → tds_value  → × 1       → ppm     → [0, 9999]
DP 104  → temperature→ ÷ 10      → °C      → [-10, 50]
DP 105  → salinity   → ÷ 10      → ppt     → [0, 100]
DP 106  → orp_value  → × 1       → mV      → [-2000, 2000]
DP 107  → turbidity  → ÷ 10      → NTU     → [0, 1000]
```

---

## 3. PHÂN RÃ CHI TIẾT TÁC VỤ

### TRACK A — Tái Sử Dụng từ `mushroom-cp`

---

#### Task A-1: Copy & Adapt `database/`

**Source:** `mushroom-cp/mushroom-backend/src/database/`  
**Target:** `aeroponics-backend/src/database/`

**Files cần copy:**
- `database.module.ts` — giữ nguyên hoàn toàn
- `database.service.ts` — giữ nguyên hoàn toàn
- `typeorm.config.ts` — **chỉ sửa path** `entities` và `migrations`

**Thay đổi trong `typeorm.config.ts`:**
```typescript
// Chỉ thay đổi paths
entities: [path.join(__dirname, '/../**/*.entity{.ts,.js}')],
migrations: [path.join(__dirname, '/migrations/[0-9]*{.ts,.js}')],
// Xóa bỏ logic load .env từ ../../.env (Docker đã inject env vars)
```

---

#### Task A-2: Copy & Adapt `config/`

**Source:** `mushroom-cp/mushroom-backend/src/config/`  
**Adapter:** Bỏ InfluxDB validation, thêm Tuya env vars

**Env vars cần validate:**
```typescript
const configSchema = Joi.object({
  NODE_ENV: Joi.string().valid('development', 'production', 'test').default('development'),
  PORT: Joi.number().default(3001),
  DATABASE_URL: Joi.string().required(),
  MQTT_HOST: Joi.string().required(),
  MQTT_PORT: Joi.number().default(1883),
  MQTT_USERNAME: Joi.string().required(),
  MQTT_PASSWORD: Joi.string().required(),
  JWT_SECRET: Joi.string().min(32).required(),
  TUYA_DEVICE_IP: Joi.string().ip().required(),
  TUYA_DEVICE_ID: Joi.string().required(),
  TUYA_LOCAL_KEY: Joi.string().length(16).required(),
  TUYA_SENSOR_ID: Joi.string().default('ph-w218-01'),
  TUYA_POLL_INTERVAL_MS: Joi.number().default(10000),
});
```

---

#### Task A-3: Copy & Adapt `mqtt/`

**Source:** `mushroom-cp/mushroom-backend/src/mqtt/mqtt.service.ts` (file 44KB!)  
**Strategy:** Dùng làm tham chiếu kiến trúc, viết lại `mqtt.service.ts` mới gọn hơn cho domain Aeroponics.

**`mqtt.service.ts` — Hàm cần implement:**

```typescript
@Injectable()
export class MqttService implements OnModuleInit, OnModuleDestroy {
  private client: mqtt.MqttClient;

  onModuleInit(): void {
    // Connect MQTT với credentials từ ConfigService
    // Đăng ký các subscriptions
  }

  private subscribe(): void {
    this.client.subscribe([
      'aeroponics/device/+/status',           // Heartbeat + LWT
      'aeroponics/device/+/telemetry/#',       // Relay state changes
      'aeroponics/sensor/+/reading',           // Tuya sensor data (self-published)
    ]);
  }

  private onMessage(topic: string, payload: Buffer): void {
    // Router pattern (tham khảo mushroom-cp mqtt.service.ts)
    const parts = topic.split('/');
    if (topic.match(/device\/.+\/status/)) this.handleDeviceStatus(parts[2], payload);
    else if (topic.match(/device\/.+\/telemetry\/relay\/.+/)) this.handleRelayTelemetry(...);
    else if (topic.match(/sensor\/.+\/reading/)) this.handleSensorReading(parts[2], payload);
  }

  publish(topic: string, payload: object, opts?: mqtt.IClientPublishOptions): void {
    this.client.publish(topic, JSON.stringify(payload), opts);
  }

  onModuleDestroy(): void {
    this.client?.end();
  }
}
```

---

### TRACK B — TypeORM Entities

---

#### Task B-1: `relay/entities/relay-profile.entity.ts`

```typescript
@Entity('relay_profiles')
export class RelayProfile {
  @PrimaryColumn({ type: 'smallint' })
  relayId: number;  // 1-4

  @Column({ length: 50, default: '' })
  displayName: string;

  @Column({ name: 'spray_day_s', default: 30 })
  sprayDayS: number;

  @Column({ name: 'cooldown_day_s', default: 300 })
  cooldownDayS: number;

  @Column({ name: 'spray_night_s', default: 30 })
  sprayNightS: number;

  @Column({ name: 'cooldown_night_s', default: 600 })
  cooldownNightS: number;

  @UpdateDateColumn({ name: 'updated_at' })
  updatedAt: Date;
}
```

#### Task B-2: `relay/entities/relay-event.entity.ts`

```typescript
@Entity('relay_events')
export class RelayEvent {
  @PrimaryGeneratedColumn()
  id: number;  // TimescaleDB hypertable — không có PK mặc định, dùng composite nếu cần

  @CreateDateColumn({ name: 'time', type: 'timestamptz' })
  time: Date;  // TimescaleDB time column — KHÔNG được đặt primary key

  @Column({ name: 'relay_id', type: 'smallint' })
  relayId: number;

  @Column({ name: 'device_id', length: 64 })
  deviceId: string;

  @Column({ length: 32 })
  state: string;  // SPRAYING | COOLING_DOWN | MANUAL_ON | MANUAL_OFF | FLUSH

  @Column({ name: 'phase_remaining_s', nullable: true })
  phaseRemainingS: number | null;

  @Column({ nullable: true, length: 8 })
  mode: string | null;  // day | night

  @Column({ name: 'override_active', default: false })
  overrideActive: boolean;
}
```

> **⚠️ QUAN TRỌNG với TimescaleDB:** Hypertable không thể có `PRIMARY KEY` trên cột `time` đơn lẻ. Nếu cần PK, dùng composite `(time, relay_id)`. Trong thực tế, thường để `synchronize: false` và quản lý qua `schema.sql` + migrations.

#### Task B-3: `sensor/entities/sensor-reading.entity.ts`

```typescript
@Entity('sensor_readings')
export class SensorReading {
  @CreateDateColumn({ name: 'time', type: 'timestamptz' })
  time: Date;

  @Column({ name: 'sensor_id', length: 64 })
  sensorId: string;

  @Column({ name: 'ph_value', type: 'numeric', precision: 4, scale: 2, nullable: true })
  phValue: number | null;

  @Column({ name: 'ec_value', type: 'int', nullable: true })
  ecValue: number | null;

  @Column({ name: 'tds_value', type: 'int', nullable: true })
  tdsValue: number | null;

  @Column({ name: 'temperature', type: 'numeric', precision: 5, scale: 2, nullable: true })
  temperature: number | null;

  @Column({ name: 'salinity', type: 'numeric', precision: 6, scale: 3, nullable: true })
  salinity: number | null;

  @Column({ name: 'orp_value', type: 'int', nullable: true })
  orpValue: number | null;

  @Column({ name: 'turbidity', type: 'numeric', precision: 8, scale: 2, nullable: true })
  turbidity: number | null;
}
```

---

### TRACK C — Relay Module

---

#### Task C-1: `relay/relay.service.ts`

```typescript
@Injectable()
export class RelayService {
  constructor(
    @InjectRepository(RelayEvent) private readonly relayEventRepo: Repository<RelayEvent>,
    @InjectRepository(RelayProfile) private readonly relayProfileRepo: Repository<RelayProfile>,
    private readonly mqttService: MqttService,
    private readonly eventsGateway: EventsGateway,
  ) {}

  // Được gọi từ MqttService khi nhận relay telemetry
  async handleTelemetry(relayId: number, deviceId: string, data: RelayTelemetryDto): Promise<void> {
    const event = this.relayEventRepo.create({ relayId, deviceId, ...data });
    await this.relayEventRepo.save(event);
    this.eventsGateway.broadcast('relay_update', { relayId, ...data });
  }

  // REST: GET /relay/:id/state — lấy state mới nhất
  async getLatestState(relayId: number): Promise<RelayEvent | null> {
    return this.relayEventRepo.findOne({
      where: { relayId },
      order: { time: 'DESC' },
    });
  }

  // REST: GET /relay/:id/history?hours=24
  async getHistory(relayId: number, hours = 24): Promise<RelayEvent[]> {
    const since = new Date(Date.now() - hours * 3600 * 1000);
    return this.relayEventRepo.find({
      where: { relayId, time: MoreThanOrEqual(since) },
      order: { time: 'ASC' },
      take: 2000,
    });
  }

  // REST: GET /relay/:id/profile
  async getProfile(relayId: number): Promise<RelayProfile | null> {
    return this.relayProfileRepo.findOneBy({ relayId });
  }

  // REST: PUT /relay/:id/profile — cập nhật schedule → MQTT → ESP32
  async updateProfile(relayId: number, dto: UpdateRelayProfileDto): Promise<RelayProfile> {
    await this.relayProfileRepo.update({ relayId }, dto);
    const updated = await this.relayProfileRepo.findOneBy({ relayId });
    // Publish command cho ESP32
    this.mqttService.publish(
      `aeroponics/device/${process.env.DEVICE_ID}/command/relay/${relayId}/schedule`,
      { relay_id: relayId, ...dto },
      { qos: 1 }
    );
    return updated!;
  }

  // REST: POST /relay/:id/override
  async sendOverride(relayId: number, dto: RelayOverrideDto): Promise<void> {
    this.mqttService.publish(
      `aeroponics/device/${process.env.DEVICE_ID}/command/relay/${relayId}/override`,
      { relay_id: relayId, ...dto },
      { qos: 1 }
    );
  }
}
```

#### Task C-2: `relay/relay.controller.ts`

```typescript
@Controller('api/relay')
export class RelayController {
  // GET  /api/relay/:id/state
  // GET  /api/relay/:id/history?hours=24
  // GET  /api/relay/:id/profile
  // PUT  /api/relay/:id/profile     body: UpdateRelayProfileDto
  // POST /api/relay/:id/override    body: RelayOverrideDto
  // GET  /api/relay                 (tất cả 4 relay profiles)
}
```

**DTOs cần định nghĩa:**

```typescript
class UpdateRelayProfileDto {
  @IsInt() @Min(5) @Max(300)     sprayDayS: number;
  @IsInt() @Min(30) @Max(7200)   cooldownDayS: number;
  @IsInt() @Min(5) @Max(300)     sprayNightS: number;
  @IsInt() @Min(30) @Max(7200)   cooldownNightS: number;
}

class RelayOverrideDto {
  @IsIn(['on', 'off', 'flush', 'cancel'])  action: string;
  @IsOptional() @IsInt() @Min(1) @Max(3600) durationS?: number;
}
```

---

### TRACK D — Sensor Module

---

#### Task D-1: `sensor/sensor.service.ts`

```typescript
@Injectable()
export class SensorService {
  // Được gọi từ MqttService khi nhận sensor/+/reading
  async saveReading(sensorId: string, dto: SensorReadingDto): Promise<void>;

  // REST: GET /api/sensor/:id/latest
  async getLatest(sensorId: string): Promise<SensorReading | null>;

  // REST: GET /api/sensor/:id/history?hours=24
  async getHistory(sensorId: string, hours = 24): Promise<SensorReading[]>;
}
```

---

### TRACK E — Tuya Bridge Module

---

#### Task E-1: `tuya-bridge/tuya-bridge.service.ts`

```typescript
@Injectable()
export class TuyaBridgeService implements OnModuleInit, OnModuleDestroy {
  private device: TuyAPI;
  private pollingTimer: NodeJS.Timeout;

  onModuleInit(): void {
    // Khởi tạo TuyAPI device với TUYA_DEVICE_ID, TUYA_LOCAL_KEY, TUYA_DEVICE_IP
    // Bắt đầu polling loop
  }

  private async poll(): Promise<void> {
    try {
      const data = await this.device.get({ schema: true });
      const reading = this.parseDps(data.dps);
      // Publish lên MQTT topic 'aeroponics/sensor/ph-w218-01/reading'
      // MqttService sẽ tự subscribe và lưu vào TimescaleDB
      this.mqttService.publish(`aeroponics/sensor/${this.sensorId}/reading`, reading);
    } catch (err) {
      this.logger.warn(`Tuya poll error: ${err.message}`);
      // Không throw — tiếp tục retry ở lần poll kế tiếp
    }
  }

  private parseDps(dps: Record<string, unknown>): SensorReadingDto {
    return {
      sensorId: this.sensorId,
      phValue:     dps['101'] != null ? Number(dps['101']) / 10 : null,
      ecValue:     dps['102'] as number ?? null,
      tdsValue:    dps['103'] as number ?? null,
      temperature: dps['104'] != null ? Number(dps['104']) / 10 : null,
      salinity:    dps['105'] != null ? Number(dps['105']) / 10 : null,
      orpValue:    dps['106'] as number ?? null,
      turbidity:   dps['107'] != null ? Number(dps['107']) / 10 : null,
    };
  }

  onModuleDestroy(): void {
    clearInterval(this.pollingTimer);
    this.device?.disconnect();
  }
}
```

---

### TRACK F — WebSocket Gateway

---

#### Task F-1: `events/events.gateway.ts`

```typescript
@WebSocketGateway({
  cors: { origin: '*' },
  namespace: '/ws',
})
export class EventsGateway {
  @WebSocketServer()
  server: Server;

  // Được gọi từ RelayService, SensorService, DeviceService
  broadcast(event: string, data: unknown): void {
    this.server.emit(event, data);
  }
}
```

> **Lưu ý:** Dùng `@nestjs/platform-socket.io` hoặc `@nestjs/platform-ws`. Nếu dùng Socket.IO thì Dashboard cần `socket.io-client` CDN. Nếu dùng native WS thì Dashboard dùng `new WebSocket('/ws')`.

> **Khuyến nghị:** Dùng **native WebSocket** (`@nestjs/platform-ws`) để Dashboard Vanilla HTML kết nối qua `new WebSocket()` mà không cần thêm library.

---

### TRACK G — REST API Tổng Hợp

| Method | Path | Mô tả |
|---|---|---|
| `GET` | `/health` | Health check |
| `GET` | `/` | Serve `index.html` |
| `GET` | `/api/relay` | Tất cả 4 relay profiles |
| `GET` | `/api/relay/:id/state` | State mới nhất của relay |
| `GET` | `/api/relay/:id/history?hours=24` | Lịch sử state |
| `GET` | `/api/relay/:id/profile` | Profile cấu hình |
| `PUT` | `/api/relay/:id/profile` | Cập nhật schedule → MQTT → ESP32 |
| `POST` | `/api/relay/:id/override` | Manual override → MQTT → ESP32 |
| `GET` | `/api/sensor/:id/latest` | Sensor reading mới nhất |
| `GET` | `/api/sensor/:id/history?hours=24` | Lịch sử sensor |
| `GET` | `/api/device/:id/status` | Device online/offline status |

---

## 4. TIÊU CHUẨN RÀ SOÁT CỨNG (Sprint 3)

### Rule S3-REUSE-01: Tái Sử Dụng Từ `mushroom-cp`
```
PASS: database/, config/ được copy từ mushroom-cp và adapt
PASS: mqtt.service.ts follow pattern từ mushroom-cp
FAIL: Viết DatabaseModule từ đầu không tham chiếu mushroom-cp
```

### Rule S3-NO-INFLUX-02: Không Import InfluxDB
```
PASS: KHÔNG có @influxdata/influxdb-client trong package.json
PASS: KHÔNG có InfluxModule trong app.module.ts
FAIL: Bất kỳ reference nào đến InfluxDB
```

### Rule S3-DB-03: synchronize: false
```
PASS: typeorm.config.ts có synchronize: false
FAIL: synchronize: true trong bất kỳ môi trường nào (production)
```

### Rule S3-TUYA-04: Local Key Không Log
```
PASS: TUYA_LOCAL_KEY không bao giờ xuất hiện trong logger output
FAIL: this.logger.log(this.config.get('TUYA_LOCAL_KEY'))
```

### Rule S3-MQTT-05: onMessage Bắt Mọi Exception
```
PASS: onMessage() bọc trong try/catch, log error nhưng không throw
FAIL: Uncaught exception trong MQTT callback crash NestJS process
```

### Rule S3-WS-06: WebSocket Native (không cần Socket.IO trên client)
```
PASS: @nestjs/platform-ws với native WebSocket
PASS: Dashboard kết nối bằng new WebSocket('ws://localhost:3001/ws')
FAIL: Cần socket.io-client CDN trên Dashboard để kết nối
```

### Rule S3-DTO-07: class-validator Cho Mọi Request Body
```
PASS: Mọi PUT/POST endpoint dùng DTO với @IsInt(), @Min(), @Max() decorators
FAIL: Nhận raw body mà không validate
```

---

*Sprint 3 Planning (NestJS + TimescaleDB + Tuya Bridge) — Updated 2026-07-30*
