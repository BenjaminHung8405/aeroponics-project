# Aeroponics Lean — Progress Tracker

> **Tracker hiện hành:** Cập nhật ngày **2026-08-22** theo xác nhận phần cứng/vận hành mới.
> - **Không triển khai tiếp đường 4 relay GPIO trực tiếp:** `RelayController`, `ScheduleManager`, MQTT relay command/topic, `relay_profiles`, `relay_events` và `sensor_readings` chỉ là **prototype rig / compatibility debt**, không phải production baseline.
> - **Sprint 1.5 đang ở pha Remediation S0–S1 bắt buộc:** thay thế hoặc cô lập code cũ trước khi viết POC RF + Flow. Không được xoá nền tảng còn tái sử dụng (boot-safe, NVS abstraction, RTC, WDT, FreeRTOS, Docker 3-service) khi chưa có successor và regression test.
> - **Sprint 2, 3, 4:** chỉ mở theo các gate đã nêu, với kiến trúc **ESP32-S3 RF Gateway + 4 MEGA8 Node + Season/Group/Flow Domain + Tuya On-demand**. Việc mở rộng quá 4 node là backlog, không thuộc scope hiện tại.

---

## 📌 Khái quát trạng thái dự án (Sprint Roadmap Summary)

| Sprint | Nội dung chính | Trạng thái | Tài liệu tham chiếu |
|---|---|---|---|
| **Sprint 0** | Hạ tầng Docker (TimescaleDB + Mosquitto + Backend) & schema | 🟠 Cần remediation: schema/health-check/ACL/env còn legacy relay và Tuya poll liên tục | [`sprint_0.md`](./sprint_0.md) |
| **Sprint 1** | Firmware Foundation (boot-safe, NVS, RTC, WDT, FreeRTOS) | 🟠 Nền tảng tái sử dụng được; runtime 4 relay trực tiếp phải được thay thế | [`sprint_1.md`](./sprint_1.md) |
| **Sprint 1.5** | Remediation S0–S1 → RF 433 MHz + Flow POC & Hardware Decision Gate | 🚧 **ĐANG THỰC HIỆN** — 1 ESP32-S3 gateway + 1 MEGA8 node, mở rộng thử nghiệm tới 4 node | [`sprint_1_5.md`](./sprint_1_5.md) |
| **Sprint 2** | Firmware Production RF Gateway & 4 MEGA8 Node Control | 🔵 Chờ Sprint 1.5 PASS | [`sprint_2.md`](./sprint_2.md) |
| **Sprint 3** | NestJS Backend (Season + Group + Node + Flow + On-demand) | 🔵 Chờ Sprint 2 PASS (Kế hoạch đã align 100%) | [`sprint_3.md`](./sprint_3.md) |
| **Sprint 4** | Single-file HTML Dashboard UI | 🔵 Chờ Sprint 3 PASS (Kế hoạch đã align 100%) | [`sprint_4.md`](./sprint_4.md) |

---

## 📌 Thông tin vận hành Sprint 1.5

| Field | Value |
|---|---|
| **Thời gian cập nhật tracker** | 2026-08-22 (sau xác nhận phần cứng và vận hành) |
| **Sprint hiện hành** | Sprint 1.5 — Remediation S0–S1, sau đó RF + MEGA8 protocol/flow POC & Hardware Decision Gate |
| **Agent thực thi (Execution Agent)** | Antigravity / Gemini |
| **Senior Solution Architect** | QA độc lập kiểm định trước khi chuyển bất kỳ task nào sang `[x] Done` |

---

## 🎯 Reference Plan & Document Hierarchy

- **Thư mục kế hoạch:** `.ai/planning/aeroponics-lean/`
- **Tài liệu kiến trúc ưu tiên cao nhất:** [`PROJECT_ALIGNMENT_2026-08-10.md`](./PROJECT_ALIGNMENT_2026-08-10.md)
- **Contract POC / Go-No-Go bắt buộc:** [`sprint_1_5.md`](./sprint_1_5.md)
- **Kế hoạch Backend Production:** [`sprint_3.md`](./sprint_3.md)
- **Kế hoạch Dashboard UI:** [`sprint_4.md`](./sprint_4.md)
- **Ngữ cảnh tổng quan:** [`README.md`](./README.md)

