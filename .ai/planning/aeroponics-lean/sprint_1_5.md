# Sprint 1.5: RF + Flow Proof of Concept & Hardware Decision Gate

> **Phụ thuộc:** Sprint 1 chỉ được dùng làm prototype cho boot-safe, RTC, NVS và watchdog. Không sử dụng abstraction 4 relay GPIO như kiến trúc production.  
> **Mục tiêu:** Giảm rủi ro phần cứng trước khi viết firmware production: chọn được phương án RF 433 MHz, thiết kế protocol mới cho MEGA8, chứng minh end-to-end command/ACK/feedback/lưu lượng với 1 gateway + 1 node, sau đó mở rộng kiểm thử tới 4 node và ban hành quyết định BOM/pinout/protocol.
> **Không phải output:** Đây chưa phải firmware production hoàn chỉnh. Scope hiện tại cố định 4 node; không suy luận khả năng mở rộng 12 node từ kế hoạch lịch sử.

> **Trạng thái Go/No-Go:** **NO-GO** cho đến khi các blocker về command lease, wire protocol, RF authentication/anti-replay, FMEA, test plan định lượng, calibration, heartbeat/staleness và electrical/water/EMI safety trong tài liệu này được hoàn thành và review độc lập. Tài liệu này là **acceptance contract**; `PROGRESS.md` chỉ theo dõi trạng thái và liên kết evidence.

> **Tài liệu ưu tiên:** [PROJECT_ALIGNMENT_2026-08-10.md](./PROJECT_ALIGNMENT_2026-08-10.md).

---

## 1. Phạm vi POC

### 1.1 Topology tối thiểu

```text
ESP32-S3 gateway ── UART ── RF 433 MHz transceiver
                              )) 433 MHz ((
                  RF transceiver ── UART ── MEGA8 node (x4)
                                             ├─ existing schedule/timer
                                             ├─ relay/driver pump
                                             ├─ pump feedback input
                                             └─ flow sensor (pulse output)
```

| Thành phần | Yêu cầu POC |
|---|---|
| Gateway | ESP32-S3 hiện có, USB Serial chỉ dành cho debug; UART RF riêng không dùng chung cổng log. |
| RF candidates | Chưa có model. Inventory và bench-test các candidate RF UART 433 MHz; Ebyte E32/E220 chỉ là ví dụ tham khảo, chưa phải BOM đã phê duyệt. |
| Node | MEGA8 hiện hữu, được phép sửa firmware; giữ schedule/timer nội bộ, bổ sung protocol adapter/telemetry/ACK mà không chuyển scheduler sang ESP32. |
| Flow sensor | Loại pulse output, dải đo tối đa 6 L/min; có thể hiệu chuẩn `pulses_per_litre` từng sensor. |
| Pump feedback | Phải phân biệt `driver_feedback` (relay/driver đã nhận lệnh) và `load_feedback` (dòng tải/auxiliary contact nếu có). Nếu POC chỉ có driver feedback + flow confirmation, phải ghi rõ các lỗi điện của pump không phát hiện được. |

### 1.2 Tiêu chí quyết định RF

- Tầm phủ phải được đo tại vị trí gần nhất/xa nhất, qua vật cản thực tế của khu trồng.
- Báo cáo packet loss, round-trip command ACK latency p50/p95/p99, reconnect sau power-cycle và chất lượng link nếu module hỗ trợ.
- Phương án được chọn phải có datasheet rõ ràng, nguồn hàng ổn định, anten phù hợp, nguồn cấp phù hợp và tuân thủ quy định RF/công suất tại nơi lắp đặt.
- Không chốt baud rate, UART pinout, công suất phát hay mode RF trước khi POC/datasheet xác nhận.
- Trước bench phải version `docs/RF_FLOW_POC_TEST_PLAN.md`: điều kiện test, sample size, acceptance threshold/PASS-FAIL được phê duyệt trước khi thấy kết quả; không chốt ngưỡng hậu nghiệm.

---

## 2. Hợp đồng giao thức POC

### 2.1 Application frame (bắt buộc dù module dùng transparent UART)

```text
SOF(2) | protocol_version(1) | message_type(1) | node_id(1) |
gateway_boot_session_id(4) | sequence(2) | payload_length(1) |
payload(0..MAX_PAYLOAD_LENGTH) | CRC-16(2) | MAC(TAG_LENGTH)
```

