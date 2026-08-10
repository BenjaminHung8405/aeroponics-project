# Aeroponics Lean — Progress Tracker

> **Tracker hiện hành:** Chỉ theo dõi Sprint 1.5. Sprint 0 và Sprint 1 được lưu như prototype/lịch sử; Sprint 2 MQTT direct-relay đã **superseded** và không được QA hoặc đánh dấu hoàn thành theo acceptance criteria cũ.

---

## 📌 Started

| Field | Value |
|---|---|
| **Thời gian cập nhật tracker** | 2026-08-10T21:27:43+07:00 |
| **Sprint hiện hành** | Sprint 1.5 — RF + Flow Proof of Concept & Hardware Decision Gate |
| **Agent thực thi (Execution Agent)** | Gemini |
| **Senior Solution Architect** | QA độc lập trước khi chuyển bất kỳ task nào sang `[x] Done` |

---

## 🎯 Reference Plan

- **Thư mục kế hoạch:** `.ai/planning/aeroponics-lean/`
- **Sprint tham chiếu hiện tại:** [`sprint_1_5.md`](./sprint_1_5.md)
- **Tài liệu kiến trúc ưu tiên:** [`PROJECT_ALIGNMENT_2026-08-10.md`](./PROJECT_ALIGNMENT_2026-08-10.md)
- **Ngữ cảnh tổng quan:** [`README.md`](./README.md)

---

## 📝 Addition Plan

- ESP32 gateway giao tiếp với 12 node/cụm bơm qua UART over RF 433 MHz; POC chỉ dùng 1 gateway + 1 node để lựa chọn phần cứng và giảm rủi ro trước khi scale lên 12 node.
- Treatment/version và mapping node → group là dữ liệu cấu hình động. Không hard-code M1–M4, mapping 3 node/group, RF baud rate, pinout hoặc ngưỡng flow vào firmware production.
- Xác nhận tưới thành công bắt buộc theo chuỗi: `RF_ACKED → PUMP_FEEDBACK_ON → FLOW_CONFIRMED`. ACK RF không đủ để kết luận nước đã được tưới.
- Flow sensor pulse output tối đa 6 L/min cần calibration riêng theo node (`pulses_per_litre`), lưu `flow_lpm` và `delivered_volume_l`.
- Chưa có module RF, protocol UART-RF hoặc driver flow tái sử dụng trong hai codebase đã rà soát. Không chốt BOM, anten, UART baud/mode/pinout trước POC và decision record được phê duyệt.
- Tuya PH-W218 chỉ đo on-demand/cuối vụ; không thuộc phạm vi Sprint 1.5.

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
| **A1** | Lập inventory candidate RF 433 MHz, node MCU, relay/driver bơm, nguồn, anten và flow sensor. Tạo `docs/RF_FLOW_POC_DECISION.md` với part number, datasheet URL/path, điện áp, dòng, interface và rủi ro. | [ ] Pending | Dùng **Architecture Decision Record (ADR)**: ghi lựa chọn, alternatives, trade-offs và tiêu chí reject. Candidate LoRa UART transparent chỉ là lựa chọn POC, không tự động thành BOM. Bắt buộc kiểm tra availability, công suất RF/duty-cycle/quy định địa phương, mức logic UART và ngân sách nguồn lúc pump khởi động. |
| **A2** | Vẽ wiring diagram POC cho gateway ↔ RF ↔ node, driver pump, pump feedback và flow sensor. | [ ] Pending | Áp dụng **Hardware Interface Contract**. Thể hiện TX/RX chéo, GND chung, level shifting, decoupling, fuse/protection, nguồn RF tách nhiễu nguồn pump và GPIO assignment. USB debug Serial phải tách UART RF. Cấm dùng GPIO bootstrap/strapping ESP32 nếu chưa chứng minh boot-safe; không điều khiển tải AC/DC trực tiếp từ GPIO. |
| **A3** | Chọn và chứng minh cơ chế pump feedback tại node. | [ ] Pending | Áp dụng **defence in depth**: relay/driver state chỉ là lớp một; ưu tiên thêm current sensing hoặc auxiliary contact khi phần cứng cho phép. Tài liệu phải nêu rõ failure modes phát hiện được/không phát hiện được; không gọi relay output là “pump thực tế chạy”. |
| **A4** | Chuẩn bị flow bench: nguồn nước, đường ống, bình đo thể tích chuẩn và quy trình hiệu chuẩn an toàn. | [ ] Pending | Áp dụng **measurement traceability**. Quy trình phải có thể lặp lại, ghi nhiệt độ/nước/áp lực nếu ảnh hưởng, thể tích tham chiếu, số lần lặp và cách tính sai số. Bố trí chống rò điện/nước, bảo vệ bơm không chạy khô và có van ngắt khẩn cấp. |

