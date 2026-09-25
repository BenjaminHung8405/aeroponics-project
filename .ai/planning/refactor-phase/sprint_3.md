# Sprint 3: Backend Ingestion & Admission Pipeline (NestJS & TimescaleDB)

> **Phụ thuộc:** Sprint 2 PASS (Virtual FSM + Safety Timers trên Gateway).
> **Output bàn giao:** MQTT topic namespace `aeroponics/v1/...` hoạt động, Retain tắt trên topic giao dịch, DB safety lock UC-BE-10 hoạt động, TimescaleDB batch ingestion tối ưu, 273/273 unit tests PASS.
> **Golden Baseline tham chiếu:** [`docs/interface-wire-contract.md`](../../docs/interface-wire-contract.md) §7–§9, [`docs/STATE_MACHINE_MATRIX.md`](../../docs/STATE_MACHINE_MATRIX.md) §5.

---

## 1. PHẠM VI & MỤC TIÊU

### 1.1 Modules / Components bị tác động trực tiếp

| Module / File | Hành động | Mô tả |
|---|---|---|
| `aeroponics-backend/src/mqtt/mqtt.service.ts` | **Sửa** | Chuẩn hóa topic regex routing, tắt Retain trên publish transactional |
| `aeroponics-backend/src/mqtt/mqtt.constants.ts` | **Sửa** | Cập nhật topic patterns theo namespace `aeroponics/v1/...` |
| `aeroponics-backend/src/mqtt/mqtt-router.service.ts` | **Sửa** | Thêm adapter layer map `node/{nodeId}` ↔ `device/{device_id}` |
| `aeroponics-backend/src/flow/flow.service.ts` | **Sửa** | Thêm DB safety lock (UC-BE-10) trước khi record flow event |
| `aeroponics-backend/src/flow/entities/flow_event.entity.ts` | **Sửa** | Thêm TimescaleDB hypertable hints (hypertable-specific indexes) |
| `aeroponics-backend/src/node/entities/sensor_calibration.entity.ts` | **Sửa** | Thêm method `isActive()` helper |
| `aeroponics-backend/src/database/database.module.ts` | **Sửa** | Thêm dedicated read/write connection pools cho TimescaleDB |
| `aeroponics-backend/src/database/typeorm.config.ts` | **Sửa** | Config connection pool params riêng cho batch inserts |
| `aeroponics-backend/src/pump-command/pump-command.service.ts` | **Sửa** | Thêm calibration ACTIVE check trước khi dispatch command |
| `aeroponics-backend/src/websocket/events.gateway.ts` | **Sửa** | Broadcast `flow_confirmed` event qua native WS khi evidence pipeline đạt FLOW_CONFIRMED |
| `aeroponics-backend/src/flow/flow.service.spec.ts` | **Sửa** | Thêm test cho UC-BE-10 safety lock |
| `aeroponics-backend/src/mqtt/mqtt.service.spec.ts` | **Sửa** | Thêm test namespace routing + retain policy |

### 1.2 Mục tiêu cụ thể Sprint 3

- [ ] Topic namespace chuẩn hóa: `aeroponics/v1/node/{nodeId}/command|ack|telemetry|event`.
- [ ] Transactional topics (`command`, `ack`, `event`, `telemetry`) publish với `retain: false` BẮT BUỘC.
- [ ] LWT/Status topics vẫn giữ `retain: true`.
- [ ] UC-BE-10: Controller block pump command nếu `SensorCalibration.status != ACTIVE` cho node đó.
- [ ] TimescaleDB Connection Pool: Dedicated write pool cho flow events (max 10 connections), read pool cho queries (max 20 connections).
- [ ] Batch Insert cho flow events: Buffer tối đa 50 events hoặc flush mỗi 5s, tránh row-level lock trên hypertable.
- [ ] MQTT `handleMessage` không crash event loop trên malformed payload (với outer try/catch đã có).
- [ ] `aeroponics/v1/...` và `aeroponics/device/{device_id}/...` là logical alias — KHÔNG publish đồng thời 2 namespace.

---

## 2. KIẾN TRÚC & LUỒNG DỮ LIỆU

### 2.1 Luồng MQTT Topic Namespace Standardization

```text
┌─────────────────────────────────────────────────────────────────┐
│                    MQTT TOPIC NAMESPACES                        │
│                                                                 │
│  PRODUCTION (v1) — Target namespace:                           │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ aeroponics/v1/node/{nodeId}/command     (non-retained)  │  │
│  │ aeroponics/v1/node/{nodeId}/ack         (non-retained)  │  │
│  │ aeroponics/v1/node/{nodeId}/telemetry   (non-retained)  │  │
│  │ aeroponics/v1/node/{nodeId}/event       (non-retained)  │  │
│  │ aeroponics/v1/gateway/heartbeat         (non-retained)  │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
│  LEGACY (device) — Current namespace, kept as alias:           │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ aeroponics/device/{device_id}/ack/{commandId}           │  │
│  │ aeroponics/device/{device_id}/telemetry                 │  │
│  │ aeroponics/device/{device_id}/status         (retained) │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
│  ALIAS MAPPING (mqtt-router.service.ts):                      │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ When Gateway publishes on v1/ namespace,                 │  │
│  │ router also emits equivalent event on device/ namespace  │  │
│  │ for backward-compatible subscribers.                     │  │
│  │ NEVER publish both simultaneously on same message.       │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
│  SUBSCRIBE (backend receives from gateway):                    │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ aeroponics/v1/node/+/ack                                 │  │
│  │ aeroponics/v1/node/+/telemetry                           │  │
│  │ aeroponics/v1/node/+/event                               │  │
│  │ aeroponics/v1/gateway/+/heartbeat                        │  │
│  │ aeroponics/device/+/status          (LWT, retained)     │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 Luồng DB Safety Lock (UC-BE-10)

```text
[MQTT Command arrives: SET_PUMP with nodeId=4, desired_state=ON]
        │
        ▼
