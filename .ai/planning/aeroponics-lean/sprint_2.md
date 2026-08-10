# Sprint 2: Production RF Gateway, Dynamic Treatment & 12-Node Control

> **Phụ thuộc:** Sprint 1.5 PASS. Module RF/BOM, anten, UART baud/mode/pinout, protocol version, flow calibration procedure và fail-safe policy phải được phê duyệt trong POC decision record.
> **Output bàn giao:** Firmware ESP32 gateway production quản lý tối đa 4 group timer động, mapping động 12 node RF, command lifecycle có ACK/pump feedback/flow confirmation, MQTT telemetry cho Backend và fail-safe có thể audit.
> **Không thuộc Sprint này:** NestJS database/API/UI production (Sprint 3–4); không hard-code M1–M4, mapping 3 node/group, RF baud/pinout hoặc ngưỡng flow trong source.

> **Tài liệu ưu tiên:** [PROJECT_ALIGNMENT_2026-08-10.md](./PROJECT_ALIGNMENT_2026-08-10.md) và [sprint_1_5.md](./sprint_1_5.md).

---

## 1. Mục tiêu và phạm vi

### 1.1 Các module firmware mới/cần thay thế

| Module | Trách nhiệm |
|---|---|
| `rf_transport.*` | Adapter UART cho module RF đã chốt, không dùng chung USB debug Serial. |
| `rf_frame_codec.*` | Bounded parser/encoder, version, CRC-16, sequence, duplicate-safe. |
| `pump_node_controller.*` | Điều phối request/ACK/retry, desired/reported/feedback state, timeout và fault. |
| `node_registry.*` | Inventory tối đa 12 node, health, telemetry freshness, mapping active group. |
| `treatment_store.*` | Snapshot profile/version đã publish từ backend/NVS; không chứa preset hard-code. |
| `group_scheduler.*` | Tối đa 4 group active, ngày/đêm UTC+7, fan-out lệnh tới node gán động. |
| `flow_evaluator.*` | Nhận telemetry flow, đánh giá flow-confirmed/no-flow/unexpected-flow theo cấu hình node. |
| `mqtt_client.*` | MQTT gateway↔backend, LWT, command idempotency, publish state/event; không trực tiếp điều khiển GPIO/pump. |

### 1.2 Mục tiêu nghiệm thu

- [ ] Chạy đồng thời tối đa 4 group; mỗi group dùng một `treatment_version_id` đã publish hoặc ở `UNASSIGNED` và không chạy.
- [ ] Gán/bỏ gán 12 node động qua command cấu hình có version, validation và audit event; một node không thuộc hơn một group active.
- [ ] Mỗi command ON/OFF có `command_id`, RF sequence, ACK/NACK/timeout/retry giới hạn và kết quả từng node.
- [ ] Trạng thái tưới thành công chỉ có sau `RF_ACKED → PUMP_FEEDBACK_ON → FLOW_CONFIRMED`.
- [ ] Flow telemetry gồm `flow_lpm`, `delivered_volume_l`, calibration version, quality/status; phát hiện no-flow/unexpected-flow/stale node.
- [ ] Gateway publish MQTT heartbeat, group summary, node snapshots/events và fault events; mất MQTT không được dừng scheduler cục bộ.
- [ ] Bench test 12 node đạt các ngưỡng latency/loss/freshness đã chốt từ POC hoặc Sprint 2 test plan.

---

## 2. Contract cấu hình và trạng thái

### 2.1 Treatment và group assignment

```json
{
  "treatmentVersionId": "uuid-or-monotonic-version",
  "timezone": "Asia/Ho_Chi_Minh",
  "day": { "startHour": 6, "endHourExclusive": 18, "onS": 30, "offS": 600 },
  "night": { "onS": 30, "offS": 1800 }
}
```