> **Quy tắc ưu tiên khi mâu thuẫn:** `PROJECT_ALIGNMENT_2026-08-10.md` → `sprint_1_5.md` → `sprint_2.md` → `sprint_3.md` / `sprint_4.md` → tài liệu Sprint 0–1 lịch sử. Code hiện hữu không tự trở thành SSOT.

---

## ✅ Architecture Baseline — xác nhận ngày 2026-08-22

Đây là yêu cầu người dùng đã xác nhận và có ưu tiên cao hơn các giả định cũ trong plan:

| Hạng mục | Baseline bắt buộc |
|---|---|
| Gateway | 01 ESP32-S3 làm gateway chủ; 01 RF transceiver 433 MHz nối với ESP32 qua UART riêng, tách USB/debug UART. |
| Remote nodes | Trước mắt 04 node, mỗi node là 01 ATmega8/MEGA8 có RF transceiver 433 MHz riêng; node ID `1..4`. |
| MEGA8 firmware | Có thể sửa firmware. MEGA8 tiếp tục tự chạy schedule/timer và điều khiển relay/pump độc lập. |
| ESP32 responsibility | Chỉ gửi lệnh ON/OFF thủ công hoặc override tạm thời, nhận telemetry đã parse, đánh giá ACK/feedback/flow và lưu/publish dữ liệu. ESP32 không trở thành scheduler định kỳ thay MEGA8. |
| OFF override | Lệnh OFF chỉ có hiệu lực tạm thời theo semantics được chốt ở protocol; sau chu kỳ/điểm resume của schedule, MEGA8 tự quay lại lịch. Không được dùng cơ chế này để vô tình xoá lịch trên node. |
| RF module | Chưa xác định model; phải discovery và decision gate trước khi khóa baud, pinout, mode, addressing, half-duplex/collision policy. |
| Existing protocol | Chưa có protocol hiện hữu; phải thiết kế/version protocol cho MEGA8 trước implementation. `COMMAND_ACK` production tuân thủ `RF_PROTOCOL.md`: HMAC + CRC + boot session + sequence + command ID; không echo payload. |
| Storage | Chỉ lưu dữ liệu đã parse/normalized; không lưu raw RF frame trong production database. Lỗi transport được lưu dưới dạng counters/events đã parse (CRC/MAC fail, timeout, retry, duplicate). |

### Data/analytics baseline

Tối thiểu lưu theo node: `desired_state`, `reported_state`, `driver_feedback`, `load_feedback` nếu có, `flow_lpm`, `pulse_count`, `delivered_volume`, `fault_flags`, `command_id`, ACK outcome, sequence/session, timestamps node/gateway, retry/timeout, heartbeat/stale và schedule/override source. Các chỉ số phân tích được bổ sung vào plan: command-to-ACK latency, flow-start latency, tỷ lệ `FLOW_CONFIRMED`, no-flow/unexpected-flow, runtime thực tế, delivered volume theo chu kỳ, flow stability, packet loss/retry, stale duration và schedule-vs-override mismatch.

## 📝 Addition Plan (Yêu cầu kỹ thuật bắt buộc cho Sprint 1.5 POC)

Các yêu cầu phát sinh dưới đây là **BLOCKER** cho Go/No-Go của Sprint 1.5; POC bắt đầu với 1 gateway + 1 node và phải có bài kiểm tra mở rộng tới 4 node trước khi đóng gate:

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
| `[!] Re-validate` | Đã từng PASS theo baseline cũ, evidence được giữ để kiểm toán, nhưng **không còn là bằng chứng đủ** cho baseline 2026-08-22. Phải re-scope/re-test theo task remediation tương ứng trước khi trở lại `[x] Done`. |

---

# 🚧 Track R — Remediation Sprint 0–1 (bắt buộc trước POC RF)

> **Mục tiêu:** Loại bỏ đường chạy production 4-relay cũ và đồng bộ hạ tầng với contract **4 MEGA8 node**, nhưng giữ các primitive nền tảng đã kiểm chứng. Các tham chiếu 12 node bên dưới là lịch sử/khả năng mở rộng, không phải acceptance scope hiện tại. Track này là công việc triển khai tiếp theo; không được đánh dấu Sprint 0 hoặc Sprint 1 là production-complete chỉ vì code prototype còn build được.