[pump-command.service.ts: dispatchPumpCommand(dto)]
        │
        ├── Step 1: Validate node_id ∈ [4..7] (existing)
        │
        ├── Step 2 [NEW — UC-BE-10]:
        │     │ Query: SensorCalibration.findOne({
        │     │   where: { node_id: dto.nodeId, status: CalibrationStatusEnum.ACTIVE }
        │     │ })
        │     │
        │     ├── if (!activeCalibration):
        │     │     throw BadRequestException(
        │     │       `UC-BE-10 BLOCKED: Node ${dto.nodeId} has no ACTIVE sensor calibration.`
        │     │       + ` Pump command rejected. Please calibrate flow sensor first.`
        │     │       );
        │     │     → Publish REJECTED ACK with reason
        │     │     → Return false
        │     │
        │     └── if (activeCalibration exists):
        │           → Continue to Step 3
        │
        ├── Step 3: Check NodeRegistry calibration_status
        │     │ nodeRegistry.calibration_status == CALIBRATED?
        │     │   → If not: REJECT command, publish safety audit event
        │     │   → If yes: continue
        │
        ├── Step 4: Create MQTT command payload
        │
        └── Step 5: Publish to aeroponics/v1/node/{nodeId}/command
              (retain: false)
```

### 2.3 Luồng TimescaleDB Batch Ingestion cho Flow Events

```text
┌─────────────────────────────────────────────────────────────────┐
│                 FLOW EVENT BATCH PIPELINE                       │
│                                                                 │
│  [MQTT telemetry arrives with flow data]                        │
│        │                                                        │
│        ▼                                                        │
│  [flow.service.ts: recordFlowEvent(dto)]                       │
│        │                                                        │
│        ├── Validate dto (existing logic)                        │
│        │                                                        │
│        ├── Check UC-BE-10 (calibration ACTIVE)                  │
│        │                                                        │
│        ├── Create FlowEvent entity                              │
│        │                                                        │
│        └── [NEW] Add to batch buffer:                           │
│              │                                                  │
│              ▼                                                  │
│  ┌──────────────────────────────────────────┐                  │
│  │  FlowEventBatchBuffer (in-memory)        │                  │
│  │  - Buffer: FlowEvent[] (max 50 items)    │                  │
│  │  - Timer: flush every 5000ms             │                  │
│  │  - Trigger: flush when buffer.length >= 50│                 │
│  │                                          │                  │
│  │  On flush:                               │                  │
│  │  ┌────────────────────────────────────┐  │                  │
│  │  │ Dedicated Write Connection Pool    │  │                  │
│  │  │ (max: 10 connections)              │  │                  │
│  │  │                                    │  │                  │
│  │  │ INSERT INTO flow_events            │  │                  │
│  │  │ VALUES (...batch), (...batch), ... │  │                  │
│  │  │ ON CONFLICT DO NOTHING             │  │                  │
│  │  └────────────────────────────────────┘  │                  │
│  │                                          │                  │
│  │  Invariant: Single INSERT per flush,     │                  │
│  │  no row-level locking per insert.        │                  │
│  │  TimescaleDB hypertable chunk append.    │                  │
│  └──────────────────────────────────────────┘                  │
│        │                                                        │
│        ▼                                                        │
│  [Emit FlowEventRecordedEvent for each event]                  │
│  [WebSocket Gateway broadcasts node_flow to UI]                │
└─────────────────────────────────────────────────────────────────┘
```

### 2.4 Luồng Retain Policy cho MQTT Topics

```text
[Backend publish flow]
        │
        ├── topic.match(/\/status$/):
        │     options = { qos: 1, retain: true }     ← LWT/Status: retained
        │
        ├── topic.match(/\/ack\/|\/command\/|\/event\/|\/telemetry$/):
        │     options = { qos: 1, retain: false }     ← Transactional: NOT retained
        │
        └── topic.match(/\/heartbeat$/):
              options = { qos: 1, retain: false }     ← Heartbeat: NOT retained

[Gateway publish flow — from firmware side]
        │
        ├── Same rules enforced in mqtt_client.cpp:
        │     MQTT_PUBLISH_RETAIN = false (config.h)
        │     MQTT_LWT_RETAIN = true (config.h)
```

---

## 3. PHÂN RÁ CHI TIẾU TÁC VỤ

### TRACK I — MQTT Topic Namespace Standardization

#### Task I-1: `aeroponics-backend/src/mqtt/mqtt.constants.ts` — Cập nhật topic patterns

- **File:** `aeroponics-backend/src/mqtt/mqtt.constants.ts`

```typescript
export const MQTT_TOPICS = {
  // V1 Production namespace (subscribe targets)
  V1_NODE_ACK: 'aeroponics/v1/node/+/ack',
  V1_NODE_TELEMETRY: 'aeroponics/v1/node/+/telemetry',
  V1_NODE_EVENT: 'aeroponics/v1/node/+/event',
  V1_GATEWAY_HEARTBEAT: 'aeroponics/v1/gateway/+/heartbeat',

  // Legacy device namespace (backward-compatible subscribe)
  DEVICE_STATUS: 'aeroponics/device/+/status',       // LWT — retained
  DEVICE_TELEMETRY: 'aeroponics/device/+/telemetry',
  DEVICE_COMMAND_ACK: 'aeroponics/device/+/command/+/ack',
  DEVICE_COMMAND_ACK_DIRECT: 'aeroponics/device/+/ack/+',
  DEVICE_SAFETY_AUDIT: 'aeroponics/device/+/safety/audit',
  DEVICE_NODE_SNAPSHOT: 'aeroponics/device/+/telemetry/node/+/snapshot',
  GATEWAY_SCAN_RESULTS: 'aeroponics/device/+/telemetry/gateway/scan_results',
} as const;

