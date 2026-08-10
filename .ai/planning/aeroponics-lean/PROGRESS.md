# Aeroponics Lean — Progress Tracker

> **Tracker hiện hành:** Chỉ theo dõi Sprint 1.5. Sprint 0 và Sprint 1 được lưu như prototype/lịch sử; Sprint 2 MQTT direct-relay đã **superseded** và không được QA hoặc đánh dấu hoàn thành theo acceptance criteria cũ.

---

## 📌 Started

| Field | Value |
|---|---|
| **Thời gian bắt đầu/cập nhật tracker** | 2026-08-10T21:36:45+07:00 |
| **Sprint hiện hành** | Sprint 1.5 — RF + Flow Proof of Concept & Hardware Decision Gate |
| **Agent thực thi (Execution Agent)** | Gemini |
| **Senior Solution Architect** | QA độc lập trước khi chuyển bất kỳ task nào sang `[x] Done` |

---

## 🎯 Reference Plan

- **Thư mục kế hoạch:** `.ai/planning/aeroponics-lean/`
- **File sprint tham chiếu hiện tại:** `.ai/planning/aeroponics-lean/sprint_1_5.md` ([liên kết](./sprint_1_5.md))
- **Tài liệu kiến trúc ưu tiên:** [`PROJECT_ALIGNMENT_2026-08-10.md`](./PROJECT_ALIGNMENT_2026-08-10.md)
- **Ngữ cảnh tổng quan:** [`README.md`](./README.md)

---

## 📝 Addition Plan

Các yêu cầu phát sinh dưới đây là **BLOCKER** cho Go/No-Go của Sprint 1.5; chúng bổ sung acceptance contract của Gemini, không thay đổi phạm vi POC 1 gateway + 1 node.

1. **Node-side lease/deadman:** `SET_PUMP(ON)` bắt buộc mang `run_lease_ms`/`max_on_duration_ms`; node boot/reset phải OFF trước UART/RF/application và tự force OFF khi lease hết hạn. Phải bench-test gateway mất nguồn lúc pump ON, có log `LEASE_EXPIRED_SAFE_OFF` và thời gian tắt đạt ngưỡng đã chốt.
2. **Wire protocol có thể liên thông:** tạo/version `docs/RF_PROTOCOL.md` **trước code codec** với SOF, version, endian, CRC-16 variant + test vectors, giới hạn buffer/payload/timeout, numeric enums, payload schemas, sequence wrap/reboot và correlation `command_id` hoặc `{boot_session_id, sequence}`.
3. **RF security:** CRC không phải authentication. Bắt buộc MAC/HMAC ứng dụng với key provisioned ngoài Git, anti-replay bằng boot-session/counter, policy provisioning/rotation/reset. Nếu chấp nhận lab air-gapped không MAC, ADR phải có risk acceptance và người phê duyệt; key không được có trong source, log hay evidence.
4. **FMEA và recovery semantics:** định nghĩa detection, node action, gateway action, escalation và fault-reset cho RF timeout, gateway/node reboot, no/unexpected flow, sensor lỗi, pump feedback mismatch, RTC invalid; chốt policy tắt node đơn lẻ hay dừng toàn group.
5. **Pump feedback được phân loại đúng:** tách `driver_feedback` khỏi `load_feedback`; hoặc ghi rõ giới hạn POC khi chỉ driver feedback + flow confirmation, hoặc định nghĩa current/aux-contact sensing (ngưỡng, debounce, open-load/stall/stuck-relay và isolation).
6. **Test plan trước bench:** version `docs/RF_FLOW_POC_TEST_PLAN.md` với acceptance threshold phê duyệt trước test, sample size, ON/OFF/retry/timeout, EMI khi pump switching, wet foliage/vật cản, RF configuration và breakdown latency.
7. **Calibration định lượng:** công thức, dải vận hành/multi-point, điều kiện thử, reference volume, repeatability/reject threshold và cấu hình theo sensor serial/node/version; xử lý overflow, noise/bounce, stale/zero/over-range flow.
8. **Heartbeat/staleness contract:** chốt telemetry ON/OFF/fault, heartbeat interval, stale threshold, boot-session recovery; test cắt nguồn node → gateway `STALE/RF_TIMEOUT`, và node online lại không tự resume ON nếu không có command/lease hợp lệ.
9. **Electrical/water/EMI safety:** đặc tính pump (DC/AC, nominal/inrush/stall), driver margin, flyback/snubber/MOV, fuse/TVS/reverse-polarity/isolation/connector rating, brownout/EMI/RF decoupling và emergency isolation phải được review/test.

---

## 📋 Quy ước trạng thái