- `docs/RF_PROTOCOL.md` là deliverable **trước B1** và là nguồn sự thật wire-level cho cả ESP32-S3 và MEGA8. Phải bổ sung/kiểm chứng feasibility HMAC-SHA256 trên ATmega8 (flash/RAM/CPU), hoặc ghi rõ adapter phần cứng được phê duyệt; không được giả định MEGA8 có đủ crypto chỉ vì ESP32 có. Tài liệu phải chốt giá trị cụ thể `SOF`, protocol version hiện hành, byte order, `MAX_PAYLOAD_LENGTH`, giới hạn RX buffer, inter-byte/frame timeout, `MAC` algorithm/tag length và test vectors. Không bắt đầu codec khi chưa có spec versioned.
- CRC-16 chỉ kiểm tra lỗi truyền dẫn, không xác thực nguồn gửi. `RF_PROTOCOL.md` phải nêu polynomial, init, reflected/non-reflected, byte order CRC và expected test vectors. Parser là state machine bounded, fail-closed, không cấp phát động; reject `SOF`, length, version, node ID, CRC hoặc MAC sai.
- Phải có numeric enum bảng cho `message_type`, ACK/NACK reason code, FAULT code và schema payload byte-level tối thiểu cho `SET_PUMP`, `COMMAND_ACK`, `TELEMETRY`, `FAULT_REPORT`, `PING`, `PONG`.
- `sequence` có wrap-around semantics; `{gateway_boot_session_id, sequence, command_id}` là correlation key. Telemetry/feedback/flow/fault phải mang `command_id` hoặc correlation key tương đương để không gán frame trễ sau retry/power-cycle cho command mới. Node reboot phải tạo boot-session node mới; gateway invalidate command pending khi session đổi.
- Application-layer MAC/HMAC với key provisioned ngoài Git là mặc định bắt buộc; key/counter không được xuất hiện trong source, log, raw evidence hoặc ADR. Protocol phải định nghĩa provisioning, rotation, reset/revocation và anti-replay counter/window sau reboot. Nếu POC air-gapped xin miễn MAC, `RF_FLOW_POC_DECISION.md` phải có risk acceptance được người có thẩm quyền ký; transparent UART hoặc mã hóa RF riêng không mặc định được coi là đủ an toàn.
- Duplicate command phải trả lại outcome đã xử lý, không kích pump lần hai. Các message tối thiểu: `PING`, `PONG`, `SET_PUMP`, `COMMAND_ACK`, `TELEMETRY`, `FAULT_REPORT`.
- `SET_PUMP` phải chứa `desired_state`, `command_id` và `run_lease_ms`/`max_on_duration_ms`; `COMMAND_ACK` chỉ báo accepted/rejected, không được thay thế pump/flow feedback. `OFF` là temporary override: payload phải có semantics resume/expiry để MEGA8 quay lại schedule tại điểm đã chốt; không dùng echo payload trong production.

### 2.2 State machine xác nhận tưới

```text
COMMAND_SENT → RF_ACKED → PUMP_FEEDBACK_ON → FLOW_CONFIRMED
                    └──→ NACK / TIMEOUT / PUMP_FEEDBACK_FAULT / NO_FLOW_FAULT
```

- Lệnh ON chỉ được coi là **tưới đã xác nhận** ở `FLOW_CONFIRMED`.
- Node phải khởi động/reset với output pump **OFF trước** UART, RF và application stack. Khi nhận ON, node bắt đầu command lease độc lập gateway và **force OFF** trước/đúng lease deadline nếu không có lệnh hợp lệ thay thế; ghi/telemetry `LEASE_EXPIRED_SAFE_OFF`. Không được gia hạn lease chỉ bằng retry/telemetry không correlation hợp lệ.
- Nếu đã ACK nhưng không đạt `min_flow_lpm` trong `flow_start_timeout_s`: `NO_FLOW_FAULT`.
- Nếu OFF mà lưu lượng vượt `max_off_flow_lpm`: `UNEXPECTED_FLOW_FAULT`.
- Timeout/retry có giới hạn; POC ghi rõ retry count, timeout và fail-safe quyết định. Không retry vô hạn.

### 2.3 Heartbeat, telemetry và staleness contract