## TRACK B — RF Transport POC

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật cấp cao |
|---|---|---|---|
| **B1** | Khai báo `IRfTransport`, `RfFrameCodec` và constants cấu hình tách hardware adapter; viết unit test encode/decode/CRC/length/version/node-id/duplicate sequence. | [ ] Pending | Áp dụng **Ports and Adapters / Dependency Inversion**: codec thuần C++ không phụ thuộc Arduino UART hay module RF. Parser là bounded state machine, không cấp phát động từ payload. Frame phải có SOF, version, message type, node ID, sequence, payload length, payload, CRC-16. Fuzz/regression test mọi frame malformed; reject fail-closed. |
| **B2** | Implement UART adapter cho RF candidate; gateway và node trao đổi `PING/PONG` ổn định, tách khỏi debug Serial. | [ ] Pending | Adapter chỉ implement `IRfTransport`; không trộn framing/protocol/business logic. Dùng non-blocking read/write, RX buffer có giới hạn, timeout và counters TX/RX/CRC/drop. Không log trong ISR; log phải rate-limit để không gây starvation cho UART/RF. |
| **B3** | Implement command manager POC cho `SET_PUMP`, `COMMAND_ACK`, timeout, bounded retry và idempotency. | [ ] Pending | Áp dụng **Command pattern + finite-state machine**. Mỗi lệnh có `command_id` và sequence; duplicate phải trả lại outcome đã xử lý, tuyệt đối không actuate lần hai. Queue bounded, retry/backoff/timeout cấu hình được, không busy-wait hoặc retry vô hạn. Command failure phải có reason code có thể audit. |
| **B4** | Đo RF tại vị trí triển khai: latency/loss theo khoảng cách, vật cản, power-cycle reconnect và link quality nếu hỗ trợ. | [ ] Pending | Áp dụng **evidence-based acceptance**: ghi firmware revision, wiring revision, RF config, anten, vị trí, vật cản, số sample, p50/p95/p99 round-trip latency và packet loss. Không suy luận performance 12 node từ 1 ping; kết quả chỉ dùng để chọn candidate và thiết kế benchmark Sprint 2. |

## TRACK C — Pump Feedback & Flow Measurement POC

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật cấp cao |
|---|---|---|---|
| **C1** | Implement node actuator + telemetry `desired`, `reported`, `driver` và `pump_feedback`; kiểm tra ON/OFF thực tế. | [ ] Pending | Áp dụng **explicit-state model**: không suy ra `reportedPumpState`/`pumpFeedbackState` từ desired state. Timestamps node và gateway phải phân biệt; chỉ update state sau evidence tương ứng. Default boot/fault state là OFF; không cho command path gọi GPIO từ MQTT callback. |
| **C2** | Implement pulse counter flow bằng ISR hoặc counter phần cứng và conversion L/min. | [ ] Pending | ISR chỉ tăng counter atomic/hardware counter, cấm I/O, allocation, logging hoặc blocking call trong ISR. Sử dụng snapshot atomic để tính `flow_lpm`, `delivered_volume_l`, `pulse_count`, `sample_window_ms`; validate overflow, sensor disconnect và over-range >6 L/min. Có host unit test conversion/calibration math. |
| **C3** | Hiệu chuẩn flow sensor tối thiểu 3 lần trên node prototype; tính `pulses_per_litre`, sai số và lưu calibration version. | [ ] Pending | Áp dụng **calibration as versioned configuration**. Không hard-code hệ số chung cho mọi node. Lưu raw data, thể tích tham chiếu, kết quả từng trial, mean/variance/sai số; reject calibration ngoài ngưỡng. Không overwrite calibration đang dùng mà không tạo version/audit record. |
| **C4** | Implement và test flow/fault evaluation: `FLOW_CONFIRMED`, `NO_FLOW_FAULT`, `UNEXPECTED_FLOW_FAULT`, invalid input và over-range. | [ ] Pending | Áp dụng **safety state machine**. ON chỉ success sau `RF_ACKED → PUMP_FEEDBACK_ON → FLOW_CONFIRMED`; ACK riêng lẻ không đủ. Ngưỡng `min_flow_lpm`, `max_off_flow_lpm`, `flow_start_timeout_s` phải configurable theo node. Fault phải latch/audit, retry bounded và transition approved safe-off; không tự clear fault khi telemetry chập chờn. |