| Status | Ý nghĩa |
|---|---|
| `[ ] Pending` | Chưa bắt đầu; không có source/evidence thực hiện. |
| `[ ] In Progress` | Gemini đang thực hiện task; chưa đủ evidence để review. |
| `[ ] QA Review` | Implementation/evidence đã hoàn tất, chờ Senior Solution Architect review độc lập. |
| `[x] Done` | Đã PASS toàn bộ gate áp dụng, evidence được ghi vào `WALKTHROUGH_LOG.md`, và được duyệt nghiêm ngặt. |

> **Quy tắc không thương lượng:** Không chuyển task sang `[x] Done` chỉ dựa vào compile/unit test. Các task RF, pump feedback, flow, fail-safe phải có evidence bench/field test tương ứng. Mọi secret, local key, credential MQTT/Wi-Fi và khóa RF phải ở file ignored hoặc secure provisioning, tuyệt đối không xuất hiện trong source/log/evidence.

---

# 🚧 Sprint 1.5 — RF + Flow Proof of Concept & Hardware Decision Gate

> **Mục tiêu:** Chọn được phương án RF 433 MHz và chứng minh end-to-end command/ACK/pump feedback/flow confirmation với 1 gateway + 1 node. Sprint 2 Production chỉ được mở khi Sprint 1.5 PASS.

## TRACK A — Hardware Discovery & Decision Record

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật cấp cao |
|---|---|---|---|
| **A1** | Lập inventory candidate RF 433 MHz, node MCU, relay/driver bơm, nguồn, anten và flow sensor. Tạo `docs/RF_FLOW_POC_DECISION.md` với part number, datasheet URL/path, điện áp, dòng, interface và rủi ro. | [ ] Pending | Dùng **ADR**: alternatives, trade-off, tiêu chí reject và owner phê duyệt. Candidate LoRa UART transparent chỉ là POC, không tự thành BOM. Bắt buộc kiểm tra availability, RF regulation/duty-cycle, UART logic, công suất phát, anten, pump nominal/inrush/stall, rating driver có margin và ngân sách nguồn/brownout. |
| **A2** | Vẽ wiring diagram POC cho gateway ↔ RF ↔ node, driver pump, pump feedback và flow sensor. | [ ] Pending | Áp dụng **Hardware Interface Contract**. Nêu TX/RX, GND/topology hoặc isolation, level shifting, decoupling/RF rail, fuse, TVS, reverse-polarity, connector rating và emergency isolation. DC phải có flyback; AC phải có snubber/MOV/contactor strategy. Tách USB debug/UART RF, tránh GPIO strapping; test EMI/RF reset khi pump switching và bố trí anten tránh dây pump. |
| **A3** | Chọn và chứng minh cơ chế pump feedback tại node. | [ ] Pending | Áp dụng **defence in depth**. Phân biệt bắt buộc `driver_feedback` với `load_feedback`; không gọi relay output là pump đang chạy. Nếu POC chỉ driver feedback + flow, ghi rõ failure modes điện không phát hiện được. Nếu current/aux sensing, chốt threshold, debounce, open-load/stall/stuck-relay coverage, ADC/filtering/isolation và test evidence. |
| **A4** | Chuẩn bị flow bench: nguồn nước, đường ống, bình đo thể tích chuẩn và quy trình hiệu chuẩn an toàn. | [ ] Pending | Áp dụng **measurement traceability**. Version calibration procedure: reference volume tối thiểu, multi-point operating range, nước/áp lực/van-nozzle/pump supply/nhiệt độ, ≥3 trial, variance/repeatability/reject threshold. Có công thức `pulses_per_litre`, `flow_lpm`, `delivered_volume_l`; bảo vệ rò nước/điện, chống chạy khô và van ngắt khẩn. |

