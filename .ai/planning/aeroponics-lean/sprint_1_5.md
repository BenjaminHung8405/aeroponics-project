# Sprint 1.5: RF + Flow Proof of Concept & Hardware Decision Gate

> **Phụ thuộc:** Sprint 1 chỉ được dùng làm prototype cho boot-safe, RTC, NVS và watchdog. Không sử dụng abstraction 4 relay GPIO như kiến trúc production.  
> **Mục tiêu:** Giảm rủi ro phần cứng trước khi viết firmware production: chọn được phương án RF 433 MHz, chứng minh end-to-end command/ACK/feedback/lưu lượng với 1 gateway + 1 node, và ban hành quyết định BOM/pinout/protocol.  
> **Không phải output:** Đây chưa phải thử nghiệm 12 node hoàn chỉnh và chưa phải firmware production. Không được dùng POC PASS để bỏ qua benchmark 12 node ở Sprint 2.

> **Tài liệu ưu tiên:** [PROJECT_ALIGNMENT_2026-08-10.md](./PROJECT_ALIGNMENT_2026-08-10.md).

---

## 1. Phạm vi POC

### 1.1 Topology tối thiểu

```text
ESP32-S3 gateway ── UART ── RF 433 MHz ── UART ── remote node
                                                   ├─ relay/driver pump
                                                   ├─ pump feedback input
                                                   └─ flow sensor ≤ 6 L/min (pulse output)
```

| Thành phần | Yêu cầu POC |
|---|---|
| Gateway | ESP32-S3 hiện có, USB Serial chỉ dành cho debug; UART RF riêng không dùng chung cổng log. |
| RF candidates | So sánh tối thiểu 1 phương án LoRa UART 433 MHz transparent. Ebyte E32/E220 433 MHz chỉ là candidate, chưa phải BOM đã phê duyệt. |
| Node | MCU phù hợp, mạch nguồn, relay/driver bơm và điểm feedback có thể đo được. |
| Flow sensor | Loại pulse output, dải đo tối đa 6 L/min; có thể hiệu chuẩn `pulses_per_litre` từng sensor. |
| Pump feedback | Ít nhất phản hồi mức output driver/relay. Khuyến nghị đo dòng tải hoặc tín hiệu auxiliary contact để phân biệt “đã bật relay” và lỗi tải bơm. |

### 1.2 Tiêu chí quyết định RF

- Tầm phủ phải được đo tại vị trí gần nhất/xa nhất, qua vật cản thực tế của khu trồng.
- Báo cáo packet loss, round-trip command ACK latency p50/p95/p99, reconnect sau power-cycle và chất lượng link nếu module hỗ trợ.
- Phương án được chọn phải có datasheet rõ ràng, nguồn hàng ổn định, anten phù hợp, nguồn cấp phù hợp và tuân thủ quy định RF/công suất tại nơi lắp đặt.
- Không chốt baud rate, UART pinout, công suất phát hay mode RF trước khi POC/datasheet xác nhận.

---

## 2. Hợp đồng giao thức POC

### 2.1 Application frame (bắt buộc dù module dùng transparent UART)

```text
SOF(2) | protocol_version(1) | message_type(1) | node_id(1) |
sequence(2) | payload_length(1) | payload(0..N) | CRC-16(2)
```

- Parser là state machine bounded; reject frame sai `SOF`, length, version, `node_id` hoặc CRC mà không cấp phát động.
- `sequence` đủ để nhận biết duplicate command. Node phải trả lại kết quả đã xử lý cho duplicate, không kích pump lần hai.
- Các message tối thiểu: `PING`, `PONG`, `SET_PUMP`, `COMMAND_ACK`, `TELEMETRY`, `FAULT_REPORT`.
- `SET_PUMP` gồm desired state và `command_id`; `COMMAND_ACK` báo accepted/rejected nhưng không được thay thế pump/flow feedback.

### 2.2 State machine xác nhận tưới

```text
COMMAND_SENT → RF_ACKED → PUMP_FEEDBACK_ON → FLOW_CONFIRMED
                    └──→ NACK / TIMEOUT / PUMP_FEEDBACK_FAULT / NO_FLOW_FAULT
```

- Lệnh ON chỉ được coi là **tưới đã xác nhận** ở `FLOW_CONFIRMED`.
- Nếu đã ACK nhưng không đạt `min_flow_lpm` trong `flow_start_timeout_s`: `NO_FLOW_FAULT`.
- Nếu OFF mà lưu lượng vượt `max_off_flow_lpm`: `UNEXPECTED_FLOW_FAULT`.
- Timeout/retry có giới hạn; POC ghi rõ retry count, timeout và fail-safe quyết định. Không retry vô hạn.

---

## 3. Tasks thực thi

### TRACK A — Hardware discovery và decision record

| Task ID | Công việc | Output / Done khi |
|---|---|---|
| **A1** | Lập inventory candidate RF, node MCU, relay/driver, nguồn, anten và flow sensor. | `docs/RF_FLOW_POC_DECISION.md` có part number, datasheet URL/path, điện áp, dòng, interface, rủi ro. |
| **A2** | Vẽ wiring diagram POC. | Có UART TX/RX, GND chung, level shifting nếu cần, nguồn pump/RF, GPIO feedback và GPIO pulse flow; GPIO bootstrap ESP32 không bị dùng rủi ro. |
| **A3** | Chọn cơ chế pump feedback. | Chứng minh được output feedback; ghi rõ có/không current sensing và giới hạn phát hiện pump hỏng. |
| **A4** | Chuẩn bị flow bench. | Có nguồn nước, đường ống, bình đo thể tích chuẩn và quy trình hiệu chuẩn an toàn. |