## Phân loại mã hiện hữu

| Nhóm | Quyết định | Thành phần đã xác minh |
|---|---|---|
| **Giữ và tái dùng** | Giữ abstraction/test phù hợp sau khi đổi consumer; chỉ refactor tối thiểu khi successor yêu cầu. | `NvsStorage`/NVS backend, `RtcManager`, `ESPTaskWatchdog`, `FreeRTOSTaskRunner`, `IClock`, `IWatchdog`, Docker 3-service, backend `/health`. |
| **Thay thế bắt buộc** | Viết successor theo RF Gateway/Node; chỉ xoá runtime path cũ sau khi successor build/test PASS. | `RelayController`, `IRelayOutput`, `ScheduleManager`, `RelayProfile`/profile repository relay, MQTT relay command handler, Serial override relay, task-per-relay, relay topic ACL. |
| **Loại khỏi production contract** | Không tạo mới dependency; migration/archive có kiểm soát nếu DB đã khởi tạo. | `relay_profiles`, `relay_events`, `sensor_readings`, kiểm tra health-check 5 bảng/2 hypertable cũ, `TUYA_POLL_INTERVAL_MS=10000`. |

## Kế hoạch thực thi theo thứ tự

| Task ID | Công việc | Status | Done khi |
|---|---|---|---|
| **R1** | Lập inventory dependency và migration plan cho toàn bộ runtime 4 relay; version `docs/SPRINT_0_1_LEGACY_REPLACEMENT.md`. | [x] Done | Map source/test/topic/schema/script cũ → successor Sprint 1.5/2/3; xác định thứ tự remove, rollback và acceptance test. Không xoá code chỉ vì không dùng. |
| **R2** | Tách firmware composition root: boot-safe output phải ở **node actuator**, gateway không khởi tạo relay GPIO hay 4 relay task. | [x] Done | `main.cpp` gateway không include/construct `RelayController`/`ScheduleManager`; có `IRfTransport` seam và RF UART tách USB debug. Primitive NVS/RTC/WDT/FreeRTOS vẫn build/test. |
| **R3** | Thay relay scheduler/profile/override bằng contract gateway–MEGA8 node; tách ownership schedule khỏi ESP32. | [!] Re-validate | Evidence group–node cũ được giữ, nhưng không đủ cho baseline mới. Re-validate phải chứng minh ESP32 không chạy scheduler định kỳ; MEGA8 node `1..4` giữ schedule/timer; gateway chỉ phát temporary ON/OFF override; OFF override không xoá schedule và MEGA8 resume đúng semantics. `docs/RF_PROTOCOL.md` phải được kiểm tra lại với khả năng thực thi trên MEGA8. |
| **R4** | Đồng bộ MQTT/Mosquitto/config từ relay domain sang gateway/group/node domain. | [x] Done | ACL và firmware/backend topic contract theo Sprint 2; command có `command_id`, version và ACK outcome RF; không direct GPIO từ MQTT callback. Wi-Fi/MQTT/RF keys không tracked. |
| **R5** | Hoàn tất migration schema và operational scripts cho production domain. | [x] Done | Schema/migration có `seasons`, treatment/version, timer group, node/group assignment lịch sử, command/state/feedback/flow/calibration và Tuya measurement session; health-check kiểm tra đúng contract mới. Không dùng `node_ids` array hoặc `node_registry.group_id` làm source of truth mapping lịch sử; Tuya chỉ on-demand/end-of-season, không default poll 10 giây. |
| **R6** | Gỡ legacy runtime và regression verification sau khi successor PASS. | [x] Done | `platformio.ini` không compile source relay cũ vào gateway production; test cũ được thay/di chuyển thành test primitive hoặc prototype-only rõ ràng; `rg` không còn legacy relay trong production paths, migration được rehearsal trên DB disposable. |
| **R3-M** | Re-validate R3 theo baseline 4 MEGA8. | [ ] QA Review | Cập nhật composition/ownership documentation và test: node schedule là source of truth; gateway không tạo timer fan-out định kỳ; temporary override có expiry/resume; node reboot/RF loss không tự resume ON; node ID chỉ `1..4`; evidence map tới `WALKTHROUGH_LOG.md`. |
| **R4-M** | Re-validate MQTT/command contract cho temporary override và normalized telemetry. | [ ] QA Review | MQTT/API chỉ tạo command DTO bounded; command có `command_id`, source `MANUAL_OVERRIDE`/`FAIL_SAFE`, expiry/resume policy; callback không điều khiển GPIO; chỉ persist dữ liệu đã parse, không raw RF frame; topic/schema không mô tả scheduler ESP32 như actuator owner. |
| **R5-M** | Re-validate schema/health-check theo scope 4 node và ownership MEGA8. | [ ] QA Review | Registry/telemetry/flow/command/feedback schema hỗ trợ node `1..4`, `schedule_state`, `override_state`, `resume_reason`, timestamps node/gateway và analytics fields; không để production health-check phụ thuộc relay legacy hoặc 12-node acceptance. Migration rehearsal + regression evidence PASS. |
| **R6-M** | Xác nhận build/runtime không còn đường direct relay hoặc ESP32 scheduler trong production. | [ ] QA Review | `rg`/build/test chứng minh gateway không include/construct `RelayController`, `ScheduleManager`, relay GPIO hoặc periodic schedule fan-out; test composition root và native regression PASS. Không xoá prototype code nếu chưa có archive/rollback evidence. |