## TRACK B — RF Transport POC

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật cấp cao |
|---|---|---|---|
| **B1** | Viết/version `docs/RF_PROTOCOL.md`; khai báo `IRfTransport`, `RfFrameCodec` và constants tách hardware adapter; unit test encode/decode/CRC/length/version/node-id/duplicate. | [ ] Pending | Áp dụng **Ports and Adapters / Dependency Inversion**: codec C++ thuần, parser bounded/fail-closed, không allocation động. Spec trước code phải chốt SOF, version, endian, CRC polynomial/init/reflection/test vectors, numeric enums, payload byte schema, MAX payload/RX buffer/inter-byte timeout, sequence wrap, boot session và correlation. MAC/HMAC + anti-replay là bắt buộc trừ ADR risk acceptance được ký; secret chỉ qua secure provisioning. Fuzz malformed frames. |
| **B2** | Implement UART adapter cho RF candidate; gateway và node trao đổi `PING/PONG` ổn định, tách khỏi debug Serial. | [ ] Pending | Adapter chỉ implement `IRfTransport`; không trộn framing/protocol/business logic. Non-blocking I/O, bounded RX buffer, timeout, TX/RX/CRC/drop counters; không log ISR, rate-limit log. Chốt heartbeat interval, telemetry ON/OFF/fault và stale threshold; test node power-off → `STALE/RF_TIMEOUT`, node reboot tạo boot-session mới và không tự resume ON. |
| **B3** | Implement command manager POC cho `SET_PUMP`, `COMMAND_ACK`, timeout, bounded retry, idempotency và node-side lease. | [ ] Pending | Áp dụng **Command pattern + safety FSM**. Mỗi command có `command_id`, boot-session/sequence và `run_lease_ms`; duplicate trả outcome cũ, không actuate hai lần. Queue/retry/backoff bounded, không busy-wait. Node boot OFF trước stack; lease expiry force OFF độc lập gateway, audit `LEASE_EXPIRED_SAFE_OFF`. Test gateway loss khi ON và replay/unauthenticated command rejection. |
| **B4** | Đo RF tại vị trí triển khai: latency/loss theo khoảng cách, vật cản, power-cycle reconnect và link quality nếu hỗ trợ. | [ ] Pending | Chỉ chạy theo `docs/RF_FLOW_POC_TEST_PLAN.md` đã chốt trước bench: sample size và PASS/FAIL không được đổi hậu nghiệm. Ghi firmware/wiring/RF config (frequency/channel/data rate/TX power/anten), foliage ướt/vật cản xấu nhất, pump switching EMI, ON/OFF/retry/timeout, p50/p95/p99 và breakdown UART/airtime/node/ACK/flow. Không suy luận 12-node performance từ POC. |

## TRACK C — Pump Feedback & Flow Measurement POC

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật cấp cao |
|---|---|---|---|
| **C1** | Implement node actuator + telemetry `desired`, `reported`, `driver` và `pump_feedback`; kiểm tra ON/OFF thực tế. | [ ] Pending | Áp dụng **explicit-state model**: không suy ra `reportedPumpState`/`pumpFeedbackState` từ desired. Phân biệt timestamp node/gateway và correlation command; update state chỉ sau evidence. Boot/fault OFF, command path không gọi GPIO từ MQTT callback. Telemetry phải thể hiện `driver_feedback`/`load_feedback`, boot session, health/stale context. |
| **C2** | Implement pulse counter flow bằng ISR hoặc counter phần cứng và conversion L/min. | [ ] Pending | ISR chỉ tăng counter atomic/hardware counter; cấm I/O/allocation/log/blocking. Snapshot atomic tính `flow_lpm`, `delivered_volume_l`, `pulse_count`, `sample_window_ms`; xử lý counter reset/overflow, bounce/noise, pulse bất thường, zero/stale/disconnect và >6 L/min. Có host unit test conversion/calibration math và input boundary. |
| **C3** | Hiệu chuẩn flow sensor tối thiểu 3 lần trên node prototype; tính `pulses_per_litre`, sai số và lưu calibration version. | [ ] Pending | Áp dụng **calibration as versioned configuration** theo sensor serial + node ID + version, không hard-code hệ số chung. Lưu raw trials, reference volume, điều kiện, mean/variance/repeatability/sai số; đo nhiều điểm dải vận hành và reject theo threshold định lượng đã phê duyệt. Không overwrite calibration active nếu không có version/audit. |
| **C4** | Implement và test flow/fault evaluation: `FLOW_CONFIRMED`, `NO_FLOW_FAULT`, `UNEXPECTED_FLOW_FAULT`, invalid input và over-range. | [ ] Pending | Áp dụng **safety FSM**. ON chỉ success sau `RF_ACKED → PUMP_FEEDBACK_ON → FLOW_CONFIRMED`; ACK riêng lẻ không đủ. `min_flow_lpm`, `max_off_flow_lpm`, `max_flow_lpm`, `flow_start_timeout_s` configurable per node/treatment và chốt trước test. Fault latch/audit + safe-off; không tự clear vì telemetry chập chờn. |

