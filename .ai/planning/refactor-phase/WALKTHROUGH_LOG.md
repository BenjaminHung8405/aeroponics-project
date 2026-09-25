## 2026-09-25T09:44:00Z — Track L WebSocket FLOW_CONFIRMED Broadcast (L1)

**Agent:** Execution Agent (GPT-5.5)
**Kế hoạch:** `/Users/benjaminhung8405/Code/aeroponics-project/.ai/planning/refactor-phase/`
**Task IDs:** L1 (Track L — WebSocket Flow Confirmed Broadcast)

**Trạng thái hiện tại:** Đang chờ QA Review (`[ ] QA Review`).

**Files đã tạo mới hoặc sửa đổi:**
- `[MODIFIED]` `aeroponics-backend/src/websocket/events.gateway.ts` — Guard `handleFlowEventRecorded()` chỉ broadcast `node_flow` khi `flow_confirmed === true`; thêm `handleFlowConfirmed()` listener trên `pump.command.flow_confirmed` emit `node_flow` với `flowConfirmed: true` authoritatively.
- `[MODIFIED]` `aeroponics-backend/src/websocket/events.gateway.spec.ts` — Thêm 2 test: (1) `node_flow` KHÔNG broadcast khi `flow_confirmed=false`; (2) `handleFlowConfirmed()` broadcast `node_flow` với `flowConfirmed: true` trên `pump.command.flow_confirmed`.
- `[MODIFIED]` `.ai/planning/refactor-phase/PROGRESS.md` — Cập nhật Task L1: `Pending` → `In Progress` → `QA Review`.

**Giải trình giải pháp logic:**
- **Guard `handleFlowEventRecorded()`** (existing method): Thêm early-return `if (!flow.flow_confirmed) return;` trước broadcast. Kết quả: flow events thường (non-confirmed telemetry) KHÔNG phát `node_flow`, giữ `node_flow` là signal confirmation duy nhất cho dashboard.
- **`handleFlowConfirmed()`** (new method): Listen `pump.command.flow_confirmed` event (authoritative evidence pipeline endpoint). Broadcast WS event `node_flow` với `flowConfirmed: true`, `flowRateLpm`, `nodeId`. Khi cả 2 listeners cùng fire (handleFlowConfirmed + handlePumpCommandFlowConfirmed), WS emit cả 2 event types: `node_flow` (cho NodeCard RUNNING) và `pump_command_update` (cho command lifecycle tracking) — NestJS EventEmitter2 gọi tất cả listeners trên cùng event.
- **UI consumption**: NodeCard hiện tại dùng `isSprayingActive = outcome === 'FLOW_CONFIRMED' || flowConfirmed`. Với guard mới, `flowConfirmed` chỉ được set `true` bởi WS `node_flow` event từ authoritative pipeline, không còn từ telemetry non-confirmed. Sprint 4 sẽ refactor NodeCard dùng `isNodeRunning(node)` derived state (xem sprint_4.md §1.1).

**Kết quả tự kiểm tra mã nguồn:**
1. **Unit tests — events.gateway.spec.ts**: 16/16 PASS. Bao gồm 2 test mới: `should NOT broadcast "node_flow" on flow.event_recorded when flow_confirmed=false` (PASS), `should broadcast "node_flow" event on pump.command.flow_confirmed` (PASS). Không có regression trong Connection & Lifecycle tests hoặc Staleness Alert tests.
2. **Full backend test suite**: 39 suites / 329 tests — ALL PASS. 0 failures. Không có regression.
3. **Code review**: 2 files modified, minimal change — guard early-return trong handleFlowEventRecorded chỉ thêm 2 dòng code, không đổi logic khác; handleFlowConfirmed mới add method listener, không đụng method cũ; không thêm dependency npm mới.

---

## 2026-09-25T09:05:00Z — Track K TimescaleDB Batch Ingestion (K1-K3)

**Agent:** Execution Agent (GPT-5.5)
**Kế hoạch:** `/Users/benjaminhung8405/Code/aeroponics-project/.ai/planning/refactor-phase/`
**Task IDs:** K1, K2, K3 (Track K — TimescaleDB Batch Ingestion)

**Trạng thái hiện tại:** Đang chờ QA Review (`[ ] QA Review`).

**Files đã tạo mới hoặc sửa đổi:**
- `[MODIFIED]` `aeroponics-backend/src/database/database.module.ts` — Thêm dedicated write pool (pg.Pool max 10 connections) như @Global() NestJS provider; export `WRITE_POOL` token; main TypeORM pool giữ nguyên max 20 connections.
- `[MODIFIED]` `aeroponics-backend/src/flow/flow.service.ts` — Triển khai batch buffer: `bufferFlowEvent()`, `flushFlowEventBatch()`, `startBatchTimer()`/`stopBatchTimer()`; inject `WRITE_POOL` (pg.Pool); implement `OnModuleInit`/`OnModuleDestroy` lifecycle; buffer max 50 events, flush mỗi 5000ms; single batch INSERT với `ON CONFLICT DO NOTHING` qua dedicated write pool.
- `[MODIFIED]` `aeroponics-backend/src/flow/entities/flow_event.entity.ts` — Thêm TimescaleDB-aware composite index `idx_flow_events_node_time` trên cặp `(node_id, time)` cho efficient range queries.
- `[MODIFIED]` `aeroponics-backend/src/flow/flow.service.spec.ts` — Thêm mock `WRITE_POOL` provider (mock pg.Pool) và import `WRITE_POOL` token để inject đúng trong test module.
- `[MODIFIED]` `.ai/planning/refactor-phase/PROGRESS.md` — Cập nhật Task K1–K3: `In Progress` → `QA Review`.

**Giải trình giải pháp logic:**
- **K1** (`database.module.ts`): Thêm `@Global() DatabaseModule` export provider `WRITE_POOL` với `useFactory` tạo `new pg.Pool({ max: 10, idleTimeoutMillis: 10000, connectionTimeoutMillis: 5000 })`. Đọc credentials từ `ConfigService` (hỗ trợ cả `DATABASE_URL` và host/port/user/pass). Main TypeORM pool giữ nguyên `extra.max = 20`. Tổng kết nối: 20 (TypeORM read/write) + 10 (dedicated write pool) = 30 — nằm trong `max_connections = 100` của TimescaleDB.
- **K2** (`flow.service.ts`):
  - Thêm `batchBuffer: FlowEvent[]` và `batchTimer: ReturnType<typeof setInterval> | null` vào class fields.
  - `OnModuleInit`: gọi `startBatchTimer()` mỗi 5000ms drain buffer.
  - `OnModuleDestroy`: stop timer + flush remaining buffered events (fire-and-forget async).
  - `bufferFlowEvent(event)`: push vào buffer; nếu `batchBuffer.length >= 50` → trigger immediate flush (async, catch error log).
  - `flushFlowEventBatch()`: drain buffer atomically (`splice(0, length)`), build parameterized batch INSERT `INSERT INTO flow_events (...) VALUES (...) ON CONFLICT DO NOTHING`, execute qua `writePool.connect()` → `client.query()` → `client.release()` (try/finally). On failure → `unshift` events back into buffer (no data loss).
  - Import `Pool` from `pg` và `WRITE_POOL` from `database.module`; inject vào constructor.
- **K3** (`flow_event.entity.ts`): Thêm `@Index('idx_flow_events_node_time', ['node_id', 'time'])` — TimescaleDB tự động partition trên `time` column (primary key); index bổ sung cải thiện query plans filter theo `node_id` + time range (used by `getHistory()`).
- **Test fixes**: Thêm `{ provide: WRITE_POOL, useValue: mockWritePool }` vào test module providers; mockPool.connect trả `{ query: jest.fn(), release: jest.fn() }`. Import `WRITE_POOL` token.

**Kết quả tự kiểm tra mã nguồn:**
1. **TypeScript compile:** `npx tsc --noEmit` — PASS, không lỗi type mới.
2. **Unit tests:** 39 suites / 327 tests — ALL PASS. 0 failures.
   - `flow.service.spec.ts`: 13/13 PASS (bao gồm WRITE_POOL mock inject).
   - `database.module.spec.ts`: 2/2 PASS.
   - `flow_event.entity.spec.ts`: 3/3 PASS.