- M1/M2/M3 là seed ở Backend/provisioning, không là enum firmware.
- Gateway chỉ nhận profile `PUBLISHED`, validate range POC-approved, persist snapshot atomically vào NVS và chỉ thay đổi state khi command version mới hơn config hiện có.
- Group assignment gồm `group_id`, `node_id`, `assignment_version`, `effective_at`. Reject group ngoài 1–4, node ngoài 1–12, profile unpublished, duplicate assignment hoặc update stale version.

### 2.2 Node state contract

```json
{
  "nodeId": 1,
  "desiredPumpState": "ON",
  "reportedPumpState": "ON",
  "pumpFeedbackState": "ON",
  "irrigationState": "FLOW_CONFIRMED",
  "flowLpm": 1.25,
  "deliveredVolumeL": 0.42,
  "calibrationVersion": "node-01-v1",
  "lastAckAt": "...",
  "lastTelemetryAt": "...",
  "healthStatus": "ONLINE"
}
```

`reportedPumpState` và `pumpFeedbackState` không được suy ra từ desired state. `healthStatus` phải biểu thị `ONLINE | STALE | RF_TIMEOUT | NO_FLOW_FAULT | UNEXPECTED_FLOW_FAULT | SENSOR_FAULT | SAFE_OFF`.

---

## 3. MQTT contract

```text
aeroponics/device/{gateway_id}/
  status                                      ← gateway heartbeat/LWT
  telemetry/group/{group_id}                  ← summary group
  telemetry/node/{node_id}/snapshot           ← định kỳ + retained policy được chốt
  telemetry/node/{node_id}/event              ← ACK/state/flow/fault, append-only
  command/config/treatment                    → profile version published
  command/config/assignment                   → group ↔ node mapping versioned
  command/node/{node_id}/override             → manual command có command_id
  command/group/{group_id}/control            → pause/resume/manual group action
  ack/{command_id}                            ← gateway result, không chỉ MQTT receipt
```

- Mọi command MQTT có `command_id`, `issued_at`, `config_version` hoặc `assignment_version`, actor/audit metadata.
- Gateway reject malformed/stale/unauthorised command, publish negative ACK có reason code.
- `ack/{command_id}` chỉ `completed` sau RF outcome; với ON phải bao gồm irrigation outcome (`FLOW_CONFIRMED` hoặc fault), không chỉ `publish()` success.
- LWT QoS 1, retained. Telemetry/state không chứa credential hoặc RF key/config bí mật.

---

## 4. Phân rã tác vụ

### TRACK A — Production RF transport và node controller

| Task ID | Công việc | Done khi |
|---|---|---|
| **A1** | Promote POC RF adapter thành production `IRfTransport` implementation. | UART độc lập debug, timeout/error counters, config từ POC decision record, test host/hardware PASS. |
| **A2** | Implement `RfFrameCodec`. | Frame bounded, CRC/version/length/node-id validation, duplicate response cache, fuzz/regression tests. |
| **A3** | Implement `PumpNodeController`. | Queue bounded, per-node sequence, ACK/NACK/retry/timeout, cancellation, no busy wait. |
| **A4** | Implement `NodeRegistry`. | 12 node maximum, heartbeat freshness, reboot detection, state snapshot thread-safe. |

### TRACK B — Treatment, group và scheduler động

| Task ID | Công việc | Done khi |
|---|---|---|
| **B1** | Implement treatment snapshot/version validation + NVS persistence. | Published version only, atomic update/rollback, no flash write in timer loop. |
| **B2** | Implement versioned group assignment. | Node chỉ có một assignment active, change effective/time-safe, audit event emitted. |
| **B3** | Replace 4 direct-relay scheduler path bằng `GroupScheduler`. | Fan-out per group/node, `UNASSIGNED` never actuates, timezone day/night tests PASS. |
| **B4** | Manual override/pause/resume policy. | Override có TTL/audit, không bypass RF feedback/fail-safe, deterministic recovery to schedule. |

### TRACK C — Pump feedback, flow và safety