## TRACK D — Evidence, QA & Decision Gate

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật cấp cao |
|---|---|---|---|
| **D1** | Viết/version `docs/RF_FLOW_POC_TEST_PLAN.md`, test matrix và chạy QA POC; lưu evidence timestamp, firmware/wiring revision, điều kiện, expected/actual/result. | [ ] Pending | Áp dụng **traceable verification matrix**: threshold + sample size/PASS-FAIL phải được duyệt trước bench; map case tới `S1.5-*`, commit và raw evidence. Bao gồm malformed/auth/replay/duplicate, ACK/NACK/timeout, lease gateway-loss, ON/OFF, no/stuck flow, sensor invalid, RF loss/power-cycle, heartbeat/stale recovery, brownout và EMI pump switching. Screenshot/log đơn lẻ không đủ. |
| **D2** | Review fail-safe cho power loss gateway/node, RF timeout, RTC invalid, pump feedback mismatch và sensor fault. | [ ] Pending | Áp dụng **fail-safe by default + FMEA** versioned: RF timeout, gateway/node reboot, no/unexpected flow, sensor disconnect/stale, feedback mismatch, RTC invalid. Mỗi mode có detection, node action, gateway action, retry/escalation, latch/reset/recovery và owner. Phải chốt node-only OFF hay group-stop; không có `RUNNING` giả khi node fault/stale. |
| **D3** | Ra quyết định BOM/protocol qua `RF_FLOW_POC_DECISION.md`: phê duyệt hoặc reject candidate với remediation rõ ràng. | [ ] Pending | Chỉ QA Review khi toàn bộ blocker PASS và RF evidence đủ. Decision record phải link `RF_PROTOCOL.md`, test plan/FMEA/calibration, chốt/đề xuất BOM/anten/mode/baud/pinout, lease, heartbeat/stale, security posture/risk acceptance, electrical-water-EMI safety và open risks. Không mở Sprint 2 nếu thiếu sign-off độc lập. |

---

## 🛡️ QA Gateways — Sprint 1.5 POC

| Rule ID | Tiêu chí PASS / FAIL | Severity |
|---|---|---|
| **S1.5-RF-01** | Parser reject CRC/length/version sai; duplicate sequence không kích pump lần hai. | 🔴 BLOCKER |
| **S1.5-RF-02** | ON/OFF có command ID, ACK/NACK/timeout/bounded retry và log outcome có thể audit. | 🔴 BLOCKER |
| **S1.5-RF-03** | ON chỉ được coi là tưới thành công sau `RF_ACKED → PUMP_FEEDBACK_ON → FLOW_CONFIRMED`. | 🔴 BLOCKER |
| **S1.5-SAFE-04** | `SET_PUMP(ON)` có lease; node boot OFF và force OFF khi gateway/RF biến mất trước lease deadline, có evidence `LEASE_EXPIRED_SAFE_OFF`. | 🔴 BLOCKER |
| **S1.5-PROTO-05** | `RF_PROTOCOL.md` chốt wire contract, correlation/reboot/sequence semantics, CRC vectors, bounded parser và MAC/HMAC anti-replay; hoặc có ADR risk acceptance được ký. | 🔴 BLOCKER |
| **S1.5-FLOW-04** | Calibration có evidence; `flow_lpm` và `delivered_volume_l` đạt sai số chấp nhận được đã ghi trong decision record. | 🔴 BLOCKER |
| **S1.5-FLOW-05** | No-flow sau ON tạo `NO_FLOW_FAULT`; OFF còn flow tạo `UNEXPECTED_FLOW_FAULT`. | 🔴 BLOCKER |
| **S1.5-SAFE-06** | Mất nguồn, RF timeout hoặc sensor fault không gây command lặp vô hạn; actuator giữ/đi safe-off. | 🔴 BLOCKER |
| **S1.5-OPS-07** | Heartbeat/telemetry/stale/recovery contract PASS; node reboot/online lại không tự resume ON nếu thiếu command/lease hợp lệ. | 🔴 BLOCKER |
| **S1.5-HW-08** | Electrical/water/EMI safety checklist PASS: protection/rating/isolation, brownout và pump-switching RF test có raw evidence. | 🔴 BLOCKER |
| **S1.5-RF-07** | Field test có latency/loss và candidate RF được kết luận bằng decision record. | 🟠 CRITICAL |
| **S1.5-QUALITY-08** | `pio test -e native` và `pio run -e esp32-s3-devkitc-1` PASS từ `aeroponics-firmware/`; không có secret tracked. | 🔴 BLOCKER |

### Điều kiện đóng Sprint 1.5

- Toàn bộ task A1–D3 đạt `[x] Done`.
- Tất cả rule BLOCKER PASS; rule CRITICAL có evidence và quyết định rõ ràng.
- `docs/RF_FLOW_POC_DECISION.md`, wiring versioned, `docs/RF_PROTOCOL.md`, `docs/RF_FLOW_POC_TEST_PLAN.md`, FMEA, flow calibration procedure, test matrix và raw evidence đã tồn tại.
- Senior Solution Architect review độc lập và cập nhật kết quả vào `WALKTHROUGH_LOG.md` trước khi Sprint 2 Production được mở.

---

*Senior Solution Architect — Tracker chuẩn hoá cho Sprint 1.5 ngày 2026-08-10.*