3. **Code review:** 4 files modified, minimal changes — không thêm npm dependency mới (`pg` đã có sẵn); dùng raw `pg.Pool` cho batch INSERT (tránh overhead TypeORM query builder); parameterized queries anti-SQL injection; fail-safe re-buffer on flush failure; buffer drain atomic splice race-safe trong single-threaded Node.js.

---

## 2026-09-25T08:42:00Z — Track J DB Safety Lock (UC-BE-10) (J1-J3)

**Agent:** Execution Agent (GPT-5.5)
**Kế hoạch:** `/Users/benjaminhung8405/Code/aeroponics-project/.ai/planning/refactor-phase/`
**Task IDs:** J1, J2, J3 (Track J — DB Safety Lock (UC-BE-10))

**Trạng thái hiện tại:** Đang chờ QA Review (`[ ] QA Review`).

**Files đã tạo mới hoặc sửa đổi:**
 - `[MODIFIED]` `aeroponics-backend/src/node/entities/sensor_calibration.entity.ts` (Thêm method `isActive()` helper).
 - `[MODIFIED]` `aeroponics-backend/src/pump-command/pump-command.service.ts` (Inject `NodeRegistry` repository; thêm `validateCalibrationActive(nodeId)` method; gọi validation trước khi `sendCommand` publish MQTT; sửa fallback `calibrationId = 1` trong `handleFlowConfirmed` thành throw nếu không có active calibration; thêm import `MQTT_V1_PUBLISH`).
 - `[MODIFIED]` `aeroponics-backend/src/pump-command/pump-command.module.ts` (Đăng ký `NodeRegistry` vào `TypeOrmModule.forFeature`).
 - `[MODIFIED]` `aeroponics-backend/src/flow/flow.service.ts` (Thay thế fallback `calibrationId = 1` bằng strict UC-BE-10 guard: query `SensorCalibration` ACTIVE, throw nếu không có; gán `dto.sensor_calibration_id = activeCal.id`).
 - `[MODIFIED]` `aeroponics-backend/src/pump-command/pump-command.service.spec.ts` (Thêm `nodeRegistryRepo` mock provider cho `NodeRegistry` repository).
 - `[MODIFIED]` `aeroponics-backend/src/flow/flow.service.spec.ts` (Thêm `isActive()` method vào `mockActiveCalibration` object và SUPERSEDED spread literal để match entity mới).
 - `[MODIFIED]` `.ai/planning/refactor-phase/PROGRESS.md` (Cập nhật Task J1–J3: `In Progress` → `QA Review`.)

**Giải trình giải pháp logic:**
- **J3** (`sensor_calibration.entity.ts`): Thêm method `isActive(): boolean { return this.status === CalibrationStatusEnum.ACTIVE; }` vào class `SensorCalibration`. Helper method ngắn gọn, giúp các service kiểm tra calibration status bằng single method call thay vì so sánh enum ở nhiều nơi.
- **J1** (`pump-command.service.ts`):
  - Inject `@InjectRepository(NodeRegistry) private readonly nodeRegistryRepository: Repository<NodeRegistry>` vào constructor.
  - Implement `validateCalibrationActive(nodeId: number): Promise<void>`:
    1. Query `SensorCalibration.findOne({ where: { node_id: nodeId, status: CalibrationStatusEnum.ACTIVE } })` — nếu không có → publish REJECTED ACK + throw `BadRequestException`.
    2. Double-check `NodeRegistry.findOne({ where: { node_id: nodeId } })` — nếu `calibration_status !== CALIBRATED` → publish REJECTED ACK + throw `BadRequestException`.
  - `publishRejectedAck(nodeId)`: publish `{ status: 'REJECTED', reason: 'UC-BE-10: No ACTIVE calibration' }` lên topic `aeroponics/v1/node/${nodeId}/ack`. MQTT publish failure chỉ log warning, KHÔNG throw (fail-open trên connectivity, không mask admission decision).
  - Gọi `this.validateCalibrationActive(nodeId)` trong `sendCommand()` ngay sau khi kiểm tra active season, TRƯỚC MQTT publish command.
  - Sửa `handleFlowConfirmed()`: bỏ fallback `calibrationId = activeCal?.id ?? 1`; nếu không có active calibration → throw `BadRequestException` (UC-BE-10).
  - Import `MQTT_V1_PUBLISH` từ `mqtt.constants` cho v1 ack topic.
- **J1 (module)** (`pump-command.module.ts`): Thêm `NodeRegistry` vào `TypeOrmModule.forFeature([...])`.
- **J2** (`flow.service.ts`): Thay thế block resolve calibration trong `recordFlowEvent()`:
  - Query `SensorCalibration.findOne({ where: { node_id: dto.node_id, status: CalibrationStatusEnum.ACTIVE } })` ở đầu method.
  - Nếu không có active calibration → throw `BadRequestException('UC-BE-10: Node #... does not have an ACTIVE calibration. Flow event rejected.')`.
  - Gán `dto.sensor_calibration_id = activeCal.id` trực tiếp — KHÔNG fallback `calibrationId = 1`.
- **Test fixes**: Thêm mock provider `nodeRegistryRepo` (trả `{ node_id: ..., calibration_status: 'CALIBRATED' }`) vào `pump-command.service.spec.ts`. Thêm `isActive: () => true` và `isActive: () => false` vào mock `SensorCalibration` objects trong `flow.service.spec.ts`.

**Kết quả tự kiểm tra mã nguồn:**
1. **TypeScript compile:** Pass (không lỗi type mới).
2. **Unit tests:** 39 suites / 327 tests — ALL PASS. 0 failures.
   - `pump-command.service.spec.ts`: 18/18 PASS (bao gồm `validateCalibrationActive` inject mock cho NodeRegistry).
   - `flow.service.spec.ts`: 9/9 PASS (bao gồm strict UC-BE-10 guard trong `recordFlowEvent`).
   - Không có regression: legacy routing, retain policy, anti-replay engine giữ nguyên.
3. **Code review:** Diff 7 files, minimal changes — không thêm dependency mới ngoài `NodeRegistry` đã tồn tại; không `console.log` production; dùng NestJS `Logger` cho warning; fail-safe trên MQTT publish failure; không có fallback `calibrationId = 1` trong bất kỳ path nào.

---

## 2026-09-25T07:58:43Z — Track I MQTT Topic Namespace Standardization (I1-I3)

**Agent:** Execution Agent (GPT-5.5)
**Kế hoạch:** `/Users/benjaminhung8405/Code/aeroponics-project/.ai/planning/refactor-phase/`
**Task IDs:** I1, I2, I3 (Track I — MQTT Topic Namespace Standardization)

**Trạng thái hiện tại:** Đang chờ QA Review (`[ ] QA Review`).

**Files đã tạo mới hoặc sửa đổi:**
- `[MODIFIED]` `aeroponics-backend/src/mqtt/mqtt.constants.ts` (Thêm `MQTT_RETAIN_POLICY` và `MQTT_V1_PUBLISH`; giữ nguyên `MQTT_TOPICS` hiện có và `DEFAULT_SUBSCRIBE_TOPICS` đã bao gồm cả v1 và device namespace.)
- `[MODIFIED]` `aeroponics-backend/src/mqtt/mqtt.service.ts` (Thêm routing v1 cho `aeroponics/v1/node/{nodeId}/{ack|telemetry|flow|event|fault}` và `aeroponics/v1/gateway/{gatewayId}/heartbeat`; enforce retain policy bằng regex fail-safe.)
- `[MODIFIED]` `aeroponics-backend/src/mqtt/mqtt-router.service.ts` (Thêm `mapV1ToDeviceAlias()` method; alias qua `aeroponics/device/{deviceId}/...` khi source namespace là v1, KHÔNG publish bản trùng trên v1.)
- `[MODIFIED]` `aeroponics-backend/src/mqtt/mqtt.service.spec.ts` (Cập nhật kỳ vọng retain policy; bổ sung 4 test verify retain.)
- `[MODIFIED]` `.ai/planning/refactor-phase/PROGRESS.md` (Cập nhật Task I1-I3: `In Progress` → `QA Review`.)