- `RF_PROTOCOL.md` phải chốt heartbeat interval, stale threshold, telemetry rate khi ON/OFF/fault, payload event-driven và periodic, cùng clock/timestamp semantics. Giá trị phải được đưa vào test plan trước bench.
- Gateway phải chuyển node thành `STALE`/`RF_TIMEOUT` trong stale threshold đã chốt; UI/telemetry không được trình bày stale như healthy/normal.
- Sau node reboot hoặc reconnect, gateway phải fresh-state sync theo boot session. Node **không tự resume ON** nếu chưa nhận command authenticated, correlated và lease hợp lệ.

---

## 3. Tasks thực thi

### TRACK A — Hardware discovery và decision record

| Task ID | Công việc | Output / Done khi |
|---|---|---|
| **A1** | Lập inventory candidate RF, node MCU, relay/driver, nguồn, anten và flow sensor. | `docs/RF_FLOW_POC_DECISION.md` theo ADR có part number, datasheet URL/path, availability, RF regulation/duty-cycle, interface, rủi ro và quyết định/reject criteria. Bắt buộc ghi điện áp/dòng nominal, pump inrush/stall, driver rating margin, nguồn/anten/RF configuration candidate. |
| **A2** | Vẽ wiring diagram POC và electrical/water/EMI safety checklist. | Có UART TX/RX, GND/topology hoặc isolation, level shifting, nguồn pump/RF, GPIO feedback/pulse flow và boot-safe GPIO. Bắt buộc fuse, TVS, reverse-polarity, connector rating, emergency isolation; DC có flyback, AC có snubber/MOV/contactor strategy. Có brownout và pump-switching EMI/RF reset test; decoupling/filter rail RF, anten tránh dây pump. |
| **A3** | Chọn cơ chế pump feedback. | Tách `driver_feedback`/`load_feedback`. Nếu chỉ driver feedback + flow confirmation, document rõ electrical failures không cover. Nếu current sensing/aux contact, chốt ngưỡng ON/OFF, debounce, open-load/stall/stuck-relay coverage, ADC reference/filtering/isolation và bench evidence. |
| **A4** | Chuẩn bị flow bench và calibration procedure versioned. | Có nguồn nước, đường ống, bình đo thể tích chuẩn, chống rò điện/nước, bảo vệ chạy khô/van ngắt khẩn. Procedure chốt reference volume tối thiểu, dải multi-point, loại nước/áp lực/cột áp/van-nozzle/nguồn bơm/nhiệt độ, ≥3 trial, formula, variance/repeatability và reject criteria. |

### TRACK B — RF transport POC

| Task ID | Công việc | Output / Done khi |
|---|---|---|
| **B1** | Viết/version `docs/RF_PROTOCOL.md`, sau đó khai báo `IRfTransport`, `RfFrameCodec` và constants tách hardware adapter. | Spec wire-level đầy đủ, security posture và test vectors được review trước code. Unit test host encode/decode/CRC/MAC/length/version/node-id/duplicate/sequence wrap/reboot correlation; fuzz malformed frame, parser reject fail-closed. |
| **B2** | Implement UART adapter candidate, heartbeat/telemetry/staleness contract; tách khỏi debug Serial. | Gateway/node `PING/PONG` ổn định, non-blocking bounded I/O, TX/RX/CRC/drop counters và log rate-limit. Có heartbeat, ON/OFF/fault telemetry rate, stale threshold; test cắt nguồn node → `STALE/RF_TIMEOUT`, reboot session change và safe reconnect. |
| **B3** | Implement command manager và node-side lease/deadman. | `SET_PUMP` có authenticated `command_id`, session/sequence, ACK/NACK/timeout, bounded retry/idempotency và lease. Node boot OFF; gateway-loss lúc ON phải force OFF trong deadline, evidence `LEASE_EXPIRED_SAFE_OFF`; reject replay/unauthenticated command. |
| **B4** | Đo RF tại vị trí triển khai theo test plan đã chốt. | Bảng ON/OFF latency/loss/retry/timeout theo distance, worst obstacle/wet foliage, power-cycle và pump-switching EMI. Ghi frequency/channel/air data rate/TX power/anten type-gain-position, sample size, p50/p95/p99 và breakdown UART/airtime/node/ACK/flow; PASS/FAIL candidate theo threshold phê duyệt trước bench. |
| **B5** | Định nghĩa và kiểm thử temporary override/schedule resume trên MEGA8. | `SET_PUMP(OFF)` chỉ override tạm thời; MEGA8 giữ schedule và tự resume tại boundary/điểm resume đã chốt. Test reboot, mất RF, lệnh trùng, OFF giữa chu kỳ và ON override; chứng minh ESP32 không trở thành scheduler định kỳ và không xoá schedule node. |