export const DEFAULT_SUBSCRIBE_TOPICS = [
  MQTT_TOPICS.V1_NODE_ACK,
  MQTT_TOPICS.V1_NODE_TELEMETRY,
  MQTT_TOPICS.V1_NODE_EVENT,
  MQTT_TOPICS.V1_GATEWAY_HEARTBEAT,
  MQTT_TOPICS.DEVICE_STATUS,
  MQTT_TOPICS.DEVICE_TELEMETRY,
  MQTT_TOPICS.DEVICE_COMMAND_ACK,
  MQTT_TOPICS.DEVICE_COMMAND_ACK_DIRECT,
  MQTT_TOPICS.DEVICE_SAFETY_AUDIT,
  MQTT_TOPICS.DEVICE_NODE_SNAPSHOT,
  MQTT_TOPICS.GATEWAY_SCAN_RESULTS,
];

// V1 publish templates
export const MQTT_V1_PUBLISH = {
  NODE_COMMAND: (nodeId: number) => `aeroponics/v1/node/${nodeId}/command`,
  NODE_ACK: (nodeId: number) => `aeroponics/v1/node/${nodeId}/ack`,
  NODE_TELEMETRY: (nodeId: number) => `aeroponics/v1/node/${nodeId}/telemetry`,
  NODE_EVENT: (nodeId: number) => `aeroponics/v1/node/${nodeId}/event`,
  GATEWAY_HEARTBEAT: (gatewayId: string) => `aeroponics/v1/gateway/${gatewayId}/heartbeat`,
} as const;

// Retain policy per topic type
export const MQTT_RETAIN_POLICY = {
  STATUS_LWT: true,       // aeroponics/v1/node/+/status, aeroponics/device/+/status
  TRANSACTIONAL: false,    // command, ack, event, telemetry
  HEARTBEAT: false,        // heartbeat (non-retained)
} as const;
```

#### Task I-2: `aeroponics-backend/src/mqtt/mqtt.service.ts` — Cập nhật routeMessage với v1 patterns

- **File:** `aeroponics-backend/src/mqtt/mqtt.service.ts`
- **Hàm bị ảnh hưởng:** `routeMessage()`, `publish()`

```typescript
// Thêm routing cho v1 namespace trong routeMessage():
private routeMessage(topic: string, payload: any): void {
  const receivedAt = new Date();

  // V1 Node ACK: aeroponics/v1/node/{nodeId}/ack
  const v1AckMatch = topic.match(/^aeroponics\/v1\/node\/(\d+)\/ack$/);
  if (v1AckMatch) {
    const nodeId = parseInt(v1AckMatch[1], 10);
    this.eventEmitter.emit(MQTT_EVENTS.COMMAND_ACK, {
      topic, nodeId, payload, receivedAt,
      schema_version: payload.schema_version,
    });
    return;
  }

  // V1 Node Telemetry: aeroponics/v1/node/{nodeId}/telemetry
  const v1TelemetryMatch = topic.match(/^aeroponics\/v1\/node\/(\d+)\/telemetry$/);
  if (v1TelemetryMatch) {
    const nodeId = parseInt(v1TelemetryMatch[1], 10);
    this.eventEmitter.emit(MQTT_EVENTS.TELEMETRY, {
      topic, nodeId, payload, receivedAt,
    });
    return;
  }

  // ... existing routing for legacy device/ namespace ...
}

// Cập nhật publish() method để enforce retain policy:
public async publish(
  topic: string,
  message: any,
  options: mqtt.IClientPublishOptions = { qos: 1 },
): Promise<void> {
  // Enforce retain policy per contract §7
  if (topic.includes('/ack/') || topic.includes('/command/') ||
      topic.includes('/event/') || topic.includes('/telemetry')) {
    options.retain = false; // Transactional topics: ALWAYS non-retained
  }
  if (topic.includes('/status') || topic.includes('/heartbeat')) {
    options.retain = true;  // LWT/Status: retained
  }
  // ... rest of existing publish logic ...
}
```

#### Task I-3: `aeroponics-backend/src/mqtt/mqtt-router.service.ts` — Alias mapping layer

- **File:** `aeroponics-backend/src/mqtt/mqtt-router.service.ts`
- **Hàm mới:** `mapV1ToDeviceAlias(nodeId: number, eventType: string)`

```typescript
/**
 * When gateway publishes on v1/ namespace, emit equivalent on device/ namespace
 * for backward-compatible subscribers. NEVER publish on both for same message.
 */