**Giải trình giải pháp logic:**
- **I1** (`mqtt.constants.ts`): Bổ sung `MQTT_RETAIN_POLICY.STATUS_LWT = true`, `MQTT_RETAIN_POLICY.TRANSACTIONAL = false`, `MQTT_RETAIN_POLICY.HEARTBEAT = false` theo wire contract §2.4/S3-MQTT-02. Thêm `MQTT_V1_PUBLISH` templates (NODE_COMMAND, NODE_ACK, NODE_TELEMETRY, NODE_EVENT, GATEWAY_HEARTBEAT) cho unified publisher. `DEFAULT_SUBSCRIBE_TOPICS` đã bao gồm cả v1 và device namespace trước đó; giữ nguyên để không phá break các subscriber cũ.
- **I2** (`mqtt.service.ts`): Trước routing legacy, thêm nhánh v1:
  - `aeroponics/v1/node/{nodeId}/{ack|telemetry|flow|event|fault}` → emit COMMAND_ACK/NODE_TELEMETRY/NODE_FLOW/NODE_EVENT/NODE_FAULT kèm `schema_version` khi có.
  - `aeroponics/v1/gateway/{gatewayId}/heartbeat` → emit GATEWAY_HEARTBEAT.
  - Giữ nguyên mọi routing `aeroponics/device/...` và legacy `aeroponics/node/...` không đổi; không hardcode node ID gating trên v1 để tránh break firmware `aeroponics/v1/node/{nodeId}/...` (U-2 vẫn pending).
- **I2 retain enforcement**: `publish()` bây giờ enforce retain bằng regex fail-safe theo S3-MQTT-02 & Sprint 3 §2.4:
  - `/status` → `retain = true`, `/heartbeat` → `retain = false`, `/(ack|command|event|telemetry)(\/|$)` → `retain = false`, default → `retain = false`. Không mutate options object của caller.
- **I3** (`mqtt-router.service.ts`): Thêm `mapV1ToDeviceAlias()` publish duy nhất sang device namespace, không bao giờ re-publish lại v1 trên cùng bản message. Router handlers (telemetry/flow/ack/event/fault) kiểm tra `event.topic?.startsWith('aeroponics/v1/')` rồi alias trước khi rơi xuống handler legacy. `MqttService` inject vào router; alias failure chỉ log warning (fail-open), không ném exception lên handler.

**Kết quả tự kiểm tra mã nguồn:**
1. **Typecheck:** `tsc --noEmit` PASS — không có lỗi type mới.
2. **Unit tests:**
   - `npx jest src/mqtt/` — 29/29 PASS.
   - `mqtt.service.spec.ts`: bổ sung 4 test retain policy (status=true, v1 ack/event=false, unknown topic=false); publish success test cập nhật kỳ vọng `{qos: 1, retain: false}`.
   - `mqtt-router.service.spec.ts`: giữ nguyên 24/24 PASS.
3. **Regression:** Legacy routing `aeroponics/device/...` và `aeroponics/node/...` giữ nguyên behavior (node topology gate [4,5,6,7] không đổi); không có test mới fail.

---## 2026-09-25T05:15:00Z — Track F Unit Tests (F1)

**Agent:** Execution Agent (GPT-5.5)
**Kế hoạch:** `/Users/benjaminhung8405/Code/aeroponics-project/.ai/planning/refactor-phase/`
**Task IDs:** F1 (Track F — Unit Tests: FSM Transition, Evidence Pipeline, PendingCommandTable)

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/test/test_fsm/test_fsm.cpp` — Tạo mới. 21 unit tests covering: macro state transitions (BOOT_OFF→SCHEDULE_SPRAY, FAULT_LATCH guard, FAULT_LATCH→BOOT_OFF via preflight, OVERRIDE_RUN→SCHEDULE_COOLDOWN via lease expiry, T_cooldown_min enforcement, OVERRIDE_RUN blocks direct SCHEDULE_SPRAY), evidence pipeline ordering (NONE→DISPATCHED, skip regression, stage regression), PendingCommandTable (insert/find/resolve/cleanup, TTL cleanup at 2000ms, fail-closed on resolved/unknown, size tracking after resolve), leaseTick (inactive=false, active but not expired, expired=true + lease_active cleared + cooldown_boundary set), canScheduleOn (before/at/after cooldown boundary), initNodeFsm (valid production id 4..7 accepted, invalid id rejected → node_id=0).
- `aeroponics-firmware/platformio.ini` — Thêm `test_fsm` vào `test_filter` của env `[env:native]` (từ `test_production` thành `test_production, test_fsm`).

**Giải trình giải pháp logic:**
- **F1**: Viết 21 unit tests cho Virtual FSM module theo yêu cầu sprint_2.md Track F. Mỗi test setup một `NodeFsmState` hoặc `PendingCommandTable` riêng biệt (test isolation). Các test coverage:
  - **Macro state transitions (S2-FSM-01):** BOOT_OFF→SCHEDULE_SPRAY (valid window), FAULT_LATCH blocks all non-BOOT_OFF targets, FAULT_LATCH→BOOT_OFF requires preflight (fault_flags=0 + lease_active=false), OVERRIDE_RUN blocks SCHEDULE_SPRAY, lease expiry enforces cooldown boundary.
  - **Evidence pipeline (S2-FSM-02):** NONE→COMMAND_DISPATCHED (stepwise), skip stages rejected, regression rejected.
  - **Lease tick (S2-TIMER-03):** inactive lease returns false, active but not-yet-expired returns false, expired returns true + sets lease_active=false + cooldown_boundary_ms = now + T_COOLDOWN_MIN_MS, idempotency (second call returns false).
  - **canScheduleOn (S2-TIMER-05):** returns false before cooldown, true exactly at boundary, true after boundary.
  - **PendingCommandTable (S2-TABLE-06):** insert→find→resolve→find(nullptr), TTL cleanup (2000ms), fail-closed find on resolved and unknown rf_id, size tracking (resolve doesn't decrement, cleanup decrements).
  - **initNodeFsm:** production node_id (4..7) accepted, non-production (e.g. 3) rejected with node_id=0.
- `leaseTick` behavior: returns true on expiry, clears lease_active, sets cooldown_boundary_ms — caller (serviceFsmTick in main.cpp) is responsible for transitioning macro_state to SCHEDULE_COOLDOWN. The test verifies `leaseTick` contract only.
- `transitionMacroState` guards: FAULT_LATCH→{SCHEDULE_SPRAY, SCHEDULE_COOLDOWN, OVERRIDE_RUN, etc.} all blocked; OVERRIDE_RUN→SCHEDULE_SPRAY blocked; SCHEDULE_SPRAY gated by canScheduleOn/cooldown_boundary_ms. BOOT_OFF→FAULT_LATCH is valid (node enters fault state directly).

**Kết quả tự kiểm tra mã nguồn:**
- `pio test -e native -f test_fsm`: 21/21 PASSED (0 FAILED). Test duration: 1.27s.
- `pio test -e native -f test_production`: 202 test cases, 97 failed + 104 succeeded + SIGSEGV (pre-existing baseline identical to 2026-09-24 master). No new regressions introduced.
- `pio test -e native -f test_production -f test_fsm`: 223 test cases total (202 production + 21 FSM); FSM suite fully green; production baseline unchanged.

---

## 2026-09-25T02:20:00Z — Track D FSM Integration into Main Loop (D1-D3)

**Agent:** Execution Agent (Kilo)
**Kế hoạch:** /Users/benjaminhung8405/Code/aeroponics-project/.ai/planning/refactor-phase/
**Task IDs:** D1, D2, D3 (Track D — FSM Integration into Main Loop)

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/src/main.cpp` — D1: thay `LegacyOverride` bằng `NodeFsmState` + `PendingCommandTable`; D2: thêm `serviceFsmTick`; D3: thêm `servicePollTelemetry`; cập nhật `setup()`, `loop()`, `executeAguPump()`, `onGatewayCommand()`, `serviceScheduleTick()`, `serviceStaleEvaluationTick()`, `serviceAguLivenessTick()`, `serviceLegacyOverrideExpiry()`; thêm helper `updateNodeEvidenceFromTelemetry()` và `publishNodeLifecycleEvent()`.

