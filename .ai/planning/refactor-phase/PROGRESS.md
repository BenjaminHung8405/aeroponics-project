# PROGRESS — Refactor Phase Tracking

> Tài liệu theo dõi tiến độ thực thi (Progress Register) cho kế hoạch Refactor được khởi tạo bởi **Gemini**. Mọi Agent thực thi phải cập nhật Status tại đây sau mỗi tác vụ theo đúng quy ước markdown checkbox (Pending / In Progress / QA Review / Done). Không được sửa cột Note trừ khi có thay đổi hướng dẫn kỹ thuật được phê duyệt ở `.ai/planning/refactor-phase/README.md`.

---

## 1. Started

| Trường | Giá trị |
|---|---|
| **Thời điểm khởi tạo** | `2026-09-25T06:13:14Z` (UTC) |
| **Execution Agent** | **Gemini** |
| **Vai trò** | Kỹ sư thực thi (Execution Agent) theo kế hoạch Sprint |
| **Baseline Agent** | Đã khởi tạo master plan `README.md` ngày `2026-09-24` (không thay đổi trong phạm vi refactor) |

---

## 2. Reference Plan

| Trường | Giá trị |
|---|---|
| **Thư mục kế hoạch** | `.ai/planning/refactor-phase/` |
| **Master Planning Context** | `.ai/planning/refactor-phase/README.md` (Single Source of Truth — bắt buộc đọc trước khi bắt đầu bất kỳ Sprint nào) |
| **Sprint hiện tại (đang tham chiếu)** | `.ai/planning/refactor-phase/sprint_3.md — **Sprint 3: Backend Ingestion & Admission Pipeline (NestJS & TimescaleDB)**` |
| **Thứ tự Sprint roadmap** | `Sprint 1 (Codec ESP32)` → `Sprint 2 (Virtual FSM & Safety Timers)` → `Sprint 3 (Backend NestJS & TimescaleDB)` → `Sprint 4 (Dashboard Next.js & Nginx)` |
| **Golden Baseline tham chiếu Sprint 3** | `docs/interface-wire-contract.md` §7–§9, `docs/STATE_MACHINE_MATRIX.md` §5–§9 |
| **Phụ thuộc Sprint 2** | Sprint 2 PASS (Virtual FSM + Safety Timers trên Gateway) |
| **Output bàn giao Sprint 3** | MQTT topic namespace `aeroponics/v1/...` hoạt động, Retain tắt trên topic giao dịch, DB safety lock UC-BE-10 hoạt động, TimescaleDB batch ingestion tối ưu, 273/273 unit tests PASS |

---

## 3. Addition Plan (Yêu cầu phát sinh)

**Chưa có yêu cầu phát sinh nào được bổ sung.** Mọi yêu cầu mới vượt phạm vi Golden Baseline hoặc Sprint hiện tại phải được ghi vào bảng dưới đây trước khi triển khai, và phải được phê duyệt tại `.ai/planning/refactor-phase/README.md`.

| ID | Yêu cầu phát sinh | Trạng thái | Ghi chú |
|---|---|---|---|
| (trống) | Chưa có | — | — |

---

## 4. Track Status — Sprint 3

> Quy ước Status (bắt buộc, không thay đổi ký hiệu):
> - `[ ] Pending` — Task chưa chạm vào.
> - `[ ] In Progress` — Execution Agent đang viết code.
> - `[ ] QA Review` — Code đã viết xong, đang chờ rà soát chất lượng.
> - `[x] Done` — Đã qua vòng review nghiêm ngặt và được duyệt.

> **Chỉ thị kỹ thuật bắt buộc (đóng vai Note):** Mỗi Task Note phải tuân theo các quy tắc trong `.ai/planning/refactor-phase/README.md` (Clean Architecture, Dependency Inversion, Strangler Fig, Fail‑safe, Zero‑hardcode credential, Non‑retained transactional topics, `synchronize: false`, v.v.) và các rule chuẩn sprint tương ứng (S3‑MQTT‑01…S3‑TABLE‑06, v.v.).