async mapV1ToDeviceAlias(nodeId: number, eventType: string, payload: any): Promise<void> {
  // Map nodeId → device_id from NodeRegistry
  const device = await this.nodeService.getDeviceForNode(nodeId);
  if (!device) return;

  switch (eventType) {
    case 'ack':
      await this.mqttService.publish(
        `aeroponics/device/${device.device_id}/ack/${payload.command_id}`,
        payload,
        { qos: 1, retain: false },
      );
      break;
    case 'telemetry':
      await this.mqttService.publish(
        `aeroponics/device/${device.device_id}/telemetry`,
        { ...payload, node_id: nodeId },
        { qos: 1, retain: false },
      );
      break;
    // ... other event types
  }
}
```

---

### TRACK J — DB Safety Lock (UC-BE-10)

#### Task J-1: `aeroponics-backend/src/pump-command/pump-command.service.ts` — Calibration ACTIVE check

- **File:** `aeroponics-backend/src/pump-command/pump-command.service.ts`
- **Hàm bị ảnh hưởng:** `dispatchPumpCommand()` (hoặc `sendPumpCommand()`)

```typescript
// THÊM trước khi dispatch command:
async validateCalibrationActive(nodeId: number): Promise<void> {
  const calibration = await this.calibrationRepo.findOne({
    where: { node_id: nodeId, status: CalibrationStatusEnum.ACTIVE },
  });

  if (!calibration) {
    throw new BadRequestException(
      `UC-BE-10 BLOCKED: Node ${nodeId} has no ACTIVE sensor calibration record.`,
    );
  }

  // Double-check: NodeRegistry calibration_status must also be CALIBRATED
  const node = await this.nodeRepo.findOne({ where: { id: nodeId } });
  if (node && node.calibration_status !== CalibrationStatus.CALIBRATED) {
    throw new BadRequestException(
      `UC-BE-10 BLOCKED: Node ${nodeId} registry calibration_status is ${node.calibration_status}, expected CALIBRATED.`,
    );
  }
}
```

- **Gọi `validateCalibrationActive()` TRƯỚC BƯỚC dispatch MQTT command.**

#### Task J-2: `aeroponics-backend/src/flow/flow.service.ts` — Calibration guard cho recordFlowEvent

- **File:** `aeroponics-backend/src/flow/flow.service.ts`
- **Hàm bị ảnh hưởng:** `recordFlowEvent()` (hoặc `recordEvent()`)

```typescript
// THÊM guard UC-BE-10 ở đầu recordFlowEvent():
async recordFlowEvent(dto: RecordFlowEventDto): Promise<FlowEvent> {
  // UC-BE-10: Reject flow events for nodes without ACTIVE calibration
  const activeCal = await this.calibrationRepo.findOne({
    where: { node_id: dto.node_id, status: CalibrationStatusEnum.ACTIVE },
  });
  if (!activeCal) {
    this.logger.warn(
      `UC-BE-10: Flow event for node ${dto.node_id} rejected — no ACTIVE calibration.`,
    );
    throw new BadRequestException(
      `Node ${dto.node_id} has no ACTIVE sensor calibration. Flow events rejected.`,
    );
  }

  // ... existing logic continues ...
}
```

#### Task J-3: `aeroponics-backend/src/node/entities/sensor_calibration.entity.ts` — Helper method

- **File:** `aeroponics-backend/src/node/entities/sensor_calibration.entity.ts`
- **Hàm mới:** `isActive()`

```typescript
/**
 * Check if this calibration record is in ACTIVE status.
 */
isActive(): boolean {
  return this.status === CalibrationStatusEnum.ACTIVE;
}
```

---

### TRACK K — TimescaleDB Batch Ingestion

#### Task K-1: `aeroponics-backend/src/database/database.module.ts` — Dedicated connection pools

- **File:** `aeroponics-backend/src/database/database.module.ts`

```typescript
// Thêm dedicated write pool cho TimescaleDB batch inserts:
TypeOrmModule.forRootAsync({
  name: 'timescaledb-write',
  imports: [ConfigModule],
  inject: [ConfigService],
  useFactory: (configService: ConfigService) => ({
    type: 'postgres' as const,
    name: 'timescaledb-write',
    url: configService.get<string>('DATABASE_URL'),
    autoLoadEntities: false,
    synchronize: false,
    extra: {
      max: 10,                    // Dedicated write pool: 10 connections
      idleTimeoutMillis: 10000,
      connectionTimeoutMillis: 5000,
    },
  }),
}),
```

- **Pool sizing rationale:**
  - Main pool: 20 connections (general queries, REST API)
  - Write pool: 10 connections (dedicated for batch inserts)
  - Total: 30 connections (well within TimescaleDB default `max_connections = 100`)

#### Task K-2: `aeroponics-backend/src/flow/flow.service.ts` — Batch buffer implementation

- **File:** `aeroponics-backend/src/flow/flow.service.ts`
- **Hàm mới:** `bufferFlowEvent()`, `flushFlowEventBatch()`, `startBatchTimer()`

```typescript
@Injectable()
export class FlowService implements OnModuleInit, OnModuleDestroy {
  // Batch buffer cho TimescaleDB inserts
  private flowEventBuffer: FlowEvent[] = [];
  private batchTimer: NodeJS.Timeout | null = null;
  private static readonly BATCH_MAX_SIZE = 50;
  private static readonly BATCH_FLUSH_INTERVAL_MS = 5000;

  constructor(
    // ... existing injections ...
    @Inject('timescaledb-write')
    private readonly writeDataSource: DataSource,
  ) {}

  onModuleInit(): void {
    this.startBatchTimer();
  }

  onModuleDestroy(): void {
    if (this.batchTimer) {
      clearInterval(this.batchTimer);
      this.batchTimer = null;
    }
    // Flush remaining buffer on shutdown
    this.flushFlowEventBatch();
  }