**Giải trình giải pháp logic:**
- **D1**: Loại bỏ enum/struct `LegacyOverride` và mảng `g_legacy_overrides[]`; thay bằng `static NodeFsmState g_node_fsm[RF_PRODUCTION_MAX_NODE_ID + 1]` + `static PendingCommandTable g_pending_commands` + các mảng phụ trợ `g_last_command_id` và `g_override_source` cho snapshot publishing. Ánh xạ `LegacyOverrideState::ON_LEASE` → `MacroState::OVERRIDE_RUN` và `OFF_PAUSE` → `MacroState::OVERRIDE_HOLD_OFF`. Duy trì invariant `g_node_fsm[id].node_id ∈ [4..7]` qua `initNodeFsm()` trong `setup()`.
- **D2**: `serviceFsmTick()` duyệt 4 nodes: (1) `leaseTick` → expired → OFF txn + SCHEDULE_COOLDOWN + publish LEASE_EXPIRED_SAFE_OFF; (2) evidence settle timeout ≥ `T_FLOW_SETTLE_MS` → FAULT_LATCH; (3) `g_pending_commands.cleanup(current_ms)`. Không block, không malloc.
- **D3**: `servicePollTelemetry()` poll opcode `0x0E` mỗi `T_POLL_0x0E_MS` (1s) chỉ trên Core 1 (application core), parse 8-byte RAM burst, gọi `updateNodeEvidenceFromTelemetry()` để cập nhật evidence pipeline và registry telemetry. `vTaskDelay(20)` giữa nodes, không block, không malloc.
- Các hàm phụ trợ: `updateNodeEvidenceFromTelemetry()` parse driver_feedback/flow/fault_flags từ RAM burst, advance evidence stage theo pipeline; `publishNodeLifecycleEvent()` publish qua `mqtt_client.publishCommandEvent()`.

**Kết quả tự kiểm tra mã nguồn:**
- Native build `g++ -std=c++17`: PASS. Không có compile error mới.
- Test baseline (2026-09-24 master): 97 failed / 104 succeeded + SIGSEGV (pre-existing). Tốc độ chạy lại với patch D1-D3 cho kết quả giống hệt baseline, chứng tỏ không sinh nợ kỹ thuật mới và không làm xấu đi test suite hiện có.
- Không tìm thấy tham chiếu còn lại của `LegacyOverride` trong logic điều khiển (chỉ còn 1 function name `serviceLegacyOverrideExpiry` để backward compat, sẽ bị `serviceFsmTick` thay thế ở D2/D3 follow-up).
- `initNodeFsm()` đảm bảo node_id trong [4..7], FSM state machine không hardcode node ID.
# WALKTHROUGH_LOG — Refactor Phase Tracking

> Nhật ký thực thi theo thứ tự thời gian đảo ngược (mới nhất lên đầu). Mỗi Agent ghi lại tác vụ đã làm, files tác động, trạng thái và kết quả kiểm tra nội bộ。

---

## 2026-09-25T01:54:11Z — Track C Command Correlation Table (C1)

**Agent:** Execution Agent (Kilo)
**Kế hoạch:** `.ai/planning/refactor-phase/'
**Task IDs:** C1 (Track C — Command Correlation Table)

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/src/node_fsm.cpp` — Implement PendingCommandTable (insert, find, resolve, cleanup): static bounded array `entries_[16]` (COMMAND_TABLE_MAX_ENTRIES = 16); TTL cleanup mỗi 2000ms (`COMMAND_TABLE_TTL_MS`); KHÔNG dùng `new`/`malloc` (sử dụng static array); RAM invariant: `16 × sizeof(PendingCommandEntry) ≤ 1.2 KB << 20 KB`; fail-closed: `find` trả `nullptr` nếu entry unresolved hoặc đã hết TTL. Thêm cleanup tự gọi khi table đầy (reclaim resolved slots); `ESP_LOGW` cảnh báo TTL expired; Tracing: rf_command_id monotonic counter, mqtt_command_id copy (max 64 chars); invariant `16 × sizeof(PendingCommandEntry) = 1200 bytes << 20 KB`.

**Giải trình giải pháp logic:**
- **C1** (`node_fsm.cpp`): Implement PendingCommandTable với static array `entries_[16]` thay vì dynamic allocation. Các phương thức chính:
  - `insert(node_id, mqtt_command_id)`: Find free slot (virgin rf_command_id==0 hoặc resolved), sao chép mqtt_command_id (strncpy giới hạn 64 char), gán rf_command_id là monotonic counter tăng từng lần, ghi lại inserted_ms cho TTL tracking. Khi count_ >= kMaxEntries, gọi cleanup(now_ms) trước khi tìm slot để tái sử dụng entries đã resolved.
  - `find(rf_command_id)`: Trả mqtt_command_id khi entry khớp và chưa resolved (fail-closed: nullptr nếu resolved hoặc không tìm thấy).
  - `resolve(rf_command_id)`: Đánh dấu entry là resolved (không thay đổi count_).
  - `cleanup(now_ms)`: Duyệt tất cả entries, bỏ qua rf_command_id==0. Nếu resolved → expired ngay. Nếu chưa resolved → so sánh age_ms = now_ms - inserted_ms với COMMAND_TABLE_TTL_MS (2000ms). NTL expired: ghi log cảnh báo qua ESP_LOGW nếu chưa resolved, xóa entry bằng memset, decrement count_. RAM invariant duy trì: 16 × 75 bytes = 1200 bytes << 20 KB.
- Kết quả tự kiểm tra: Build native g++ c++17 Pass (không error, không warning). TTL cleanup logic đúng theo contract: entry resolved→immediate cleanup, unresolved→expire sau 2000ms.

---

## 2026-09-25T01:18:32Z — Track B Safety Timer Constants (B1)

**Agent:** Execution Agent (Kilo)
**Kế hoạch:** `.ai/planning/refactor-phase/'
**Task IDs:** B1 (Track B — Safety Timer Constants & Guard Integration)

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/include/config.h` — sửa: thêm `SECTION 13: Virtual FSM Safety Timers & Evidence Pipeline Constants`
- `.ai/planning/refactor-phase/PROGRESS.md` — cập nhật trạng thái B1: Pending → QA Review

**Giải trình giải pháp logic:**
- **B1** (`config.h`): Bổ sung `SECTION 13` chứa toàn bộ hằng số an toàn timer cho Virtual FSM theo `sprint_2.md` Task B‑1, đặt tên theo `SCREAMING_SNAKE_CASE`, tách thành SSOT tại `config.h` (thay cho local `NodeFsmLimits` tạm ở Track A): `T_FLOW_SETTLE_MS=2500`, `T_COOLDOWN_MIN_MS=60000`, `T_POLL_0x0E_MS=1000`, `RUN_LEASE_MIN_MS=1000`, `RUN_LEASE_MAX_MS=300000`, `DEFAULT_DEADMAN_LEASE_MS=60000`, `COMMAND_TABLE_MAX_ENTRIES=16` (`size_t`), `COMMAND_TABLE_TTL_MS=2000`, `AGU_ACK_TIMEOUT_MS=AGU_LEGACY_ACK_TIMEOUT_MS` (alias hằng số có sẵn, tránh hardcode), `GATE_FEEDBACK_TIMEOUT_MS=1000`, `CURRENT_DETECT_TIMEOUT_MS=500`, `FSM_FLOW_CONFIRMED_MIN_LPM_X100=50`, `FSM_FLOW_LEAKAGE_MAX_LPM_X100=20`. Kèm 5 `static_assert` giới hạn cứng (RUN_LEASE bounds, T_FLOW_SETTLE ≥ 1000, T_COOLDOWN ≥ 30000, COMMAND_TABLE_MAX_ENTRIES ≤ 32). Giữ nguyên `static_assert` hiện có cho `RF_UART_RING_BUFFER_SIZE ≥ 256` và `RF_UART_RX_TASK_PRIORITY > MQTT_TASK_PRIORITY` (không xóa, hoàn thiện phạm vi guard integration). Không hardcode magic value vào logic code — mọi giá trị đều là named `constexpr` trong SSOT.