### Gate chuyển từ Track R sang Track A–D POC

- [x] `docs/SPRINT_0_1_LEGACY_REPLACEMENT.md` đã được review, nêu rõ file nào giữ/thay/xoá và rollback/migration plan.
- [x] `docs/RF_PROTOCOL.md` và `docs/RF_FLOW_POC_TEST_PLAN.md` tồn tại **trước** codec, UART adapter và actuator POC.
- [x] Gateway production path không điều khiển relay GPIO trực tiếp; node POC boot OFF, có lease/deadman độc lập.
- [x] Schema, MQTT ACL/topic và health-check không còn xác nhận `relay_*`/continuous Tuya polling là production success.
- [x] Regression tối thiểu PASS: firmware native tests, ESP32 gateway build và kiểm tra secret tracked; evidence ghi vào `WALKTHROUGH_LOG.md`.
- [ ] R3-M/R4-M/R5-M/R6-M PASS theo baseline 4 MEGA8; các gate `[x]` cũ không được dùng thay thế cho các re-validation này.

### Thứ tự thực thi được đề xuất (execution order)

1. **R3-M** — chốt ownership schedule ở MEGA8 và bỏ mọi periodic fan-out ở gateway.
2. **B1** — cập nhật/kiểm chứng `docs/RF_PROTOCOL.md` cho MEGA8, gồm HMAC feasibility và temporary override/resume semantics.
3. **A1 + A2** — RF module discovery, wiring và electrical/EMI safety trước khi mua số lượng.
4. **B6 + B2** — node adapter/telemetry và UART transport `PING/PONG` trên hardware thật.
5. **B3 + B5** — command manager, lease/deadman, ACK idempotency và temporary override/resume.
6. **C1 → C4** — actuator/feedback, pulse counter, calibration, flow/fault evaluation.
7. **C5 + R4-M + R5-M** — normalized telemetry contract, MQTT command contract và schema/health-check theo 4 node.
8. **B4 + S1.5-4NODE-10** — RF field test 1 node, sau đó 4 node chung channel.
9. **R6-M + D1 → D4** — regression, test plan/FMEA/decision record và QA gate cuối.

---

# 🚧 Sprint 1.5 — RF + Flow Proof of Concept & Hardware Decision Gate

> **Mục tiêu:** Chọn được phương án RF 433 MHz, thiết kế protocol mới cho MEGA8 và chứng minh end-to-end command/ACK/pump feedback/flow confirmation với 1 gateway + 1 node, sau đó kiểm thử shared RF với đủ 4 node. Sprint 2 Production chỉ được mở khi Sprint 1.5 PASS cùng toàn bộ re-validation Track R.