### 4.1 TRACK I — MQTT Topic Namespace Standardization

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| I1 | `aeroponics-backend/src/mqtt/mqtt.constants.ts` — Cập nhật topic patterns | [ ] QA Review | Export MQTT_TOPICS với V1_NODE_ACK, V1_NODE_TELEMETRY, V1_NODE_EVENT, V1_GATEWAY_HEARTBEAT patterns theo namespace `aeroponics/v1/...`. Retain policy: STATUS_LWT: true, TRANSACTIONAL: false, HEARTBEAT: false. DEFAULT_SUBSCRIBE_TOPICS phải include cả v1 và device namespace. |
| I2 | `aeroponics-backend/src/mqtt/mqtt.service.ts` — Cập nhật routeMessage với v1 patterns | [ ] QA Review | Thêm routing cho v1/ node/+/ack, node/+/telemetry, node/+/event trong routeMessage(). Cập nhật publish() enforce retain policy: retain: false cho transactional (ack, command, event, telemetry), retain: true cho status/LWT. |
| I3 | `aeroponics-backend/src/mqtt/mqtt-router.service.ts` — Alias mapping layer | [ ] QA Review | Triển khai mapV1ToDeviceAlias() method: khi gateway publish trên v1/ namespace, router emit equivalent event trên device/ namespace cho backward-compatible subscribers. NEVER publish both simultaneous trên same message. |

### 4.2 TRACK J — DB Safety Lock (UC-BE-10)

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| J1 | `aeroponics-backend/src/pump-command/pump-command.service.ts` — Calibration ACTIVE check | [ ] QA Review | Triển khai validateCalibrationActive(nodeId: number): Promise<void>. Query SensorCalibration.findOne({ where: { node_id: nodeId, status: CalibrationStatusEnum.ACTIVE } }). Nếu không có active calibration → throw BadRequestException + publish REJECTED ACK. Double-check nodeRegistry calibration_status phải CALIBRATED. |
| J2 | `aeroponics-backend/src/flow/flow.service.ts` — Calibration guard cho recordFlowEvent | [ ] QA Review | Thêm guard UC-BE-10 ở đầu recordFlowEvent(). Nếu !activeCal → throw BadRequestException, KHÔNH fallback calibrationId = 1. Gán dto.sensor_calibration_id = activeCal.id (sử dụng calibration vừa query được). |
| J3 | `aeroponics-backend/src/node/entities/sensor_calibration.entity.ts` — Helper method | [ ] QA Review | Thêm method isActive(): boolean { return this.status === CalibrationStatusEnum.ACTIVE; }. Helper kiểm tra calibration status ACTIVE. |

### 4.3 TRACK K — TimescaleDB Batch Ingestion

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| K1 | `aeroponics-backend/src/database/database.module.ts` — Dedicated connection pools | [ ] QA Review | Thêm dedicated write pool cho TimescaleDB batch inserts: max 10 connections, idleTimeoutMillis: 10000, connectionTimeoutMillis: 5000. Main pool max 20 connections. Tổng: 30 connections well within TimescaleDB max_connections = 100. |
| K2 | `aeroponics-backend/src/flow/flow.service.ts` — Batch buffer implementation | [ ] QA Review | Triển khai bufferFlowEvent(), flushFlowEventBatch(), startBatchTimer(). Buffer max 50 events, flush mỗi 5000ms. Dùng dedicated write connection pool. Single batch INSERT với ON CONFLICT DO NOTHING. Không row-level locking per insert. |
| K3 | `aeroponics-backend/src/flow/entities/flow_event.entity.ts` — TimescaleDB hypertable index | [ ] QA Review | Thêm TimescaleDB-aware index hint cho cặp (node_id, time). TimescaleDB tự động tạo indexes trên hypertable partition key. Đảm bảo time column là partitioning column trong CREATE HYPERTABLE. |

### 4.4 TRACK L — WebSocket Flow Confirmed Broadcast

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| L1 | `aeroponics-backend/src/websocket/events.gateway.ts` — FLOW_CONFIRMED broadcast | [ ] QA Review | Triển khai handleFlowConfirmed() emit 'node_flow' event chỉ khi evidence pipeline đạt FLOW_CONFIRMED stage. UI dùng flowConfirmed để hiển thị RUNNING status. Dashboard NodeCard subscribe node_flow event. |

### 4.5 TRACK M — Unit Tests

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| M1 | `aeroponics-backend/src/mqtt/mqtt.service.spec.ts` — Namespace routing test | [ ] QA Review | Thêm test routing v1/node/{nodeId}/ack tới COMMAND_ACK event. Test retain=false trên transactional publish. Test retain=true trên status/LWT publish. |
| M2 | `aeroponics-backend/src/flow/flow.service.spec.ts` — UC-BE-10 safety lock test | [ ] QA Review | Test reject flow event khi không có ACTIVE calibration. Test allowance khi ACTIVE calibration tồn tại. |