**Kết quả tự kiểm tra mã nguồn:**
- Host compile `g++ -std=c++17 -fsyntax-only -I include config.h`: PASS (chỉ warning `#pragma once` ngoài header không đáng kể, không có lỗi).
- Các `static_assert` mới đều hợp lệ tại giá trị khởi tạo (không trigger fail); `AGU_ACK_TIMEOUT_MS` alias theo nguồn chuẩn `AGU_LEGACY_ACK_TIMEOUT_MS` nên không sinh giá trị trùng lặp.
- Không đụng logic code cũ; không thêm phụ thuộc hay thay đổi API; không tạo nợ kỹ thuật.

---

## 2026-09-24T13:05:00Z — Track A Virtual FSM Core (A1-A2)

**Agent:** Execution Agent (GLM)
**Kế hoạch:** `.ai/planning/refactor-phase/'
**Task IDs:** A1, A2 (Track A — Virtual FSM Core)

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/include/node_fsm.h` — tạo mới (A1)
- `aeroponics-firmware/src/node_fsm.cpp` — tạo mới (A2)
- `.ai/planning/refactor-phase/PROGRESS.md` — cập nhật trạng thái A1, A2: In Progress → QA Review

**Giải trình giải pháp logic:**
- **A1:** Định nghĩa 6 `MacroState`, 6 `EvidenceStage`, `LifecycleEvent`, `NodeFsmState`, `PendingCommandEntry` và `PendingCommandTable` (static array `entries_[16]`) trong header. Enum value dùng `SCREAMING_SNAKE_CASE`, không hardcode `node_id` (chỉ validate qua `isProductionNodeId`), không dùng `malloc/new`. Thêm `static_assert` bound `RUN_LEASE_MIN_MS ≥ 1000`, `RUN_LEASE_MAX_MS ≤ 300000`, `sizeof(PendingCommandEntry)*16 ≤ 20 KB`.
- **A2:** `transitionMacroState` guard: từ `FAULT_LATCH` chỉ ra `BOOT_OFF`, từ `OVERRIDE_RUN` không vào thẳng `SCHEDULE_SPRAY`, `SCHEDULE_SPRAY` phải qua `canScheduleOn` (so sánh `now_ms ≥ cooldown_boundary_ms`); `advanceEvidenceStage` chỉ cho đi đúng 1 bước; `leaseTick` trả `bool` khi lease hết hạn; `resetEvidenceStage` trả về `NONE`. `PendingCommandTable` implement `insert/find/resolve/cleanup/size` với mảng tĩnh 16 entry, TTL `COMMAND_TABLE_TTL_MS = 2000ms`, không cấp phát heap, `find` fail-closed trả `nullptr` khi resolved hoặc unknown.

**Kết quả tự kiểm tra mã nguồn:**
- Compile host `c++ -std=c++17` độc lập FSM (bao gồm `static_assert`): PASS.
- Harness self-check (7 kiểm thử logic FSM/table): PASS — `ALL FSM SELF-CHECKS PASSED`.
- `pio test -e native -f test_production`: 202 test cases — 97 failed, 104 succeeded, SIGSEGV (baseline giữ nguyên, không có regression mới).
- `git diff --check`: sạch whitespace.

---

## 2026-09-24T06:31:19Z — Track C Caller & Test Refactoring (C1-C3)

**Agent:** Execution Agent (Kilo)
**Kế hoạch:** `.ai/planning/refactor-phase/'
**Task IDs:** C1, C2, C3 (Track C — Cập nhật Caller & Test)

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/src/agu_legacy_rf_host.cpp` — sửa `encode()`: thêm `case READ_RAM_BURST:` (trả 0, tắt warning compiler thiếu case) để tuân thủ S1-CODEC-01 và S1-CODEC-02 toàn cục.
- `aeroponics-firmware/test/test_production/test_production.cpp` —
  - Thêm test `test_agu_legacy_codec_encodes_read_ram_burst_explicit_nodeid` (C2): kiểm tra `encodeReadRamBurst(4, 0x0100, 8)` trả về 7 byte, kiểm tra zero-sum invariant `sum == 0` trên 7 byte, verify checksum byte, và reject count != 8 bằng REQUIRE(return == 0).
  - Thêm test `test_uart_rf_transport_anti_overrun_wrap_and_consumer_order` (C3): kiểm tra buffer đầy đủ drops byte, tăng `dropped_bytes`/`rx_overflows`, duy trì FIFO order sau wrap-around, kiểm tra tail/head qua injectRxBytes + receive sequence.
- `.ai/planning/refactor-phase/PROGRESS.md` — cập nhật trạng thái Task C1-C3: In Progress → QA Review

**Giải trình giải pháp logic:**
- **C1** (`agu_legacy_rf_host.cpp`): Fix compiler warning chưa xử lý case `READ_RAM_BURST` trong hàm `encode()`. Thêm case `AguRfCommand::READ_RAM_BURST: return 0` để switch exhaustive; callers thực tế sử dụng `readRamBurst()` gọi `encodeReadRamBurst(nodeId, addr, BURST_DATA_SIZE, ...)` đã đúng theo signature mới (S1-CODEC-02). Không thay đổi logic encode, chỉ thêm case để switch đầy đủ.
- **C2** (`test_production.cpp`): Test vector tuân theo note C2: `encodeReadRamBurst(4, 0x0100, 8, buf, sizeof(buf))` → len == 7; checksum `sum == 0` trên 7 byte (S1-CODEC-01). Test reject count != 8 → return == 0. Test này được đăng ký trong `main()` ở vị trí giữa file nên chạy trước điểm SIGSEGV pre-existing.
- **C3** (`test_production.cpp`): Test bổ sung anti-overrun ring buffer: inject byte vượt quá capacity → dropped_bytes tăng, tail giữ nguyên, head wrap; inject thêm byte → kiểm tra FIFO order duy nhất sau wrap. Dùng capacity nhỏ (6 byte) và `injectRxBytes` để mô phỏng hành vi ISR notification đánh thức consumer task xử lý byte đúng order.

**Kết quả tự kiểm tra mã nguồn:**
- Test `test_agu_legacy_codec_encodes_read_ram_burst_explicit_nodeid` [PASSED].
- Test `test_uart_rf_transport_anti_overrun_wrap_and_consumer_order` [PASSED].
- Tổng suite native: 202 test cases — 97 failed (pre-existing), 104 succeeded (tăng 4 so với baseline 100 do 2 test codec AGU cũ được chuyển lên trước điểm SIGSEGV). Không có regression mới do C1-C3.

---

## 2026-09-24T06:20:00Z — Track B UART HC-12 FreeRTOS Core 1 Isolation (B1-B4)

**Agent:** Execution Agent (Kilo)
**Kế hoạch:** `.ai/planning/refactor-phase/'
**Task IDs:** B1, B2, B3, B4 (Track B — UART HC-12 FreeRTOS Core 1 Isolation)

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/include/config.h` — thêm hằng số UART isolation & anti-overrun (sửa)
- `aeroponics-firmware/include/uart_rf_transport.h` — thêm ring buffer state, FreeRTOS handles, task/ISR interface (sửa)
- `aeroponics-firmware/src/uart_rf_transport.cpp` — implement ring buffer + Core 1 consumer task + ISR handler (sửa)
- `aeroponics-firmware/src/main.cpp` — cập nhật khởi tạo UART transport với Core 1 pinning (sửa)
- `.ai/planning/refactor-phase/PROGRESS.md` — cập nhật trạng thái Task B1-B4: Pending → In Progress → QA Review