## TRACK A — Hardware Discovery & Decision Record

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật cấp cao |
|---|---|---|---|
| **A1** | Lập inventory candidate RF 433 MHz, node MCU, relay/driver bơm, nguồn, anten và flow sensor. Tạo `docs/RF_FLOW_POC_DECISION.md` với part number, datasheet URL/path, điện áp, dòng, interface và rủi ro. | [ ] QA Review | Dùng **ADR**: alternatives, trade-off, tiêu chí reject và owner phê duyệt. Candidate LoRa UART transparent chỉ là POC, không tự thành BOM. Bắt buộc kiểm tra availability, RF regulation/duty-cycle, UART logic, công suất phát, anten, pump nominal/inrush/stall, rating driver có margin và ngân sách nguồn/brownout. |
| **A2** | Vẽ wiring diagram POC cho gateway ↔ RF ↔ node, driver pump, pump feedback và flow sensor. | [ ] QA Review | Áp dụng **Hardware Interface Contract**. Nêu TX/RX, GND/topology hoặc isolation, level shifting, decoupling/RF rail, fuse, TVS, reverse-polarity, connector rating và emergency isolation. DC phải có flyback; AC phải có snubber/MOV/contactor strategy. Tách USB debug/UART RF, tránh GPIO strapping; test EMI/RF reset khi pump switching và bố trí anten tránh dây pump. |
| **A3** | Chọn và chứng minh cơ chế pump feedback tại node. | [ ] QA Review | Áp dụng **defence in depth**. Phân biệt bắt buộc `driver_feedback` với `load_feedback`; không gọi relay output là pump đang chạy. Nếu POC chỉ driver feedback + flow, ghi rõ failure modes điện không phát hiện được. Nếu current/aux sensing, chốt threshold, debounce, open-load/stall/stuck-relay coverage, ADC/filtering/isolation và test evidence. |
| **A4** | Chuẩn bị flow bench: nguồn nước, đường ống, bình đo thể tích chuẩn và quy trình hiệu chuẩn an toàn. | [ ] QA Review | Áp dụng **measurement traceability**. Version calibration procedure: reference volume tối thiểu, multi-point operating range, nước/áp lực/van-nozzle/pump supply/nhiệt độ, ≥3 trial, variance/repeatability/reject threshold. Có công thức `pulses_per_litre`, `flow_lpm`, `delivered_volume_l`; bảo vệ rò nước/điện, chống chạy khô và van ngắt khẩn. |