### TRACK C — Pump feedback và flow measurement POC

| Task ID | Công việc | Output / Done khi |
|---|---|---|
| **C1** | Implement node actuator + feedback state. | Telemetry tách `desired`, `reported`, `driver_feedback`, `load_feedback`, `pump_feedback`, node/gateway timestamp, boot session và command correlation. Không suy reported/feedback từ desired; boot/fault OFF; không gọi GPIO trực tiếp từ MQTT callback. |
| **C2** | Implement pulse counter flow bằng ISR/counter. | ISR chỉ atomic increment/hardware counter; không I/O/allocation/log/blocking. Snapshot atomic tính `flow_lpm`, `delivered_volume_l`, `pulse_count`, `sample_window_ms`; host tests cover formula/boundary, reset/overflow, bounce/noise, abnormal period, zero/stale/disconnect và >6 L/min. |
| **C3** | Hiệu chuẩn sensor theo procedure. | Tối thiểu 3 trial tại nhiều điểm dải vận hành. Lưu raw data, reference volume, điều kiện, `pulses_per_litre = pulse_count / reference_volume_l`, sai số/mean/variance/repeatability. Calibration version áp dụng theo sensor serial + node ID; reject theo threshold định lượng đã chốt, không overwrite active calibration không audit/version. |
| **C4** | Implement flow/fault evaluation. | `min_flow_lpm`, `max_off_flow_lpm`, `max_flow_lpm`, `flow_start_timeout_s` configurable per node/treatment và được chốt trước test. Test `FLOW_CONFIRMED`, `NO_FLOW_FAULT`, `UNEXPECTED_FLOW_FAULT`, invalid/stale sensor, over-range; fault latch/audit + safe-off, không tự clear vì telemetry chập chờn. |
| **C5** | Xác định dữ liệu parse và metric analytics ở gateway/backend. | Chỉ persist normalized records, không raw RF frame. Có schema/event contract cho ACK outcome, relay desired/reported, feedback, flow, volume, session/sequence, retry/timeout, stale và schedule-vs-override; tính command-to-ACK, flow-start latency, confirmation rate, runtime, volume/run và mismatch. |

### TRACK D — Evidence, QA và decision gate

| Task ID | Công việc | Output / Done khi |
|---|---|---|
| **D1** | Viết/version `docs/RF_FLOW_POC_TEST_PLAN.md`, traceable test matrix và chạy QA POC. | Trước bench, plan chốt threshold, sample size, conditions, expected/actual/result. Evidence có timestamp, firmware/wiring revision, source commit và raw path; cover malformed/auth/replay/duplicate, lease gateway-loss, ACK/NACK/timeout, flow faults, stale/recovery, brownout/EMI. |
| **D2** | Viết/review FMEA và fail-safe/recovery policy. | FMEA có detection, node action, gateway action, retry/escalation, latch/reset/recovery cho RF timeout, gateway/node reset, no/unexpected flow, sensor disconnect/stale, feedback mismatch, RTC invalid. Phải chốt node-only OFF hay all-group stop và owner; cấm trạng thái RUNNING giả khi node fault/stale. |
| **D3** | Ra quyết định BOM/protocol/Go-No-Go. | `RF_FLOW_POC_DECISION.md` link `RF_PROTOCOL.md`, test plan, FMEA, calibration và raw evidence; chốt/đề xuất BOM/anten/mode/baud/pinout, lease, heartbeat/stale, security/risk acceptance, electrical-water-EMI safety. Phê duyệt hoặc reject với remediation/sign-off độc lập. |

---

## 4. QA POC — Cổng nghiệm thu bắt buộc