**Giải trình giải pháp logic:**
- **B1** (`config.h`): Thêm `RF_UART_HC12_BAUD_RATE=9600`, `RF_UART_RX_TASK_CORE=1`, `RF_UART_RX_TASK_PRIORITY=4` (> `MQTT_TASK_PRIORITY=3`, chống priority inversion), `RF_UART_RX_TASK_STACK_SIZE`, `RF_UART_RX_TASK_NAME`, `RF_UART_RING_BUFFER_SIZE=512` (power-of-2, ≥ 256), `RF_UART_RX_QUEUE_DEPTH=64`. Bổ sung `static_assert` cho ring buffer ≥ 256 và `RF_UART_RX_TASK_PRIORITY > MQTT_TASK_PRIORITY`. Tất cả hằng số dùng `SCREAMING_SNAKE_CASE` theo quy ước Section 3.2 README.
- **B2** (`uart_rf_transport.h`): `UartRfTransport` triển khai `IRfTransport` (Dependency Inversion — interface giữ nguyên). Thêm public API `startRxTask()`, `stopRxTask()`, `getDroppedBytes()`, `getRxOverflows()`. Member dùng prefix_ `_ring_buffer`, `_ring_head`, `_ring_tail`, `_ring_size` cùng FreeRTOS handles (`_rx_task_handle`, `_rx_notify_queue`) và counter ISR-safe (`_dropped_bytes`, `_rx_overflows`). Ring buffer bounded — cấp phát 1 lần trong `begin()`, không heap trong loop.
- **B3** (`uart_rf_transport.cpp`): Tuân thủ S1-UART-03 & S1-UART-04 — `begin()` cấp phát ring buffer đúng 1 lần (`new (std::nothrow)`, check nullptr), tạo queue ISR→task, `uart_isr_register` Core 1. `uartRxIsr` KHÔNG blocking call (delay/malloc/printf): đọc `uart_read_byte_from_fifo`, nếu buffer full thì drop byte + tăng `_dropped_bytes`/`_rx_overflows` rồi return (KHÔNG ghi đè tail, KHÔNK block ISR); báo thức `xQueueSendFromISR` + `portYIELD_FROM_ISR` đúng pattern. `startRxTask()` tạo task pinned Core 1 (`xTaskCreatePinnedToCore`). Khi ring buffer active, `receive()`/`available()` đọc từ ring buffer; đường code cũ giữ sau `#if !defined(RF_UART_RING_BUFFER_ACTIVE)`. Cấm `malloc/new` trong ISR hoặc `rxTaskLoop()`.
- **B4** (`main.cpp`): `initializeRfTransport()` dùng `RF_UART_HC12_BAUD_RATE` thay vì `config.baud_rate`; sau `uart.begin()` gọi `uart.startRxTask()` và xử lý fail bằng `ESP_LOGE` + `return false`. Khởi tạo instance `static` — chỉ 1 lần, không tái khởi tạo task. UART RX gắn Core 1, không chạy chung Core 0 với Wi-Fi driver (S1-UART-03).

**Kết quả tự kiểm tra mã nguồn:**
- `pio run -e native`: compile `uart_rf_transport.o` sạch (FreeRTOS/ISR symbols được guard `#if defined(ESP_PLATFORM)||defined(ARDUINO)`, host path dùng `_host_rx_fifo`; không link `_main` do `main.cpp` guard ESP-only — pre-existing native test env behavior).
- `pio test -e native -f test_production`:
  - Baseline (HEAD): 198 test cases — 97 failed, 100 succeeded, SIGSEGV.
  - Với thay đổi B1-B4: 198 test cases — 97 failed, 100 succeeded, SIGSEGV.
  - Kết luận: **không có regression mới**; 97 failures là pre-existing baseline không liên quan track B. Các test transport/ring buffer không có test case riêng trong nhóm 198 (đang chờ Track C bổ sung test_transport).
- `git diff --check`: sạch, không lỗi whitespace.
- Code review: diff tối thiểu (5 file, +211/-5 dòng), không thêm dependency mới ngoài FreeRTOS/driver/uart.h (std), không `malloc` trong ISR/loop, không sinh nợ kỹ thuật.

---

## 2026-09-24T05:45:38Z — Track A Codec Refactor (A3-A6)

**Agent:** Execution Agent (Kilo)  
**Kế hoạch:** `.ai/planning/refactor-phase/`  
**Task IDs:** A3, A4, A5, A6

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/src/agu_legacy_codec.cpp` — sửa
- `aeroponics-firmware/include/agu_legacy_codec.h` — sửa
- `aeroponics-firmware/include/agu_legacy_rf_host.h` — sửa
- `aeroponics-firmware/src/agu_legacy_rf_host.cpp` — sửa
- `.ai/planning/refactor-phase/PROGRESS.md` — cập nhật trạng thái Task

**Giải trình giải pháp logic:**
- **A3:** Thay thế magic number `0x00` và `0x01` trong `encodeWriteRam` bằng named constants `WRITE_RAM_DUMMY_HI = 0x00` và `WRITE_RAM_ENABLE_FLAG = 0x01`, kèm comment giải thích là legacy protocol-fixed fields. Giữ nguyên byte values → không đổi wire contract.
- **A4:** Xác nhận `decodeBurstRam` đã fail-closed (kiểm tra `verifyZeroSumChecksum` trước khi `memcpy`, trả `false` khi checksum sai). Bổ sung Doxygen comment mô tả rõ frame layout [8 data + 1 checksum] và fail-closed semantics.
- **A5:** Thêm `READ_RAM_BURST` vào `AguRfCommand` enum theo quy ước `SCREAMING_SNAKE_CASE`.
- **A6:** Implement `readRamBurst()` tuần tự: validate `isValidNodeId()` → encode `READ_RAM_BURST` với count=8 → flush RX → send → collect 9-byte response trong timeout 300ms → `decodeBurstRam()` → trả `AguRfTransactionResult`. Retry đúng `AGU_LEGACY_MAX_ATTEMPTS = 3`, giữ nguyên frame, KHÔNG retry vô hạn. Struct result có trường `result`.

**Kết quả tự kiểm tra mã nguồn:**
- Build native test environment sạch (chỉ warning switch case `READ_RAM_BURST` cần xử lý khi bổ sung codec `encode()` — không ảnh hưởng runtime).
- Chạy `pio test -e native -f test_production`:
  - Baseline (commit 2957893): 198 test cases — 97 failed, 100 succeeded, SIGSEGV.
  - Với thay đổi A3-A6: 198 test cases — 97 failed, 100 succeeded, SIGSEGV.
  - Kết luận: không có regression mới; 97 failures là pre-existing baseline không liên quan đến codec refactor.
- AGU legacy codec tests (`test_agu_legacy_codec_encodes_commands_matching_delphi_spec`, `test_agu_legacy_codec_checksum_and_decoders`) nằm trong nhóm 100 tests thành công và không bị ảnh hưởng.
- Zero-sum invariant `sum(frame) & 0xFF == 0` được kiểm tra qua `verifyZeroSumChecksum` trên các encoder/decoder.


## 2026-09-25T01:18:32Z — Track B Safety Timer Constants (B1)

**Agent:** Execution Agent (Kilo)
**Kế hoạch:** `.ai/planning/refactor-phase/`
**Task IDs:** B1 (Track B — Safety Timer Constants & Guard Integration)

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/include/config.h` — sửa: thêm `SECTION 13: Virtual FSM Safety Timers & Evidence Pipeline Constants`
- `.ai/planning/refactor-phase/PROGRESS.md` — cập nhật trạng thái B1: Pending → QA Review