## TRACK B — RF Transport POC

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật cấp cao |
|---|---|---|---|
| **B1** | Viết/version `docs/RF_PROTOCOL.md` cho ESP32-S3 ↔ MEGA8; khai báo `IRfTransport`, `RfFrameCodec` và constants tách hardware adapter; unit test encode/decode/CRC/length/version/node-id/duplicate. | [ ] QA Review | Áp dụng **Ports and Adapters / Dependency Inversion**: codec C++ thuần, parser bounded/fail-closed, không allocation động. Trước code phải kiểm chứng HMAC-SHA256 feasibility trên ATmega8 về flash/RAM/CPU hoặc phê duyệt adapter thay thế. Spec phải chốt SOF, version, endian, CRC vectors, enums, schemas, MAX payload/RX buffer/timeout, sequence wrap, boot session, command ID và temporary override/resume semantics. MAC/HMAC + anti-replay bắt buộc; secret chỉ qua secure provisioning. Fuzz malformed frames. |
| **B2** | Implement UART adapter cho RF candidate; gateway và node trao đổi `PING/PONG` ổn định, tách khỏi debug Serial. | [ ] QA Review | Adapter chỉ implement `IRfTransport`; không trộn framing/protocol/business logic. Non-blocking I/O, bounded RX buffer, timeout, TX/RX/CRC/drop counters; không log ISR, rate-limit log. Chốt heartbeat interval, telemetry ON/OFF/fault và stale threshold; test node power-off → `STALE/RF_TIMEOUT`, node reboot tạo boot-session mới và không tự resume ON. |
| **B3** | Implement command manager POC cho `SET_PUMP`, `COMMAND_ACK`, timeout, bounded retry, idempotency và node-side lease. | [ ] QA Review | Áp dụng **Command pattern + safety FSM**. Mỗi command có `command_id`, boot-session/sequence và `run_lease_ms`; duplicate trả outcome cũ, không actuate hai lần. Queue/retry/backoff bounded, không busy-wait. Node boot OFF trước stack; lease expiry force OFF độc lập gateway, audit `LEASE_EXPIRED_SAFE_OFF`. Test gateway loss khi ON và replay/unauthenticated command rejection. |
| **B4** | Đo RF tại vị trí triển khai: latency/loss theo khoảng cách, vật cản, power-cycle reconnect và link quality nếu hỗ trợ. | [ ] QA Review | Chỉ chạy theo `docs/RF_FLOW_POC_TEST_PLAN.md` đã chốt trước bench: sample size và PASS/FAIL không được đổi hậu nghiệm. Ghi firmware/wiring/RF config, pump switching EMI, ON/OFF/retry/timeout, p50/p95/p99 và breakdown UART/airtime/node/ACK/flow. Sau POC 1 node phải kiểm thử 4 node chung channel với polling/time-slot/collision policy. Không suy luận ngoài scope 4 node hiện tại. |
| **B5** | Implement/test temporary override và schedule resume trên MEGA8. | [ ] QA Review | `SET_PUMP(OFF)` chỉ override tạm thời; MEGA8 giữ schedule và tự resume tại boundary/điểm resume đã chốt. Test OFF giữa chu kỳ, expiry, ON override, reboot, mất RF, duplicate command và chứng minh ESP32 không chạy schedule định kỳ. |
| **B6** | Implement MEGA8 RF node adapter/telemetry sender và gateway parser cho 4 node. | [ ] QA Review | Node ID `1..4`, UART/RF framing, ACK/telemetry/heartbeat, boot session/sequence, authenticated command và normalized telemetry hoạt động trên hardware thật; không dùng echo payload. |

## TRACK C — Pump Feedback & Flow Measurement POC

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật cấp cao |
|---|---|---|---|
| **C1** | Implement node actuator + telemetry `desired`, `reported`, `driver` và `pump_feedback`; kiểm tra ON/OFF thực tế. | [ ] QA Review | `SPEC-FEEDBACK-001` explicit-state model: commanded != driver feedback != load current != flow. Boot-safe default LOW, fault latch triggers hard safe-off. Optocoupler gate sense, ACS712 current sensing (>150mA), inrush blanking (80ms), stall protection (>3.8A/50ms), dual timestamps & command correlation. |
| **C2** | Implement pulse counter flow bằng ISR hoặc counter phần cứng và conversion L/min. | [ ] Pending | ISR chỉ tăng counter atomic/hardware counter; cấm I/O/allocation/log/blocking. Snapshot atomic tính `flow_lpm`, `delivered_volume_l`, `pulse_count`, `sample_window_ms`; xử lý counter reset/overflow, bounce/noise, pulse bất thường, zero/stale/disconnect và >6 L/min. Có host unit test conversion/calibration math và input boundary. |
| **C3** | Hiệu chuẩn flow sensor tối thiểu 3 lần trên node prototype; tính `pulses_per_litre`, sai số và lưu calibration version. | [ ] Pending | Áp dụng **calibration as versioned configuration** theo sensor serial + node ID + version, không hard-code hệ số chung. Lưu raw trials, reference volume, điều kiện, mean/variance/repeatability/sai số; đo nhiều điểm dải vận hành và reject theo threshold định lượng đã phê duyệt. Không overwrite calibration active nếu không có version/audit. |
| **C4** | Implement và test flow/fault evaluation: `FLOW_CONFIRMED`, `NO_FLOW_FAULT`, `UNEXPECTED_FLOW_FAULT`, invalid input và over-range. | [ ] Pending | Áp dụng **safety FSM**. ON chỉ success sau `RF_ACKED → PUMP_FEEDBACK_ON → FLOW_CONFIRMED`; ACK riêng lẻ không đủ. `min_flow_lpm`, `max_off_flow_lpm`, `max_flow_lpm`, `flow_start_timeout_s` configurable per node/treatment và chốt trước test. Fault latch/audit + safe-off; không tự clear vì telemetry chập chờn. |
| **C5** | Chốt normalized telemetry và analytics contract. | [ ] Pending | Chỉ lưu parsed fields: desired/reported, driver/load feedback, flow, pulses, volume, fault, command ID, ACK outcome, sequence/session, node/gateway timestamps, retry/timeout/stale và schedule-vs-override. Không persist raw RF frame. Có metric latency, confirmation rate, runtime, volume/run, stability, packet loss và mismatch. |