| Task ID | Công việc | Done khi |
|---|---|---|
| **C1** | Parse/store node pump feedback + flow telemetry. | Desired/reported/feedback fields riêng biệt, timestamps node/gateway, invalid payload rejected. |
| **C2** | Implement `FlowEvaluator`. | `FLOW_CONFIRMED`, `NO_FLOW_FAULT`, `UNEXPECTED_FLOW_FAULT`, `SENSOR_FAULT`, over-range and stale conditions covered by tests. |
| **C3** | Implement fail-safe policy. | RF timeout/node stale/RTC invalid/fault dẫn đến trạng thái approved safe-off; event reason per node/group. |
| **C4** | Persist only configuration and essential recovery snapshot. | Không ghi NVS trong telemetry/timer loop; reboot recovery documented/tested. |

### TRACK D — MQTT production integration

| Task ID | Công việc | Done khi |
|---|---|---|
| **D1** | Adapt MQTT client/topic ACL từ relay domain sang gateway/group/node domain. | LWT, reconnect bounded, credentials secure, subscriptions least privilege. |
| **D2** | Implement config/override command routing. | DTO/JSON validation, idempotent `command_id`, stale version rejection, no direct GPIO call. |
| **D3** | Publish heartbeat, group/node snapshots and append-only events. | Payload schema versioned, bounded buffers, publish failures tracked, no false completion. |
| **D4** | Mosquitto integration test. | LWT, ACL denial, command lifecycle and offline behavior evidenced. |

### TRACK E — 12-node system test and handoff

| Task ID | Công việc | Done khi |
|---|---|---|
| **E1** | Build node simulator/test harness for 12 identities. | Deterministic ACK/drop/delay/feedback/flow/fault scenarios. |
| **E2** | Hardware bench test 12 node. | Measured latency/loss/freshness/throughput, staggered telemetry prevents collision, results documented. |
| **E3** | Power-cycle and fault injection. | Gateway/node reset, RF outage, no-flow, stuck-flow, sensor disconnect and MQTT loss verified. |
| **E4** | Production readiness review. | All QA blockers PASS, docs/pinout/BOM/config migration ready for Sprint 3. |

---

## 5. QA Gateways — Sprint 2 Production

| Rule ID | PASS khi | Severity |
|---|---|---|
| **S2-RF-01** | Frame validation/CRC/duplicate sequence/ACK retry timeout có host regression tests; không duplicate actuation. | 🔴 BLOCKER |
| **S2-GROUP-02** | Treatment dynamic/versioned; `UNASSIGNED` group không chạy; 12 node assignment dynamic không trùng active group. | 🔴 BLOCKER |
| **S2-PUMP-03** | Desired/reported/pump feedback khác biệt rõ; command ON không success chỉ vì RF ACK. | 🔴 BLOCKER |
| **S2-FLOW-04** | Flow L/min/volume/calibration/status đúng; no-flow, stuck-flow, sensor fault và >6 L/min xử lý an toàn. | 🔴 BLOCKER |
| **S2-SAFE-05** | RF timeout/stale/RTC invalid/power-cycle fail-safe theo policy approved, bounded retry, audited reason. | 🔴 BLOCKER |
| **S2-MQTT-06** | LWT QoS1 retained, credentials không hard-code, ACL least privilege, MQTT loss không phá scheduler local. | 🔴 BLOCKER |
| **S2-12NODE-07** | 12-node simulator + hardware bench evidence thỏa threshold latency/loss/freshness được phê duyệt. | 🔴 BLOCKER |
| **S2-QUALITY-08** | `pio test -e native`, RF integration tests và `pio run -e esp32-s3-devkitc-1` PASS. | 🔴 BLOCKER |

## 6. Handoff sang Sprint 3

Sprint 3 chỉ bắt đầu khi có: schema MQTT versioned, decision record RF/flow, node inventory, mapping/treatment command contract, event samples, test evidence 12 node và danh sách configuration keys cần backend quản lý.

*Sprint 2 Production Planning — thay thế kế hoạch MQTT/direct-relay cũ ngày 2026-08-10.*