## TRACK D — Evidence, QA & Decision Gate

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật cấp cao |
|---|---|---|---|
| **D1** | Viết test matrix và chạy QA POC; lưu evidence timestamp, firmware revision, wiring revision, điều kiện, expected/actual/result. | [ ] Pending | Áp dụng **traceable verification matrix**: map từng case tới `S1.5-*`, source commit/revision và evidence path. Bao gồm CRC/length/version lỗi, duplicate, ACK/NACK/timeout, pump ON/OFF, no-flow, stuck-flow, sensor invalid, RF loss và power-cycle. Không coi screenshot/log đơn lẻ là evidence đầy đủ. |
| **D2** | Review fail-safe cho power loss gateway/node, RF timeout, RTC invalid và sensor fault. | [ ] Pending | Áp dụng **fail-safe by default** và FMEA tối thiểu. Mỗi failure mode phải có detection, actuator state, retry policy, user-visible fault, reset/recovery procedure. Không có trạng thái `RUNNING` giả khi actuator/node đã fault hoặc stale. |
| **D3** | Ra quyết định BOM/protocol qua `RF_FLOW_POC_DECISION.md`: phê duyệt hoặc reject candidate với remediation rõ ràng. | [ ] Pending | Chỉ được QA Review khi toàn bộ blocker `S1.5-RF-01..03`, `S1.5-FLOW-04..05`, `S1.5-SAFE-06`, `S1.5-QUALITY-08` PASS và `S1.5-RF-07` có evidence. Decision record phải chốt/đề xuất BOM, anten, mode/baud/pinout, protocol version, calibration procedure, security posture và open risks; không mở Sprint 2 khi thiếu sign-off. |

---

## 🛡️ QA Gateways — Sprint 1.5 POC

| Rule ID | Tiêu chí PASS / FAIL | Severity |
|---|---|---|
| **S1.5-RF-01** | Parser reject CRC/length/version sai; duplicate sequence không kích pump lần hai. | 🔴 BLOCKER |
| **S1.5-RF-02** | ON/OFF có command ID, ACK/NACK/timeout/bounded retry và log outcome có thể audit. | 🔴 BLOCKER |
| **S1.5-RF-03** | ON chỉ được coi là tưới thành công sau `RF_ACKED → PUMP_FEEDBACK_ON → FLOW_CONFIRMED`. | 🔴 BLOCKER |
| **S1.5-FLOW-04** | Calibration có evidence; `flow_lpm` và `delivered_volume_l` đạt sai số chấp nhận được đã ghi trong decision record. | 🔴 BLOCKER |
| **S1.5-FLOW-05** | No-flow sau ON tạo `NO_FLOW_FAULT`; OFF còn flow tạo `UNEXPECTED_FLOW_FAULT`. | 🔴 BLOCKER |
| **S1.5-SAFE-06** | Mất nguồn, RF timeout hoặc sensor fault không gây command lặp vô hạn; actuator giữ/đi safe-off. | 🔴 BLOCKER |
| **S1.5-RF-07** | Field test có latency/loss và candidate RF được kết luận bằng decision record. | 🟠 CRITICAL |
| **S1.5-QUALITY-08** | `pio test -e native` và `pio run -e esp32-s3-devkitc-1` PASS từ `aeroponics-firmware/`; không có secret tracked. | 🔴 BLOCKER |

### Điều kiện đóng Sprint 1.5

- Toàn bộ task A1–D3 đạt `[x] Done`.
- Tất cả rule BLOCKER PASS; rule CRITICAL có evidence và quyết định rõ ràng.
- `docs/RF_FLOW_POC_DECISION.md`, wiring versioned, `docs/RF_PROTOCOL.md`, flow calibration procedure, test matrix và evidence raw đã tồn tại.
- Senior Solution Architect review độc lập và cập nhật kết quả vào `WALKTHROUGH_LOG.md` trước khi Sprint 2 Production được mở.

---

*Senior Solution Architect — Tracker chuẩn hoá cho Sprint 1.5 ngày 2026-08-10.*