  /**
   * Buffer a flow event for batch insertion.
   * Flushes immediately when buffer reaches BATCH_MAX_SIZE.
   */
  async bufferFlowEvent(event: FlowEvent): Promise<void> {
    this.flowEventBuffer.push(event);
    if (this.flowEventBuffer.length >= FlowService.BATCH_MAX_SIZE) {
      await this.flushFlowEventBatch();
    }
  }

  /**
   * Flush buffered flow events to TimescaleDB using batch INSERT.
   * Uses dedicated write connection pool to avoid blocking main pool.
   */
  async flushFlowEventBatch(): Promise<void> {
    if (this.flowEventBuffer.length === 0) return;

    const batch = this.flowEventBuffer.splice(0, FlowService.BATCH_MAX_SIZE);
    try {
      const queryRunner = this.writeDataSource.createQueryRunner();
      await queryRunner.connect();

      // Single batch INSERT — avoids row-level locking per insert
      await queryRunner.query(
        `INSERT INTO flow_events
         (node_id, time, flow_rate_lpm, litres_total, is_fault, fault_code,
          flow_confirmed, sample_window_ms, sensor_calibration_id,
          delivered_volume_ml, pulse_count)
         VALUES ${batch.map((_, i) => `($${i * 11 + 1}, $${i * 11 + 2}, $${i * 11 + 3}, $${i * 11 + 4}, $${i * 11 + 5}, $${i * 11 + 6}, $${i * 11 + 7}, $${i * 11 + 8}, $${i * 11 + 9}, $${i * 11 + 10}, $${i * 11 + 11})`).join(', ')}
         ON CONFLICT DO NOTHING`,
        batch.flatMap((e) => [
          e.node_id, e.time, e.flow_rate_lpm, e.litres_total,
          e.is_fault, e.fault_code, e.flow_confirmed, e.sample_window_ms,
          e.sensor_calibration_id, e.delivered_volume_ml, e.pulse_count,
        ]),
      );

      await queryRunner.release();
      this.logger.debug(`Flushed ${batch.length} flow events to TimescaleDB`);
    } catch (err: any) {
      this.logger.error(`Batch insert failed: ${err.message}`);
      // Re-add failed batch to buffer for retry
      this.flowEventBuffer.unshift(...batch);
    }
  }

  private startBatchTimer(): void {
    this.batchTimer = setInterval(() => {
      this.flushFlowEventBatch().catch((err) => {
        this.logger.error(`Batch flush timer error: ${err.message}`);
      });
    }, FlowService.BATCH_FLUSH_INTERVAL_MS);
  }
}
```

#### Task K-3: `aeroponics-backend/src/flow/entities/flow_event.entity.ts` — TimescaleDB hypertable index

- **File:** `aeroponics-backend/src/flow/entities/flow_event.entity.ts`
- **Thay đổi:** Thêm TimescaleDB-aware index hint cho `node_id` + `time` query pattern

```typescript
// Thêm decorator hoặc migration hint:
// @Index('idx_flow_events_node_time', ['node_id', 'time'])
// TimescaleDB automatically creates indexes on hypertable partition key.
// Ensure time column is the partitioning column in CREATE HYPERTABLE.
```

---

### TRACK L — WebSocket Flow Confirmed Broadcast

#### Task L-1: `aeroponics-backend/src/websocket/events.gateway.ts` — FLOW_CONFIRMED broadcast

- **File:** `aeroponics-backend/src/websocket/events.gateway.ts`
- **Hàm mới:** `handleFlowConfirmed()`

```typescript
/**
 * Broadcast FLOW_CONFIRMED event to UI — only fires when evidence pipeline
 * reaches FLOW_CONFIRMED stage (from gateway FSM).
 */