| Rule ID | PASS khi | Severity |
|---|---|---|
| **S1.5-RF-01** | Frame parser reject CRC sai, length sai, version sai; duplicate `sequence` không làm pump switch lần hai. | 🔴 BLOCKER |
| **S1.5-RF-02** | Command ON/OFF có ACK/NACK/timeout bounded retry và logged `command_id`/sequence/result. | 🔴 BLOCKER |
| **S1.5-RF-03** | Lệnh ON đạt chuỗi `RF_ACKED → PUMP_FEEDBACK_ON → FLOW_CONFIRMED`; ACK riêng lẻ không được hiển thị là tưới thành công. | 🔴 BLOCKER |
| **S1.5-SAFE-04** | `SET_PUMP(ON)` có lease; node boot OFF và force OFF khi gateway/RF biến mất trước lease deadline, có raw evidence + reason `LEASE_EXPIRED_SAFE_OFF`. | 🔴 BLOCKER |
| **S1.5-PROTO-05** | `RF_PROTOCOL.md` chốt wire contract, numeric payload/enums, correlation/reboot/sequence semantics, CRC vectors, bounded parser và MAC/HMAC anti-replay; nếu miễn MAC phải có ADR risk acceptance được ký. | 🔴 BLOCKER |
| **S1.5-FLOW-04** | Flow calibration có bằng chứng; `flow_lpm` và `delivered_volume_l` sai số nằm trong ngưỡng đã chốt ở POC. | 🔴 BLOCKER |
| **S1.5-FLOW-05** | Mô phỏng/đo thực tế no-flow sau ON tạo `NO_FLOW_FAULT`; OFF còn flow tạo `UNEXPECTED_FLOW_FAULT`. | 🔴 BLOCKER |
| **S1.5-SAFE-06** | Mất nguồn node/gateway, RF timeout hoặc sensor fault không để command bị lặp vô hạn; actuator đi/giữ trạng thái fail-safe OFF. | 🔴 BLOCKER |
| **S1.5-OPS-07** | Heartbeat/telemetry/stale/recovery contract PASS; node reboot/online lại không tự resume ON nếu thiếu command/lease hợp lệ. | 🔴 BLOCKER |
| **S1.5-HW-08** | Electrical/water/EMI safety checklist PASS: protection/rating/isolation, brownout và pump-switching RF test có raw evidence. | 🔴 BLOCKER |
| **S1.5-RF-07** | Field test ghi latency/loss ở vị trí thực tế và candidate RF được chấp thuận bằng decision record. | 🟠 CRITICAL |
| **S1.5-SCHED-09** | MEGA8 vẫn tự chạy schedule; OFF override hết hạn/đạt resume boundary thì node quay lại schedule đúng một lần, không bị ESP32 điều khiển định kỳ. | 🔴 BLOCKER |
| **S1.5-SCOPE-10** | Kiểm thử 4 node dùng chung một RF channel có polling/time-slot/collision policy và chứng minh không suy giảm ACK/telemetry ngoài threshold. | 🔴 BLOCKER |
| **S1.5-QUALITY-08** | `pio test -e native` và `pio run -e esp32-s3-devkitc-1` PASS; không commit credentials/BOM secret. | 🔴 BLOCKER |

### Kết luận QA POC

- **PASS:** D1–D3 hoàn tất; toàn bộ blocker PASS; BOM + pinout + baud/mode RF + calibration procedure + FMEA + test plan + security posture/risk acceptance được chốt. Khi đó mới mở Sprint 2 Production.
- **CONDITIONAL PASS:** Chỉ được phép khi lỗi không ảnh hưởng RF command, pump feedback, flow confirmation hoặc fail-safe; phải có owner/deadline khắc phục.
- **FAIL:** Bất kỳ blocker fail nào. Không được bắt đầu benchmark đa node hay phát triển Backend/UI production dựa trên candidate đó.

---

## 5. Deliverables

- `docs/RF_FLOW_POC_DECISION.md`
- `docs/RF_FLOW_POC_WIRING.md` hoặc schematic được versioned
- `docs/RF_PROTOCOL.md`
- `docs/RF_FLOW_POC_TEST_PLAN.md`
- FMEA/fail-safe/recovery policy được versioned
- Source/test POC, traceable test matrix và raw measurement evidence
- Quy trình calibration flow sensor theo sensor serial/node/version
- Cập nhật `PROGRESS.md` với kết quả QA POC và link evidence

*Sprint 1.5 Planning — tạo ngày 2026-08-10, đồng bộ acceptance contract ngày 2026-08-22 theo baseline ESP32-S3 gateway + 4 MEGA8 node.*