## TRACK D — Evidence, QA & Decision Gate

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật cấp cao |
|---|---|---|---|
| **D1** | Viết/version `docs/RF_FLOW_POC_TEST_PLAN.md`, test matrix và chạy QA POC; lưu evidence timestamp, firmware/wiring revision, điều kiện, expected/actual/result. | [ ] Pending | Áp dụng **traceable verification matrix**: threshold + sample size/PASS-FAIL phải được duyệt trước bench; map case tới `S1.5-*`, commit và raw evidence. Bao gồm malformed/auth/replay/duplicate, ACK/NACK/timeout, lease gateway-loss, ON/OFF, no/stuck flow, sensor invalid, RF loss/power-cycle, heartbeat/stale recovery, brownout và EMI pump switching. Screenshot/log đơn lẻ không đủ. |
| **D2** | Review fail-safe cho power loss gateway/node, RF timeout, RTC invalid, pump feedback mismatch và sensor fault. | [ ] Pending | Áp dụng **fail-safe by default + FMEA** versioned: RF timeout, gateway/node reboot, no/unexpected flow, sensor disconnect/stale, feedback mismatch, RTC invalid. Mỗi mode có detection, node action, gateway action, retry/escalation, latch/reset/recovery và owner. Phải chốt node-only OFF hay group-stop; không có `RUNNING` giả khi node fault/stale. |
| **D3** | Ra quyết định BOM/protocol qua `RF_FLOW_POC_DECISION.md`: phê duyệt hoặc reject candidate với remediation rõ ràng. | [ ] Pending | Chỉ QA Review khi toàn bộ blocker PASS và RF evidence đủ. Decision record phải link `RF_PROTOCOL.md`, test plan/FMEA/calibration, chốt/đề xuất BOM/anten/mode/baud/pinout, lease, heartbeat/stale, security posture/risk acceptance, electrical-water-EMI safety và open risks. Không mở Sprint 2 nếu thiếu sign-off độc lập. |
| **D4** | QA regression toàn bộ Track R re-validation + 4-node acceptance. | [ ] Pending | R3-M/R4-M/R5-M/R6-M PASS; 4 node shared RF đạt threshold; MEGA8 schedule ownership, temporary resume, ACK/flow/normalized storage và fail-safe có evidence traceable. |

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
| **S1.5-MEGA8-09** | MEGA8 vẫn là schedule owner; temporary OFF override hết hạn/đạt resume boundary thì node quay lại schedule đúng một lần, không bị ESP32 điều khiển định kỳ. | 🔴 BLOCKER |
| **S1.5-4NODE-10** | 4 node dùng chung RF channel có collision/polling/time-slot policy được test; ACK/telemetry/latency/loss đạt threshold đã phê duyệt. | 🔴 BLOCKER |
| **S1.5-PARSE-11** | Production persistence chỉ chứa parsed/normalized telemetry và command events; raw RF payload/frame không được lưu vào database. | 🔴 BLOCKER |
| **S1.5-REVALIDATE-12** | R3-M/R4-M/R5-M/R6-M và D4 PASS; evidence cũ không được dùng thay thế cho re-validation baseline 2026-08-22. | 🔴 BLOCKER |
| **S1.5-QUALITY-08** | `pio test -e native` và `pio run -e esp32-s3-devkitc-1` PASS từ `aeroponics-firmware/`; không có secret tracked. | 🔴 BLOCKER |

---

*Senior Solution Architect — Progress Tracker đã đồng bộ cho Sprint 1.5 ngày 2026-08-22 theo baseline ESP32-S3 + 4 MEGA8 autonomous schedule nodes.*