@OnEvent('pump.command.flow_confirmed')
handleFlowConfirmed(event: PumpCommandFlowConfirmedEvent): void {
  this.broadcast('node_flow', {
    nodeId: event.nodeId,
    flowConfirmed: true,                    // ← UI uses this to show RUNNING
    flowRateLpm: event.flowRateLpm,
    commandId: event.commandId,
    confirmedAt: event.confirmedAt,
    // No optimistic UI: button only shows RUNNING after this event
  });
}
```

- **UI integration:** Dashboard `NodeCard` component subscribes to `node_flow` WebSocket event; only displays `RUNNING` status when `flowConfirmed === true` arrives from this event.

---

### TRACK M — Unit Tests

#### Task M-1: `aeroponics-backend/src/mqtt/mqtt.service.spec.ts` — Namespace routing test

- **File:** `aeroponics-backend/src/mqtt/mqtt.service.spec.ts`

```typescript
describe('MQTT Topic Namespace Routing', () => {
  it('should route v1/node/{nodeId}/ack to COMMAND_ACK event', () => {
    const spy = jest.spyOn(eventEmitter, 'emit');
    service.handleMessage('aeroponics/v1/node/4/ack',
      Buffer.from(JSON.stringify({ command_id: 'cmd-123', status: 'ACCEPTED' })));
    expect(spy).toHaveBeenCalledWith(MQTT_EVENTS.COMMAND_ACK,
      expect.objectContaining({ nodeId: 4 }));
  });

  it('should enforce retain=false on transactional publish', async () => {
    const publishSpy = jest.spyOn(client, 'publish');
    await service.publish('aeroponics/v1/node/4/ack', { status: 'OK' });
    expect(publishSpy).toHaveBeenCalledWith(
      expect.any(String), expect.any(String),
      expect.objectContaining({ retain: false }),
      expect.any(Function),
    );
  });

  it('should enforce retain=true on status/LWT publish', async () => {
    const publishSpy = jest.spyOn(client, 'publish');
    await service.publish('aeroponics/device/esp32/status', { status: 'online' });
    expect(publishSpy).toHaveBeenCalledWith(
      expect.any(String), expect.any(String),
      expect.objectContaining({ retain: true }),
      expect.any(Function),
    );
  });
});
```

#### Task M-2: `aeroponics-backend/src/flow/flow.service.spec.ts` — UC-BE-10 safety lock test

- **File:** `aeroponics-backend/src/flow/flow.service.spec.ts`

```typescript
describe('UC-BE-10 Safety Lock', () => {
  it('should reject flow event when no ACTIVE calibration exists', async () => {
    calibrationRepo.findOne.mockResolvedValue(null); // No active calibration
    await expect(
      service.recordFlowEvent({ node_id: 4, flow_rate_lpm: '1.23' } as any),
    ).rejects.toThrow(BadRequestException);
  });

  it('should allow flow event when ACTIVE calibration exists', async () => {
    calibrationRepo.findOne.mockResolvedValue({ status: 'ACTIVE' } as any);
    flowRepo.save.mockResolvedValue({ id: 1 } as any);
    // Should not throw
    await service.recordFlowEvent({ node_id: 4, flow_rate_lpm: '1.23' } as any);
  });
});
```

---

## 4. TIÊU CHUẨN RÀ SOÁT CỨNG (Sprint 3)

### Rule S3-MQTT-01: Topic Namespace — Single Source
```
PASS: Backend subscribe cả v1/ và device/ namespace (backward-compatible)
PASS: Backend publish MỘT trong hai namespace, không đồng thời cả hai
PASS: Gateway publish trên v1/ namespace, router alias emit trên device/
FAIL: Bắt gặp duplicate publish cùng message trên cả hai namespace
```

### Rule S3-MQTT-02: Retain Policy — No Ghost Data
```
PASS: Transactional topics (ack, command, event, telemetry) publish với retain: false
PASS: Status/LWT topics publish với retain: true
PASS: Subscriber mới không nhận lại giao dịch cũ (không ghost ACK/command)
FAIL: Bất kỳ transactional topic nào publish với retain: true
```

### Rule S3-DB-03: synchronize: false — Zero Schema Auto-Sync
```
PASS: TypeORM synchronize = false trong mọi config (production + development)
PASS: Mọi schema change qua TypeORM migrations
FAIL: Bật synchronize: true trong bất kỳ environment nào
```

### Rule S3-DB-04: UC-BE-10 — Calibration Lock Enforcement
```
PASS: Pump command bị reject nếu SensorCalibration.status != ACTIVE cho node
PASS: Flow event bị reject nếu SensorCalibration.status != ACTIVE cho node
PASS: REJECTED ACK có reason message rõ ràng cho client
FAIL: Pump command thành công mà không có ACTIVE calibration
FAIL: Flow event được persist mà không check calibration
```

### Rule S3-TIME-05: TimescaleDB Batch — No Row-Level Lock
```
PASS: Flow events insert theo batch (≤ 50 events/flush), single INSERT statement
PASS: Dedicated write pool (max 10 connections) không block main pool
PASS: Batch flush interval ≤ 5000ms
FAIL: Bắt gặp INSERT逐行 (row-by-row) trong loop
FAIL: Flow event insert dùng main connection pool (20 connections)
```

### Rule S3-WS-06: FLOW_CONFIRMED Only From Evidence Pipeline
```
PASS: WebSocket node_flow event với flowConfirmed=true chỉ publish khi gateway FSM đạt FLOW_CONFIRMED
PASS: UI hiển thị RUNNING chỉ sau khi nhận flowConfirmed=true
FAIL: UI hiển thị RUNNING từ command ACK mà không có flow evidence
```

---

## 5. SYNCHRONIZATION REVIEW FIXES (Backend Codebase ↔ Sprint 3 Plan)

> **Source:** Senior Dev flow synchronization review — 2026-09-25.
> **Purpose:** Bổ sung các task fixing mismatch giữa planning và codebase hiện tại. PHẢI hoàn thành trước khi Sprint 3 đánh PASS.

---

### 5.1 TRACK S — Critical Backend Sync Fixes

#### Task S-1: UC-BE-10 Calibration Gate — `pump-command.service.ts`

- **Finding:** Finding #4 — `sendCommand()` (line 155-245) checks node ID range và active season NHƯNG **never checks `SensorCalibration.status === ACTIVE`**. User có thể command pump ON trên node chưa calibrated.
- **Root cause:** Step 2 trong flow diagram (section 2.2) chưa được implement trong code.
- **Fix:**
  1. Thêm method `validateCalibrationActive(nodeId: number): Promise<void>` (đã có trong Task J-1 — phải implement)
  2. Gọi `validateCalibrationActive(dto.node_id)` **TRƯỚC** MQTT publish trong `sendCommand()`
  3. Nếu `!activeCalibration` → throw `BadRequestException` + publish REJECTED ACK `{ status: 'REJECTED', reason: 'UC-BE-10: No ACTIVE calibration' }`
  4. **KHÔNG BAO GIỜ** fallback `calibrationId = 1` khi không tìm thấy active calibration

- **Affected file:** `aeroponics-backend/src/pump-command/pump-command.service.ts`
- **Acceptance:** Rule S3-DB-04 PASS; test M-2 PASS

#### Task S-2: UC-BE-10 Calibration Guard — `flow.service.ts`

- **Finding:** Finding #4 — `flow.service.recordFlowEvent()` (line 397-400) fallback `calibrationId = activeCal?.id ?? 1` — bypasses calibration gate entirely, silently records flow events with invalid calibration ID.
- **Fix:**
  1. Thêm guard UC-BE-10 ở đầu `recordFlowEvent()` (đã có trong Task J-2 — phải implement)
  2. Nếu `!activeCal` → throw `BadRequestException`, KHÔNG fallback
  3. Gán `dto.sensor_calibration_id = activeCal.id` (sử dụng calibration vừa query được, không fallback)

- **Acceptance:** Rule S3-DB-04 PASS

#### Task S-3: MQTT Topic Namespace — Align DEFAULT_SUBSCRIBE_TOPICS

- **Finding:** Finding #5 — `DEFAULT_SUBSCRIBE_TOPICS` hiện tại:
  ```
  NODE_TELEMETRY: 'aeroponics/node/+/telemetry'  ← THIẾU v1
  ```
  Firmware (`mqtt_client.cpp` line 85) publish `aeroponics/v1/node/%u/event`.
  Backend regex `^aeroponics\/node\/([^/]+)\/(telemetry|flow|ack|fault)$` — **thiếu `v1`**.
  → Backend **never receives** gateway telemetry/flow/events.
- **Decision required:** Chọn MỘT trong hai:
  - **Option A (Recommended):** Update backend `DEFAULT_SUBSCRIBE_TOPICS` + `routeMessage` regex để match `aeroponics/v1/node/+/...` namespace. Task I-1 + I-2 đã lên plan — PHẢI implement đúng với v1 namespace.
  - **Option B:** Update firmware để publish trên non-v1 topics.
- **Fix:** Đảm bảo Task I-1 (`mqtt.constants.ts`) và Task I-2 (`mqtt.service.ts`) update subscription topics + route regex chính xác match firmware publish topics `aeroponics/v1/node/{nodeId}/event`.
- **Acceptance:** Backend nhận được MQTT messages từ firmware; test M-1 PASS

#### Task S-4: MQTT Retain Policy — Heartbeat Contradiction

- **Finding:** Finding #6 — Code `mqtt.service.ts` publish retain: `topic.includes('/heartbeat')` → `options.retain = true`. NHƯNG:
  - Wire contract §8.4: heartbeat **không retain**
  - `MQTT_RETAIN_POLICY.HEARTBEAT: false` (Task I-1)
  - Sprint 3 section 2.4 diagram line 179-180: heartbeat `retain: false`
- **Fix:**
  1. Xóa `topic.includes('/heartbeat')` khỏi `retain = true` block trong `publish()`
  2. Hoặc đổi `MQTT_RETAIN_POLICY.HEARTBEAT = true` và cập nhật wire contract — nhưng KHÔNG phù hợp vì heartbeat stale detection cần non-retained
  3. **Recommended:** Heartbeat là transactional, KHÔNG retain. Chỉ giữ retain: true cho `/status` (LWT)
- **Affected file:** `aeroponics-backend/src/mqtt/mqtt.service.ts` — method `publish()`
- **Acceptance:** Rule S3-MQTT-02 PASS; heartbeat publish với retain: false

#### Task S-5: Admission ACK vs RF_ACKED Separation — `mqtt-router.service.ts`

- **Finding:** Finding #7 — `handleCommandAckEvent` (line 87-93):
  ```
  acked = ['ACCEPTED','COMPLETED','OK','RF_ACKED'].includes(status)
  ```
  Includes `ACCEPTED` → UI ngay lập tức show "Đã nhận lệnh (RF)" (RF_ACKED) TRƯỚC KHI có RF transmission hoặc gateway ACK.
  Wire contract §9: `ACCEPTED != RF_ACKED != PUMP_RUNNING != FLOW_CONFIRMED`
- **Fix:**
  1. `handleCommandAckEvent`: chỉ set `acked = true` khi `payload.acked === true` hoặc `status === 'RF_ACKED'`
  2. `ACCEPTED` → emit `MQTT_EVENTS.COMMAND_ACCEPTED` event (lifecycle admission, không phải RF acknowledgment)
  3. Thêm type `CommandAcceptedEvent` riêng, KHÔNG conflated với `CommandAckEvent`
  4. Update `handleNodeAck`: chỉ set outcome = `RF_ACKED` khi status thực sự = `RF_ACKED`
- **Affected file:** `aeroponics-backend/src/mqtt/mqtt-router.service.ts`
- **Acceptance:** `ACCEPTED` emission không set outcome = `RF_ACKED`

#### Task S-6: MQTT Publish Retain Substring Matching — Robust Topic Classification

- **Finding:** Finding #16 — `topic.includes('/ack/')` có thể false-positive trên `/command/{id}/ack` (contains both `/command/` and `/ack/`).
- **Fix:**
  1. Dùng regex matching thay vì `includes()`:
     ```typescript
     const isTransactional = /\/(ack|command|event|telemetry)(\/|$)/.test(topic);
     const isStatus = /\/status(\/|$)/.test(topic);
     const isHeartbeat = /\/heartbeat(\/|$)/.test(topic);
     ```
  2. Ưu tiên status > heartbeat > transactional (status wins nếu topic matches nhiều rule)
  3. Default: `retain: false` nếu không match rule nào (fail-safe)
- **Acceptance:** Command topic `aeroponics/v1/node/4/command` KHÔNG bị retain: true

#### Task S-7: command_id UUID Validation — `mqtt-router.service.ts`

- **Finding:** Finding #13 — `handleNodeAck` (line 280-285) checks `UUID_REGEX.test(commandId)` → reject non-UUID → set `outcome = FAULT_NO_ACK`. Firmware test gửi `command_id: 'rf-cmd-1'` → backend reject → pipeline broken.
- **Fix:**
  1. Relax `command_id` validation: chấp nhận string không rỗng, KHÔNG yêu cầu UUID format
  2. Hoặc update firmware test để generate UUID
  3. **Recommended:** Backend linh hoạt hơn — validate presence (không null/empty) thay vì format
- **Acceptance:** `command_id: 'rf-cmd-1'` được xử lý đúng; outcome = `ACCEPTED` thay vì `FAULT_NO_ACK`

---

### 5.2 TRACK T — Moderate Backend Sync Fixes

#### Task T-1: PumpControl Endpoint & DTO Alignment

- **Finding:** Finding #3/#11/#17 — Plan Sprint 4 Task P-3 sử dụng `POST /pump-command/override` + `{nodeId, desired_state: 'ON', run_lease_ms}`. Code backend:
  - Endpoint: `POST api/node/:nodeId/override`
  - DTO: `SendPumpCommandDto` với `action` (`'ON'|'OFF'`), `node_id`, `run_lease_ms`
  - `desired_state` KHÔNG tồn tại trong DTO
- **Fix:**
  1. **Option A (Recommended):** Update PumpControl UI (Task P-3) để match backend:
     ```typescript
     api.post(`/api/node/${nodeId}/override`, {
       action: 'ON',     // hoặc 'OFF'
       node_id: nodeId,
       run_lease_ms: 60000,
     });
     ```
  2. **Option B:** Tạo REST wrapper endpoint `/pump-command/override` trong backend với DTO adapter
  3. Nếu Option B, thêm Task T-1B trong Sprint 3
- **Acceptance:** PumpControl button click thành công 200, KHÔNG 400

#### Task T-2: Flow Event Batch Emit Timing — Align with §2.3 Diagram

- **Finding:** Finding #14 — `recordFlowEvent()` emit `FlowEventRecordedEvent` ngay lập tức khi record, NHƯNG plan §2.3 diagram yêu cầu emit SAU batch flush.
- **Fix:** Để phù hợp Sprint 4 UX requirement (RUNNING badge xuất hiện nhanh):
  1. **Giữ emit ngay lập tức** — không cần batch delay cho WS event (UX tốt hơn)
  2. NHƯNG chỉ emit event SAU khi batch buffer đã được confirm flush (flush thành công)
  3. Hoặc thêm flag `emitAfterFlush: boolean` option cho `bufferFlowEvent()` — khi true thì delay emit
- **Acceptance:** Sprint 4 UX không bị ảnh hưởng; batch isolation vẫn maintain

---

### 5.3 TRACK U — Minor Backend Sync Fixes

#### Task U-1: Retain Policy Diagram — §2.4 Correction

- **Finding:** Finding #12 (minor) — §2.4 retain flow diagram line 179-180 cần sửa:
  ```
  topic.match(/\/heartbeat$/):
    options = { qos: 1, retain: false }     ← Heartbeat: NOT retained
  ```
  Code hiện tạiretain: true` cho heartbeat. Diagram đúng, code sai. Fix: sửa code theo diagram.