### TRACK B — RF transport POC

| Task ID | Công việc | Output / Done khi |
|---|---|---|
| **B1** | Khai báo `IRfTransport`, `RfFrameCodec` và constants cấu hình tách hardware adapter. | Unit test host cho encode/decode/CRC/length/version/node-id/duplicate sequence. |
| **B2** | Implement UART adapter của module candidate, tách khỏi debug Serial. | Gateway/node trao đổi `PING/PONG` ổn định và có log thống kê TX/RX/error. |
| **B3** | Implement command manager. | `SET_PUMP` có sequence, ACK/NACK, timeout, bounded retry, idempotency. |
| **B4** | Đo RF tại vị trí triển khai. | Bảng latency/loss theo khoảng cách và vật cản; có kết luận pass/fail candidate. |

### TRACK C — Pump feedback và flow measurement POC

| Task ID | Công việc | Output / Done khi |
|---|---|---|
| **C1** | Implement node actuator + feedback state. | `desired`, `driver`, `pump_feedback` được báo telemetry và thay đổi đúng trong test ON/OFF. |
| **C2** | Implement pulse counter flow bằng ISR/counter. | Không làm I/O nặng trong ISR; đọc atomic; có test conversion trên host. |
| **C3** | Hiệu chuẩn sensor. | Lặp tối thiểu 3 lần/mỗi node prototype, tính `pulses_per_litre`, sai số và lưu calibration version. |
| **C4** | Implement flow/fault evaluation. | Test thành công `FLOW_CONFIRMED`, `NO_FLOW_FAULT`, `UNEXPECTED_FLOW_FAULT`, invalid sensor input và over-range >6 L/min. |

### TRACK D — Evidence, QA và decision gate

| Task ID | Công việc | Output / Done khi |
|---|---|---|
| **D1** | Viết test matrix và chạy QA POC. | Evidence có timestamp, firmware revision, wiring revision, conditions, expected/actual/result. |
| **D2** | Review fail-safe. | Power loss gateway/node, RF timeout, RTC invalid và sensor fault đều có trạng thái an toàn/lý do documented. |
| **D3** | Ra quyết định BOM/protocol. | `RF_FLOW_POC_DECISION.md` được phê duyệt hoặc POC bị reject với remediation rõ ràng. |

---

## 4. QA POC — Cổng nghiệm thu bắt buộc

| Rule ID | PASS khi | Severity |
|---|---|---|
| **S1.5-RF-01** | Frame parser reject CRC sai, length sai, version sai; duplicate `sequence` không làm pump switch lần hai. | 🔴 BLOCKER |
| **S1.5-RF-02** | Command ON/OFF có ACK/NACK/timeout bounded retry và logged `command_id`/sequence/result. | 🔴 BLOCKER |
| **S1.5-RF-03** | Lệnh ON đạt chuỗi `RF_ACKED → PUMP_FEEDBACK_ON → FLOW_CONFIRMED`; ACK riêng lẻ không được hiển thị là tưới thành công. | 🔴 BLOCKER |
| **S1.5-FLOW-04** | Flow calibration có bằng chứng; `flow_lpm` và `delivered_volume_l` sai số nằm trong ngưỡng đã chốt ở POC. | 🔴 BLOCKER |
| **S1.5-FLOW-05** | Mô phỏng/đo thực tế no-flow sau ON tạo `NO_FLOW_FAULT`; OFF còn flow tạo `UNEXPECTED_FLOW_FAULT`. | 🔴 BLOCKER |
| **S1.5-SAFE-06** | Mất nguồn node/gateway, RF timeout hoặc sensor fault không để command bị lặp vô hạn; actuator đi/giữ trạng thái fail-safe OFF. | 🔴 BLOCKER |
| **S1.5-RF-07** | Field test ghi latency/loss ở vị trí thực tế và candidate RF được chấp thuận bằng decision record. | 🟠 CRITICAL |
| **S1.5-QUALITY-08** | `pio test -e native` và `pio run -e esp32-s3-devkitc-1` PASS; không commit credentials/BOM secret. | 🔴 BLOCKER |

### Kết luận QA POC

- **PASS:** D1–D3 hoàn tất, toàn bộ blocker PASS, BOM + pinout + baud/mode RF + calibration procedure được chốt. Khi đó mới mở Sprint 2 Production.
- **CONDITIONAL PASS:** Chỉ được phép khi lỗi không ảnh hưởng RF command, pump feedback, flow confirmation hoặc fail-safe; phải có owner/deadline khắc phục.
- **FAIL:** Bất kỳ blocker fail nào. Không được bắt đầu benchmark 12 node hay phát triển Backend/UI production dựa trên candidate đó.

---

## 5. Deliverables

- `docs/RF_FLOW_POC_DECISION.md`
- `docs/RF_FLOW_POC_WIRING.md` hoặc schematic được versioned
- `docs/RF_PROTOCOL.md`
- Source/test POC, test matrix và raw measurement evidence
- Quy trình calibration flow sensor theo node
- Cập nhật `PROGRESS.md` với kết quả QA POC và link evidence

*Sprint 1.5 Planning — tạo ngày 2026-08-10.*