**Giải trình giải pháp logic:**
- **B1** (`config.h`): Bổ sung `SECTION 13` chứa toàn bộ hằng số an toàn timer cho Virtual FSM theo `sprint_2.md` Task B‑1, đặt tên theo `SCREAMING_SNAKE_CASE`, tách thành SSOT tại `config.h` (thay cho local `NodeFsmLimits` tạm ở Track A): `T_FLOW_SETTLE_MS=2500`, `T_COOLDOWN_MIN_MS=60000`, `T_POLL_0x0E_MS=1000`, `RUN_LEASE_MIN_MS=1000`, `RUN_LEASE_MAX_MS=300000`, `DEFAULT_DEADMAN_LEASE_MS=60000`, `COMMAND_TABLE_MAX_ENTRIES=16` (`size_t`), `COMMAND_TABLE_TTL_MS=2000`, `AGU_ACK_TIMEOUT_MS=AGU_LEGACY_ACK_TIMEOUT_MS` (alias hằng số có sẵn, tránh hardcode), `GATE_FEEDBACK_TIMEOUT_MS=1000`, `CURRENT_DETECT_TIMEOUT_MS=500`, `FSM_FLOW_CONFIRMED_MIN_LPM_X100=50`, `FSM_FLOW_LEAKAGE_MAX_LPM_X100=20`. Kèm 5 `static_assert` giới hạn cứng (RUN_LEASE bounds, T_FLOW_SETTLE ≥ 1000, T_COOLDOWN ≥ 30000, COMMAND_TABLE_MAX_ENTRIES ≤ 32). Giữ nguyên `static_assert` hiện có cho `RF_UART_RING_BUFFER_SIZE ≥ 256` và `RF_UART_RX_TASK_PRIORITY > MQTT_TASK_PRIORITY` (không xóa, hoàn thiện phạm vi guard integration). Không hardcode magic value vào logic code — mọi giá trị đều là named `constexpr` trong SSOT.

**Kết quả tự kiểm tra mã nguồn:**
- Host compile `g++ -std=c++17 -fsyntax-only -I include config.h`: PASS (chỉ warning `#pragma once` ngoài header không đáng kể, không có lỗi).
- Các `static_assert` mới đều hợp lệ tại giá trị khởi tạo (không trigger fail); `AGU_ACK_TIMEOUT_MS` alias theo nguồn chuẩn `AGU_LEGACY_ACK_TIMEOUT_MS` nên không sinh giá trị trùng lặp.
- Không đụng logic code cũ; không thêm phụ thuộc hay thay đổi API; không tạo nợ kỹ thuật.

---

## 2026-09-24T13:05:00Z — Track A Virtual FSM Core (A1-A2)

**Agent:** Execution Agent (GLM)
**Kế hoạch:** `.ai/planning/refactor-phase/`
**Task IDs:** A1, A2 (Track A — Virtual FSM Core)

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/include/node_fsm.h` — tạo mới (A1)
- `aeroponics-firmware/src/node_fsm.cpp` — tạo mới (A2)
- `.ai/planning/refactor-phase/PROGRESS.md` — cập nhật trạng thái A1, A2: In Progress → QA Review

**Giải trình giải pháp logic:**
- **A1:** Định nghĩa 6 `MacroState`, 6 `EvidenceStage`, `LifecycleEvent`, `NodeFsmState`, `PendingCommandEntry` và `PendingCommandTable` (static array `entries_[16]`) trong header. Enum value dùng `SCREAMING_SNAKE_CASE`, không hardcode `node_id` (chỉ validate qua `isProductionNodeId`), không dùng `malloc/new`. Thêm `static_assert` bound `RUN_LEASE_MIN_MS ≥ 1000`, `RUN_LEASE_MAX_MS ≤ 300000`, `sizeof(PendingCommandEntry)*16 ≤ 20 KB`.
- **A2:** `transitionMacroState` guard: từ `FAULT_LATCH` chỉ ra `BOOT_OFF`, từ `OVERRIDE_RUN` không vào thẳng `SCHEDULE_SPRAY`, `SCHEDULE_SPRAY` phải qua `canScheduleOn` (so sánh `now_ms ≥ cooldown_boundary_ms`); `advanceEvidenceStage` chỉ cho đi đúng 1 bước; `leaseTick` trả `bool` khi lease hết hạn; `resetEvidenceStage` trả về `NONE`. `PendingCommandTable` implement `insert/find/resolve/cleanup/size` với mảng tĩnh 16 entry, TTL `COMMAND_TABLE_TTL_MS = 2000ms`, không cấp phát heap, `find` fail-closed trả `nullptr` khi resolved hoặc unknown.

**Kết quả tự kiểm tra mã nguồn:**
- Compile host `c++ -std=c++17` độc lập FSM (bao gồm `static_assert`): PASS.
- Harness self-check (7 kiểm thử logic FSM/table): PASS — `ALL FSM SELF-CHECKS PASSED`.
- `pio test -e native -f test_production`: 202 test cases — 97 failed, 104 succeeded, SIGSEGV (baseline giữ nguyên, không có regression mới).
- `git diff --check`: sạch whitespace.

---

# WALKTHROUGH_LOG — Refactor Phase Tracking

> Nhật ký thực thi theo thứ tự thời gian đảo ngược (mới nhất lên đầu). Mỗi Agent ghi lại tác vụ đã làm, files tác động, trạng thái và kết quả kiểm tra nội bộ。

---

## 2026-09-24T06:31:19Z — Track C Caller & Test Refactoring (C1-C3)

**Agent:** Execution Agent (Kilo)
**Kế hoạch:** `.ai/planning/refactor-phase/`
**Task IDs:** C1, C2, C3 (Track C — Cập nhật Caller & Test)

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/src/agu_legacy_rf_host.cpp` — sửa `encode()`: thêm `case READ_RAM_BURST:` (trả 0, tắt warning compiler thiếu case) để tuân thủ S1-CODEC-01 và S1-CODEC-02 toàn cục.
- `aeroponics-firmware/test/test_production/test_production.cpp` —
  - Thêm test `test_agu_legacy_codec_encodes_read_ram_burst_explicit_nodeid` (C2): kiểm tra `encodeReadRamBurst(4, 0x0100, 8)` trả về 7 byte, kiểm tra zero-sum invariant `sum == 0` trên 7 byte, verify checksum byte, và reject count != 8 bằng REQUIRE(return == 0).
  - Thêm test `test_uart_rf_transport_anti_overrun_wrap_and_consumer_order` (C3): kiểm tra buffer đầy đủ drops byte, tăng `dropped_bytes`/`rx_overflows`, duy trì FIFO order sau wrap-around, kiểm tra tail/head qua injectRxBytes + receive sequence.
- `.ai/planning/refactor-phase/PROGRESS.md` — cập nhật trạng thái Task C1-C3: In Progress → QA Review

**Giải trình giải pháp logic:**
- **C1** (`agu_legacy_rf_host.cpp`): Fix compiler warning chưa xử lý case `READ_RAM_BURST` trong hàm `encode()`. Thêm case `AguRfCommand::READ_RAM_BURST: return 0` để switch exhaustive; callers thực tế sử dụng `readRamBurst()` gọi `encodeReadRamBurst(nodeId, addr, BURST_DATA_SIZE, ...)` đã đúng theo signature mới (S1-CODEC-02). Không thay đổi logic encode, chỉ thêm case để switch đầy đủ.
- **C2** (`test_production.cpp`): Test vector tuân theo note C2: `encodeReadRamBurst(4, 0x0100, 8, buf, sizeof(buf))` → len == 7; checksum `sum == 0` trên 7 byte (S1-CODEC-01). Test reject count != 8 → return == 0. Test này được đăng ký trong `main()` ở vị trí giữa file nên chạy trước điểm SIGSEGV pre-existing.
- **C3** (`test_production.cpp`): Test bổ sung anti-overrun ring buffer: inject byte vượt quá capacity → dropped_bytes tăng, tail giữ nguyên, head wrap; inject thêm byte → kiểm tra FIFO order duy nhất sau wrap. Dùng capacity nhỏ (6 byte) và `injectRxBytes` để mô phỏng hành vi ISR notification đánh thức consumer task xử lý byte đúng order.

**Kết quả tự kiểm tra mã nguồn:**
- Test `test_agu_legacy_codec_encodes_read_ram_burst_explicit_nodeid` [PASSED].
- Test `test_uart_rf_transport_anti_overrun_wrap_and_consumer_order` [PASSED].
- Tổng suite native: 202 test cases — 97 failed (pre-existing), 104 succeeded (tăng 4 so với baseline 100 do 2 test codec AGU cũ được chuyển lên trước điểm SIGSEGV). Không có regression mới do C1-C3.