- **Acceptance:** Code match diagram

#### Task U-2: AGU_LEGACY_NODE_IDS Verification

- **Finding:** Finding #20 — `node_topology.ts AGU_LEGACY_NODE_IDS = [4,5,6,7]` nhưng production IDs là `1..4`. Known blocker.
- **Note:** Đã documented trong wire contract §6 item 163. Đánh dấu `BLOCKED — cần quyết định production IDs trước khi Sprint 3 Task I-1 (topic patterns) hoạt động đúng`.

---

### 5.4 Additional Hardening Rules (Sprint 3)

#### Rule S3-MQTT-07: Retain Policy Granularity
```
PASS: Heartbeat topic publish với retain: false (không bị stale data)
PASS: Chỉ /status (LWT) giữ retain: true
PASS: topic classification dùng regex, không dùng fragile substring includes()
FAIL: Heartbeat giữ retain: true → ghost state cho subscriber mới
FAIL: /command/{id}/ack bị classify nhầm là retain: true
```

#### Rule S3-PUMP-08: Endpoint Consistency
```
PASS: PumpCommand UI dùng endpoint POST /api/node/:nodeId/override với DTO {action, node_id, run_lease_ms}
PASS: Không có endpoint duplicate /pump-command/override với body schema khác
FAIL: UI gửi POST /pump-command/override → 404 hoặc 400 Bad Request
```

#### Rule S3-MQTT-09: ACK Lifecycle Separation
```
PASS: Admission ACK (ACCEPTED) KHÔNG map sang RF_ACKED outcome
PASS: RF_ACKED chỉ set khi gateway publish status: RF_ACKED
PASS: command_id validation chấp nhận non-UUID strings
FAIL: ACCEPTED status → UI hiển thị "Đã nhận lệnh (RF)" ngay lập tức
```

---

*Sprint 3 Planning — Backend Ingestion & Admission Pipeline. Golden Baseline: Interface & Wire Contract Rev 3, UC-BE-10.*