### 4.6 TRACK S — Critical Backend Sync Fixes

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| S1 | UC-BE-10 Calibration Gate — `pump-command.service.ts` | [ ] QA Review | Thêm validateCalibrationActive check TRƯỚC MQTT publish trong sendCommand(). Nếu !activeCalibration → throw BadRequestException + publish REJECTED ACK `{ status: 'REJECTED', reason: 'UC-BE-10: No ACTIVE calibration' }`. KHÔNH fallback calibrationId = 1. |
| S2 | UC-BE-10 Calibration Guard — `flow.service.ts` | [ ] QA Review | Thêm guard UC-BE-10 ở đầu recordFlowEvent(). Nếu !activeCal → throw BadRequestException, KHÔNH fallback. Gán dto.sensor_calibration_id = activeCal.id. |
| S3 | MQTT Topic Namespace — Align DEFAULT_SUBSCRIBE_TOPICS | [ ] QA Review | Cập nhật DEFAULT_SUBSCRIBE_TOPICS match aeroponics/v1/node/+/... namespace. Task I-1 + I-2 đã lên plan — PHẢI implement đúng với v1 namespace. Current NODE_TELEMETRY thiếu 'v1'. Option A (Recommended): update backend để match firmware publish topics aeroponics/v1/node/{nodeId}/event. |
| S4 | MQTT Retain Policy — Heartbeat Contradiction | [ ] QA Review | Xóa topic.includes('/heartbeat') khỏi retain = true block trong publish(). Heartbeat là transactional, KHÔNH retain. Chỉ giữ retain: true cho `/status` (LWT). |
| S5 | Admission ACK vs RF_ACKED Separation — `mqtt-router.service.ts` | [ ] QA Review | Chỉ set acked = true khi payload.acked === true hoặc status === 'RF_ACKED'. ACCEPTED → emit MQTT_EVENTS.COMMAND_ACCEPTED event (lifecycle admission, KHÔNH RF acknowledgment). Thêm type CommandAcceptedEvent riêng. |
| S6 | MQTT Publish Retain Substring Matching — Robust Topic Classification | [ ] QA Review | Dùng regex matching thay vì includes(): `/\/(ack|command|event|telemetry)(\/|$)/.test(topic)` cho transactional. `/\/status(\/|$)/.test(topic)` cho status. `/\/heartbeat(\/|$)/.test(topic)` cho heartbeat. Priority: status > heartbeat > transactional. Default: retain: false fail-safe. |
| S7 | command_id UUID Validation — `mqtt-router.service.ts` | [ ] QA Review | Relaxt command_id validation: chấp nhận string không rỗng, KHÔNH yêu cầu UUID format. Validate presence (không null/empty) thay vì format. `command_id: 'rf-cmd-1'` được xử lý đúng, outcome = ACCEPTED thay vì FAULT_NO_ACK. |

### 4.7 TRACK T — Moderate Backend Sync Fixes

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| T1 | PumpControl Endpoint & DTO Alignment | [ ] Pending | Align PumpControl UI endpoint với backend DTO. Option A (Recommended): update UI để use {action: 'ON'|'OFF', node_id, run_lease_ms} DTO. Hoặc tạo REST wrapper endpoint `/pump-command/override` trong backend với DTO adapter. |
| T2 | Flow Event Batch Emit Timing — Align with §2.3 Diagram | [ ] Pending | Keep emit ngay lập tức cho WS event UX, nhưng chỉ emit sau khi batch buffer confirm flush. Hoặc thêm flag `emitAfterFlush: boolean` option cho `bufferFlowEvent()` — khi true thì delay emit. |

### 4.8 TRACK U — Minor Backend Sync Fixes

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| U1 | Retain Policy Diagram — §2.4 Correction | [ ] Pending | Sửa code retain policy theo diagram §2.4. Code hiện retain: true cho heartbeat nhưng diagram đúng là retain: false. Fix: sửa code theo diagram. Chỉ `/status` (LWT) giữ retain: true. |
| U2 | AGU_LEGACY_NODE_IDS Verification | [ ] Pending | Verify AGU_LEGACY_NODE_IDS. Production IDs là 1..4, KHÔNH [4,5,6,7]. Đã documented trong wire contract §6 item 163. Blocked đến khi quyết định production IDs trước khi Sprint 3 Task I-1 (topic patterns) hoạt động đúng. |

---

*File PROGRESS.md đã được khởi tạo tại `.ai/planning/refactor-phase/PROGRESS.md` kèm theo cấu trúc định dạng Markdown chuẩn, Track I‑U từ Sprint 3 đã được chuyển hóa thành các bảng 4 cột (Task ID / Mô tả Task / Status / Note chỉ thị kỹ thuật bắt buộc) và tất cả Status khởi tạo là `[ ] Pending`.*