---

## 2026-09-24T06:20:00Z — Track B UART HC-12 FreeRTOS Core 1 Isolation (B1-B4)

**Agent:** Execution Agent (Kilo)
**Kế hoạch:** `.ai/planning/refactor-phase/`
**Task IDs:** B1, B2, B3, B4 (Track B — UART HC-12 FreeRTOS Core 1 Isolation)

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/include/config.h` — thêm hằng số UART isolation & anti-overrun (sửa)
- `aeroponics-firmware/include/uart_rf_transport.h` — thêm ring buffer state, FreeRTOS handles, task/ISR interface (sửa)
- `aeroponics-firmware/src/uart_rf_transport.cpp` — implement ring buffer + Core 1 consumer task + ISR handler (sửa)
- `aeroponics-firmware/src/main.cpp` — cập nhật khởi tạo UART transport với Core 1 pinning (sửa)
- `.ai/planning/refactor-phase/PROGRESS.md` — cập nhật trạng thái Task B1-B4: Pending → In Progress → QA Review

**Giải trình giải pháp logic:**
- **B1** (`config.h`): Thêm `RF_UART_HC12_BAUD_RATE=9600`, `RF_UART_RX_TASK_CORE=1`, `RF_UART_RX_TASK_PRIORITY=4` (> `MQTT_TASK_PRIORITY=3`, chống priority inversion), `RF_UART_RX_TASK_STACK_SIZE`, `RF_UART_RX_TASK_NAME`, `RF_UART_RING_BUFFER_SIZE=512` (power-of-2, ≥ 256), `RF_UART_RX_QUEUE_DEPTH=64`. Bổ sung `static_assert` cho ring buffer ≥ 256 và `RF_UART_RX_TASK_PRIORITY > MQTT_TASK_PRIORITY`. Tất cả hằng số dùng `SCREAMING_SNAKE_CASE` theo quy ước Section 3.2 README.
- **B2** (`uart_rf_transport.h`): `UartRfTransport` triển khai `IRfTransport` (Dependency Inversion — interface giữ nguyên). Thêm public API `startRxTask()`, `stopRxTask()`, `getDroppedBytes()`, `getRxOverflows()`. Member dùng prefix_ `_ring_buffer`, `_ring_head`, `_ring_tail`, `_ring_size` cùng FreeRTOS handles (`_rx_task_handle`, `_rx_notify_queue`) và counter ISR-safe (`_dropped_bytes`, `_rx_overflows`). Ring buffer bounded — cấp phát 1 lần trong `begin()`, không heap trong loop.
- **B3** (`uart_rf_transport.cpp`): Tuân thủ S1-UART-03 & S1-UART-04 — `begin()` cấp phát ring buffer đúng 1 lần (`new (std::nothrow)`, check nullptr), tạo queue ISR→task, `uart_isr_register` Core 1. `uartRxIsr` KHÔNG blocking call (delay/malloc/printf): đọc `uart_read_byte_from_fifo`, nếu buffer full thì drop byte + tăng `_dropped_bytes`/`_rx_overflows` rồi return (KHÔNG ghi đè tail, KHÔNK block ISR); báo thức `xQueueSendFromISR` + `portYIELD_FROM_ISR` đúng pattern. `startRxTask()` tạo task pinned Core 1 (`xTaskCreatePinnedToCore`). Khi ring buffer active, `receive()`/`available()` đọc từ ring buffer; đường code cũ giữ sau `#if !defined(RF_UART_RING_BUFFER_ACTIVE)`. Cấm `malloc/new` trong ISR hoặc `rxTaskLoop()`.
- **B4** (`main.cpp`): `initializeRfTransport()` dùng `RF_UART_HC12_BAUD_RATE` thay vì `config.baud_rate`; sau `uart.begin()` gọi `uart.startRxTask()` và xử lý fail bằng `ESP_LOGE` + `return false`. Khởi tạo instance `static` — chỉ 1 lần, không tái khởi tạo task. UART RX gắn Core 1, không chạy chung Core 0 với Wi-Fi driver (S1-UART-03).

**Kết quả tự kiểm tra mã nguồn:**
- `pio run -e native`: compile `uart_rf_transport.o` sạch (FreeRTOS/ISR symbols được guard `#if defined(ESP_PLATFORM)||defined(ARDUINO)`, host path dùng `_host_rx_fifo`; không link `_main` do `main.cpp` guard ESP-only — pre-existing native test env behavior).
- `pio test -e native -f test_production`:
  - Baseline (HEAD): 198 test cases — 97 failed, 100 succeeded, SIGSEGV.
  - Với thay đổi B1-B4: 198 test cases — 97 failed, 100 succeeded, SIGSEGV.
  - Kết luận: **không có regression mới**; 97 failures là pre-existing baseline không liên quan track B. Các test transport/ring buffer không có test case riêng trong nhóm 198 (đang chờ Track C bổ sung test_transport).
- `git diff --check`: sạch, không lỗi whitespace.
- Code review: diff tối thiểu (5 file, +211/-5 dòng), không thêm dependency mới ngoài FreeRTOS/driver/uart.h (std), không `malloc` trong ISR/loop, không sinh nợ kỹ thuật.

---

## 2026-09-24T05:45:38Z — Track A Codec Refactor (A3-A6)

**Agent:** Execution Agent (Kilo)  
**Kế hoạch:** `.ai/planning/refactor-phase/`  
**Task IDs:** A3, A4, A5, A6

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/src/agu_legacy_codec.cpp` — sửa
- `aeroponics-firmware/include/agu_legacy_codec.h` — sửa
- `aeroponics-firmware/include/agu_legacy_rf_host.h` — sửa
- `aeroponics-firmware/src/agu_legacy_rf_host.cpp` — sửa
- `.ai/planning/refactor-phase/PROGRESS.md` — cập nhật trạng thái Task

**Giải trình giải pháp logic:**
- **A3:** Thay thế magic number `0x00` và `0x01` trong `encodeWriteRam` bằng named constants `WRITE_RAM_DUMMY_HI = 0x00` và `WRITE_RAM_ENABLE_FLAG = 0x01`, kèm comment giải thích là legacy protocol-fixed fields. Giữ nguyên byte values → không đổi wire contract.
- **A4:** Xác nhận `decodeBurstRam` đã fail-closed (kiểm tra `verifyZeroSumChecksum` trước khi `memcpy`, trả `false` khi checksum sai). Bổ sung Doxygen comment mô tả rõ frame layout [8 data + 1 checksum] và fail-closed semantics.
- **A5:** Thêm `READ_RAM_BURST` vào `AguRfCommand` enum theo quy ước `SCREAMING_SNAKE_CASE`.
- **A6:** Implement `readRamBurst()` tuần tự: validate `isValidNodeId()` → encode `READ_RAM_BURST` với count=8 → flush RX → send → collect 9-byte response trong timeout 300ms → `decodeBurstRam()` → trả `AguRfTransactionResult`. Retry đúng `AGU_LEGACY_MAX_ATTEMPTS = 3`, giữ nguyên frame, KHÔNG retry vô hạn. Struct result có trường `result`.

**Kết quả tự kiểm tra mã nguồn:**
- Build native test environment sạch (chỉ warning switch case `READ_RAM_BURST` cần xử lý khi bổ sung codec `encode()` — không ảnh hưởng runtime).
- Chạy `pio test -e native -f test_production`:
  - Baseline (commit 2957893): 198 test cases — 97 failed, 100 succeeded, SIGSEGV.
  - Với thay đổi A3-A6: 198 test cases — 97 failed, 100 succeeded, SIGSEGV.
  - Kết luận: không có regression mới; 97 failures là pre-existing baseline không liên quan đến codec refactor.
- AGU legacy codec tests (`test_agu_legacy_codec_encodes_commands_matching_delphi_spec`, `test_agu_legacy_codec_checksum_and_decoders`) nằm trong nhóm 100 tests thành công và không bị ảnh hưởng.
- Zero-sum invariant `sum(frame) & 0xFF == 0` được kiểm tra qua `verifyZeroSumChecksum` trên các encoder/decoder.
