### [2026-09-12 16:00] - Track S2-B: Treatment, Group & Dynamic Scheduler (S2-B1, S2-B2, S2-B3, S2-B4)
* **Trạng thái:** `[ ] QA Review` (Sẵn sàng kiểm toán độc lập)
* **Hạng mục đã hoàn thành:**
  - **S2-B1:** Production `TreatmentManager`: validation nghiêm ngặt status `PUBLISHED`, bounds `spray_day_s` [5..300], `cooldown_day_s` [30..7200], monotonic `config_version`, CRC-16 checksum; NVS persistence nguyên tử (write-then-verify) và rollback tự động/thủ công khi verify thất bại; 0 byte ghi flash trong scheduler/timer loop.
  - **S2-B2:** Versioned `GroupAssignment`: giới hạn chặt chẽ node IDs 1..4 và group IDs 0..4, monotonic `assignment_version`, kiểm tra single active group invariant (từ chối gán duplicate active group fail-closed), time-safe safe-OFF migration, phát sinh `AssignmentAuditEvent` có đầy đủ metadata (`assignment_version`, `effective_at`, `actor`, `node_id`, `old_group_id`, `new_group_id`, `reason`).
  - **S2-B3:** Production `GroupScheduler` (thay thế direct relay cũ): alias tương thích ngược `GroupScheduleManager`, nhóm 0 (`UNASSIGNED`) bảo đảm 0 lệnh RF ON/desired state ép OFF, tính toán Day/Night boundary chuẩn theo múi giờ `Asia/Ho_Chi_Minh` (UTC+7, lệch chuẩn +25200s, ngày 06:00–18:00 ICT, đêm 18:00–06:00 ICT) với chuyển đổi mượt mà giữa các cấu hình pha; clock invalid kích hoạt `forceSafeOff()` và trạng thái degraded/unassigned.
  - **S2-B4:** Manual Override & Group Pause/Resume Policy: override ON bắt buộc có lease và **bị từ chối tuyệt đối khi node ở FAULT/latched fault** (không thể bypass FSM an toàn); override OFF mang TTL để node tự resume schedule tại boundary mà không cần ESP32 gửi lệnh định kỳ; `pauseGroup` và `resumeGroup` đồng bộ trạng thái an toàn.
* **Files đã sửa / tạo:**
  - `[NEW]` `aeroponics-firmware/include/treatment_manager.h`
  - `[NEW]` `aeroponics-firmware/src/treatment_manager.cpp`
  - `[NEW]` `aeroponics-firmware/include/group_scheduler.h`
  - `[NEW]` `aeroponics-firmware/src/group_scheduler.cpp`
  - `[MODIFIED]` `aeroponics-firmware/include/group_schedule_manager.h` (forwarder/alias)
  - `[MODIFIED]` `aeroponics-firmware/src/group_schedule_manager.cpp`
  - `[TEST-ADDED]` `aeroponics-firmware/test/test_production/test_production.cpp` (`test_s2_b1`, `test_s2_b2`, `test_s2_b3`, `test_s2_b4`)
* **Kết quả kiểm thử:**
  - `cd aeroponics-firmware && ~/.platformio/penv/bin/pio test -e native`: **236/236 test cases PASSED** (0 failed, duration 1.50s)
  - `cd aeroponics-firmware && ~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`: **SUCCESS** (RAM 20.0%, Flash 21.6%)
  - `cd aeroponics-firmware && ~/.platformio/penv/bin/pio run -e atmega8-node`: **SUCCESS** (Flash 6388 / 7000B, RAM 301 / 900B)
  - `bash scripts/verify_production_clean_architecture.sh`: **PASS**
  - `git diff --check`: **PASS**

---

### [2026-09-12 15:52] - Track S2-A: Production RF Transport & Node Controller (S2-A1, S2-A2, S2-A3, S2-A4)
* **Trạng thái:** `[ ] QA Review` (Sẵn sàng kiểm toán độc lập)
* **Hạng mục đã hoàn thành:**
  - **S2-A1:** Production `IRfTransport` (`UartRfTransport`): UART1 riêng biệt (GPIO 18 RX, GPIO 17 TX, M0=15, M1=16, AUX=19, Baud 115200), không conflict USB debug Serial, non-blocking I/O, `crc_errors` / `dropped_bytes` / `tx_bytes` / `rx_bytes` counters, `isAuxReady()` & `setMode(m0, m1)`.
  - **S2-A2:** Production `RfFrameCodec`: `enum class ParseError` tường minh, fail-closed `decodeFrameDetailed` không cấp phát động, `DuplicateResponseCache` FIFO giới hạn 64 entry thread-safe (0 byte overhead trên ATmega8 qua guard `#if !defined(ATMEGA8_NODE_BUILD)`).
  - **S2-A3:** Production `PumpNodeController`: queue lệnh bounded, non-blocking retry engine (cấu hình `max_retries <= 3`, `retry_interval_ms = 1000ms`), idempotency qua tuple `{command_id, boot_session_id, sequence}`, an toàn hủy lệnh qua `cancelCommand(node_id, reason)` và deadman lease timeout.
  - **S2-A4:** Production `NodeRegistry`: giới hạn chặt chẽ 4 node (Node IDs 1..4 hợp lệ, 0 và 5..12 reject fail-closed), thread-safe mutex, configurable `stale_threshold_ms`, và phát hiện reboot qua `updateBootSession` + `NodeRebootCallback`.
* **Files đã sửa / tạo:**
  - `[NEW]` `aeroponics-firmware/include/pump_node_controller.h`
  - `[NEW]` `aeroponics-firmware/src/pump_node_controller.cpp`
  - `[MODIFIED]` `aeroponics-firmware/include/command_manager.h` (alias `using CommandManager = PumpNodeController;`)
  - `[MODIFIED]` `aeroponics-firmware/src/command_manager.cpp`
  - `[MODIFIED]` `aeroponics-firmware/include/rf_provisioning.h`
  - `[MODIFIED]` `aeroponics-firmware/include/uart_rf_transport.h`
  - `[MODIFIED]` `aeroponics-firmware/src/uart_rf_transport.cpp`
  - `[MODIFIED]` `aeroponics-firmware/include/rf_frame_codec.h`
  - `[MODIFIED]` `aeroponics-firmware/src/rf_frame_codec.cpp`
  - `[MODIFIED]` `aeroponics-firmware/include/node_registry.h`
  - `[MODIFIED]` `aeroponics-firmware/src/node_registry.cpp`
  - `[MODIFIED]` `aeroponics-firmware/src/main.cpp`
  - `[TEST-ADDED]` `aeroponics-firmware/test/test_production/test_production.cpp` (`test_s2_a1`, `test_s2_a2`, `test_s2_a3`, `test_s2_a4`)
* **Kết quả kiểm thử:**
  - `cd aeroponics-firmware && ~/.platformio/penv/bin/pio test -e native`: **232/232 test cases PASSED** (0 failed, duration 1.74s)
  - `cd aeroponics-firmware && ~/.platformio/penv/bin/pio run -e atmega8-node`: **SUCCESS** (Flash 6388 / 7000B, RAM 301 / 900B)
  - `cd aeroponics-firmware && ~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`: **SUCCESS** (0 errors)

---

### [2026-09-02 10:44] - Task R5-M: Re-validate schema/health-check theo scope 4 node và ownership MEGA8 (QA remediation)
* **Trạng thái:** `[x] Done` (Đã hoàn thành kiểm toán độc lập)
* **Lỗi QA đã nêu:** Health-check thất bại do database init dừng giữa chừng; rehearsal trước đó có fixture calibration literal (đã được sửa trong working tree trước remediation).
* **Files đã sửa:**
  - `[FIXED]` database/schema.sql (Dòng 148)
  - `[FIXED]` scripts/health-check.sh (Dòng 203)
* **Nguyên nhân gốc:** `node_registry.node_id` trong schema init thiếu dấu phẩy trước `display_name`, khiến PostgreSQL dừng init ở bảng thứ 8; named volume giữ lại database dở dang. Health-check constraint predicate khớp quá chặt với textual rendering của PostgreSQL (`trial_count >= 3`, `pulses_per_litre > 0`).
* **Giải pháp khắc phục:** Thêm dấu phẩy tối thiểu vào schema init; nới predicate health-check theo column contract thay vì phụ thuộc format số literal. Đã reset có kiểm soát volume disposable local `aero_timescale_data`; không xóa volume khác và không sửa migration fixture.
* **Kết quả tái kiểm thử:** PASS (`docker compose build`; `docker compose up -d`; containers healthy; `bash scripts/health-check.sh` 10/10; `bash scripts/rehearse_production_migration.sh` PASS; `cd aeroponics-firmware && ~/.platformio/penv/bin/pio test -e native` 228/228; `git diff --check`).

---

# Aeroponics Lean — Progress Tracker

> **Tracker hiện hành:** Cập nhật ngày **2026-08-22** theo xác nhận phần cứng/vận hành mới.
> - **Không triển khai tiếp đường 4 relay GPIO trực tiếp:** `RelayController`, `ScheduleManager`, MQTT relay command/topic, `relay_profiles`, `relay_events` và `sensor_readings` chỉ là **prototype rig / compatibility debt**, không phải production baseline.
> - **Sprint 1.5 đang ở pha Remediation S0–S1 bắt buộc:** thay thế hoặc cô lập code cũ trước khi viết POC RF + Flow. Không được xoá nền tảng còn tái sử dụng (boot-safe, NVS abstraction, RTC, WDT, FreeRTOS, Docker 3-service) khi chưa có successor và regression test.
> - **Sprint 2, 3, 4:** chỉ mở theo các gate đã nêu, với kiến trúc **ESP32-S3 RF Gateway + 4 MEGA8 Node + Season/Group/Flow Domain + Tuya On-demand**. Việc mở rộng quá 4 node là backlog, không thuộc scope hiện tại.

---

## 📌 Khái quát trạng thái dự án (Sprint Roadmap Summary)

| Sprint | Nội dung chính | Trạng thái | Tài liệu tham chiếu |
|---|---|---|---|
| **Sprint 0** | Hạ tầng Docker (TimescaleDB + Mosquitto + Backend) & schema | ✅ Done (Remediation hoàn tất) | [`sprint_0.md`](./sprint_0.md) |
| **Sprint 1** | Firmware Foundation (boot-safe, NVS, RTC, WDT, FreeRTOS) | ✅ Done (Nền tảng đã kiểm chứng, relay runtime thay thế xong) | [`sprint_1.md`](./sprint_1.md) |
| **Sprint 1.5** | Remediation S0–S1 → RF 433 MHz + Flow POC & Hardware Decision Gate | ✅ **DONE** — Track R/A/B/C/D PASS, 4-node acceptance evidenced | [`sprint_1_5.md`](./sprint_1_5.md) |
| **Sprint 2** | Firmware Production RF Gateway & 4 MEGA8 Node Control | 🚧 **ĐANG THỰC HIỆN** — Gate Sprint 1.5 đã PASS | [`sprint_2.md`](./sprint_2.md) |
| **Sprint 3** | NestJS Backend (Season + Group + Node + Flow + On-demand) | 🔵 Chờ Sprint 2 PASS | [`sprint_3.md`](./sprint_3.md) |
| **Sprint 4** | Single-file HTML Dashboard UI | 🔵 Chờ Sprint 3 PASS | [`sprint_4.md`](./sprint_4.md) |

---

## 📌 Thông tin vận hành Sprint 2

| Field | Value |
|---|---|
| **Thời gian cập nhật tracker** | 2026-09-12 (sau Sprint 1.5 PASS toàn bộ gate) |
| **Sprint hiện hành** | Sprint 2 — Production RF Gateway & 4 MEGA8 Node Control |
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
| **R3-M** | Re-validate R3 theo baseline 4 MEGA8. | [x] Done | Native regression có coverage, nhưng target `atmega8-node` chưa link được (flash overflow), nên chưa có bằng chứng target-level cho schedule persistence, override/resume và lease safe-off. Phải sửa footprint và chạy lại target build trước QA. |
| **R4-M** | Re-validate MQTT/command contract cho temporary override và normalized telemetry. | [x] Done | Remediation và focused native tests đã PASS: group OFF bắt buộc duration dương và bounded; command bị reject không enqueue/mutate state; normalized telemetry không lưu raw RF. Chờ audit độc lập, không tự đánh dấu Done. |
| **R5-M** | Re-validate schema/health-check theo scope 4 node và ownership MEGA8. | [x] Done | Migration rehearsal disposable, health-check 10/10 và native regression đã PASS; schema hỗ trợ node `1..4`, dual timestamps, schedule/override/resume và calibration integrity. Chờ audit độc lập, không tự đánh dấu Done. |
| **R6-M** | Xác nhận build/runtime không còn đường direct relay hoặc ESP32 scheduler trong production. | [x] Done | `rg`/build/test chứng minh gateway không include/construct `RelayController`, `ScheduleManager`, relay GPIO hoặc periodic schedule fan-out; test composition root và native regression PASS. Không xoá prototype code nếu chưa có archive/rollback evidence. |

### Gate chuyển từ Track R sang Track A–D POC

- [x] `docs/SPRINT_0_1_LEGACY_REPLACEMENT.md` đã được review, nêu rõ file nào giữ/thay/xoá và rollback/migration plan.
- [x] `docs/RF_PROTOCOL.md` và `docs/RF_FLOW_POC_TEST_PLAN.md` tồn tại **trước** codec, UART adapter và actuator POC.
- [x] Gateway production path không điều khiển relay GPIO trực tiếp; node POC boot OFF, có lease/deadman độc lập.
- [x] Schema, MQTT ACL/topic và health-check không còn xác nhận `relay_*`/continuous Tuya polling là production success.
- [x] Regression tối thiểu PASS: firmware native tests, ESP32 gateway build và kiểm tra secret tracked; evidence ghi vào `WALKTHROUGH_LOG.md`.
- [x] R3-M/R4-M/R5-M/R6-M PASS theo baseline 4 MEGA8; các gate `[x]` cũ không được dùng thay thế cho các re-validation này.

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
| **A1** | Lập inventory candidate RF 433 MHz, node MCU, relay/driver bơm, nguồn, anten và flow sensor. Tạo `docs/RF_FLOW_POC_DECISION.md` với part number, datasheet URL/path, điện áp, dòng, interface và rủi ro. | [x] Done | Dùng **ADR**: alternatives, trade-off, tiêu chí reject và owner phê duyệt. Candidate LoRa UART transparent chỉ là POC, không tự thành BOM. Bắt buộc kiểm tra availability, RF regulation/duty-cycle, UART logic, công suất phát, anten, pump nominal/inrush/stall, rating driver có margin và ngân sách nguồn/brownout. |
| **A2** | Vẽ wiring diagram POC cho gateway ↔ RF ↔ node, driver pump, pump feedback và flow sensor. | [x] Done | Áp dụng **Hardware Interface Contract**. Nêu TX/RX, GND/topology hoặc isolation, level shifting, decoupling/RF rail, fuse, TVS, reverse-polarity, connector rating và emergency isolation. DC phải có flyback; AC phải có snubber/MOV/contactor strategy. Tách USB debug/UART RF, tránh GPIO strapping; test EMI/RF reset khi pump switching và bố trí anten tránh dây pump. |
| **A3** | Chọn và chứng minh cơ chế pump feedback tại node. | [x] Done | Áp dụng **defence in depth**. Phân biệt bắt buộc `driver_feedback` với `load_feedback`; không gọi relay output là pump đang chạy. Nếu POC chỉ driver feedback + flow, ghi rõ failure modes điện không phát hiện được. Nếu current/aux sensing, chốt threshold, debounce, open-load/stall/stuck-relay coverage, ADC/filtering/isolation và test evidence. |
| **A4** | Chuẩn bị flow bench: nguồn nước, đường ống, bình đo thể tích chuẩn và quy trình hiệu chuẩn an toàn. | [x] Done | Áp dụng **measurement traceability**. Version calibration procedure: reference volume tối thiểu, multi-point operating range, nước/áp lực/van-nozzle/pump supply/nhiệt độ, ≥3 trial, variance/repeatability/reject threshold. Có công thức `pulses_per_litre`, `flow_lpm`, `delivered_volume_l`; bảo vệ rò nước/điện, chống chạy khô và van ngắt khẩn. |

## TRACK B — RF Transport POC

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật cấp cao |
|---|---|---|---|
| **B1** | Viết/version `docs/RF_PROTOCOL.md` cho ESP32-S3 ↔ MEGA8; khai báo `IRfTransport`, `RfFrameCodec` và constants tách hardware adapter; unit test encode/decode/CRC/length/version/node-id/duplicate. | [x] Done | Áp dụng **Ports and Adapters / Dependency Inversion**: codec C++ thuần, parser bounded/fail-closed, không allocation động. Trước code phải kiểm chứng HMAC-SHA256 feasibility trên ATmega8 về flash/RAM/CPU hoặc phê duyệt adapter thay thế. Spec phải chốt SOF, version, endian, CRC vectors, enums, schemas, MAX payload/RX buffer/timeout, sequence wrap, boot session, command ID và temporary override/resume semantics. MAC/HMAC + anti-replay bắt buộc; secret chỉ qua secure provisioning. Fuzz malformed frames. |
| **B2** | Implement UART adapter cho RF candidate; gateway và node trao đổi `PING/PONG` ổn định, tách khỏi debug Serial. | [x] Done | Adapter chỉ implement `IRfTransport`; không trộn framing/protocol/business logic. Non-blocking I/O, bounded RX buffer, timeout, TX/RX/CRC/drop counters; không log ISR, rate-limit log. Chốt heartbeat interval, telemetry ON/OFF/fault và stale threshold; test node power-off → `STALE/RF_TIMEOUT`, node reboot tạo boot-session mới và không tự resume ON. |
| **B3** | Implement command manager POC cho `SET_PUMP`, `COMMAND_ACK`, timeout, bounded retry, idempotency và node-side lease. | [x] Done | Áp dụng **Command pattern + safety FSM**. Mỗi command có `command_id`, boot-session/sequence và `run_lease_ms`; duplicate trả outcome cũ, không actuate hai lần. Queue/retry/backoff bounded, không busy-wait. Node boot OFF trước stack; lease expiry force OFF độc lập gateway, audit `LEASE_EXPIRED_SAFE_OFF`. Test gateway loss khi ON và replay/unauthenticated command rejection. |
| **B4** | Đo RF tại vị trí triển khai: latency/loss theo khoảng cách, vật cản, power-cycle reconnect và link quality nếu hỗ trợ. | [x] Done | Chỉ chạy theo `docs/RF_FLOW_POC_TEST_PLAN.md` đã chốt trước bench: sample size và PASS/FAIL không được đổi hậu nghiệm. Ghi firmware/wiring/RF config, pump switching EMI, ON/OFF/retry/timeout, p50/p95/p99 và breakdown UART/airtime/node/ACK/flow. Sau POC 1 node phải kiểm thử 4 node chung channel với polling/time-slot/collision policy. Không suy luận ngoài scope 4 node hiện tại. |
| **B5** | Implement/test temporary override và schedule resume trên MEGA8. | [x] Done | `SET_PUMP(OFF)` chỉ override tạm thời; MEGA8 giữ schedule và tự resume tại boundary/điểm resume đã chốt. Test OFF giữa chu kỳ, expiry, ON override, reboot, mất RF, duplicate command và chứng minh ESP32 không chạy schedule định kỳ. |
| **B6** | Implement MEGA8 RF node adapter/telemetry sender và gateway parser cho 4 node. | [x] Done | Node ID `1..4`, UART/RF framing, ACK/telemetry/heartbeat, boot session/sequence, authenticated command và normalized telemetry hoạt động trên hardware thật; không dùng echo payload. |

## TRACK C — Pump Feedback & Flow Measurement POC

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật cấp cao |
|---|---|---|---|
| **C1** | Implement node actuator + telemetry `desired`, `reported`, `driver` và `pump_feedback`; kiểm tra ON/OFF thực tế. | [x] Done | `SPEC-FEEDBACK-001` explicit-state model: commanded != driver feedback != load current != flow. Boot-safe default LOW, fault latch triggers hard safe-off. Optocoupler gate sense, ACS712 current sensing (>150mA), inrush blanking (80ms), stall protection (>3.8A/50ms), dual timestamps & command correlation. |
| **C2** | Implement pulse counter flow bằng ISR hoặc counter phần cứng và conversion L/min. | [x] Done | ISR chỉ tăng counter atomic/hardware counter; cấm I/O/allocation/log/blocking. Snapshot atomic tính `flow_lpm`, `delivered_volume_l`, `pulse_count`, `sample_window_ms`; xử lý counter reset/overflow, bounce/noise, pulse bất thường, zero/stale/disconnect và >6 L/min. Có host unit test conversion/calibration math và input boundary. |
| **C3** | Hiệu chuẩn flow sensor tối thiểu 3 lần trên node prototype; tính `pulses_per_litre`, sai số và lưu calibration version. | [x] Done | Áp dụng **calibration as versioned configuration** theo sensor serial + node ID + version, không hard-code hệ số chung. Lưu raw trials, reference volume, điều kiện, mean/variance/repeatability/sai số; đo nhiều điểm dải vận hành và reject theo threshold định lượng đã phê duyệt. Không overwrite calibration active nếu không có version/audit. |
| **C4** | Implement và test flow/fault evaluation: `FLOW_CONFIRMED`, `NO_FLOW_FAULT`, `UNEXPECTED_FLOW_FAULT`, invalid input và over-range. | [x] Done | Áp dụng **safety FSM**. ON chỉ success sau `RF_ACKED → PUMP_FEEDBACK_ON → FLOW_CONFIRMED`; ACK riêng lẻ không đủ. `min_flow_lpm`, `max_off_flow_lpm`, `max_flow_lpm`, `flow_start_timeout_s` configurable per node/treatment và chốt trước test. Fault latch/audit + safe-off; không tự clear vì telemetry chập chờn. |
| **C5** | Chốt normalized telemetry và analytics contract. | [x] Done | Chỉ lưu parsed fields: desired/reported, driver/load feedback, flow, pulses, volume, fault, command ID, ACK outcome, sequence/session, node/gateway timestamps, retry/timeout/stale và schedule-vs-override. Không persist raw RF frame. Có metric latency, confirmation rate, runtime, volume/run, stability, packet loss và mismatch. |

## TRACK D — Evidence, QA & Decision Gate

| Task ID | Mô tả Task | Status | Note / Chỉ thị kỹ thuật cấp cao |
|---|---|---|---|
| **D1** | Viết/version `docs/RF_FLOW_POC_TEST_PLAN.md`, test matrix và chạy QA POC; lưu evidence timestamp, firmware/wiring revision, điều kiện, expected/actual/result. | [x] Done | Áp dụng **traceable verification matrix**: threshold + sample size/PASS-FAIL phải được duyệt trước bench; map case tới `S1.5-*`, commit và raw evidence. Bao gồm malformed/auth/replay/duplicate, ACK/NACK/timeout, lease gateway-loss, ON/OFF, no/stuck flow, sensor invalid, RF loss/power-cycle, heartbeat/stale recovery, brownout và EMI pump switching. Screenshot/log đơn lẻ không đủ. |
| **D2** | Review fail-safe cho power loss gateway/node, RF timeout, RTC invalid, pump feedback mismatch và sensor fault. | [x] Done | Áp dụng **fail-safe by default + FMEA** versioned: RF timeout, gateway/node reboot, no/unexpected flow, sensor disconnect/stale, feedback mismatch, RTC invalid. Mỗi mode có detection, node action, gateway action, retry/escalation, latch/reset/recovery và owner. Phải chốt node-only OFF hay group-stop; không có `RUNNING` giả khi node fault/stale. |
| **D3** | Ra quyết định BOM/protocol qua `RF_FLOW_POC_DECISION.md`: phê duyệt hoặc reject candidate với remediation rõ ràng. | [x] Done | Chỉ QA Review khi toàn bộ blocker PASS và RF evidence đủ. Decision record phải link `RF_PROTOCOL.md`, test plan/FMEA/calibration, chốt/đề xuất BOM/anten/mode/baud/pinout, lease, heartbeat/stale, security posture/risk acceptance, electrical-water-EMI safety và open risks. Không mở Sprint 2 nếu thiếu sign-off độc lập. |
| **D4** | QA regression toàn bộ Track R re-validation + 4-node acceptance. | [x] Done | R3-M/R4-M/R5-M/R6-M PASS; 4 node shared RF đạt threshold; MEGA8 schedule ownership, temporary resume, ACK/flow/normalized storage và fail-safe có evidence traceable. |

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

---

# 🚧 Sprint 2 — Production RF Gateway & 4 MEGA8 Node Control

> **Gate mở:** Sprint 1.5 PASS toàn bộ (Track R/A/B/C/D, 4-node acceptance evidenced).
> **Phạm vi firmware:** ESP32-S3 gateway production — RF transport, frame codec, node controller, group scheduler, MQTT integration.
> **Không thuộc Sprint này:** NestJS API/UI production (Sprint 3–4); không hard-code mapping node/group hay ngưỡng flow trong source.
> **Nguồn phân rã:** [`sprint_2.md`](./sprint_2.md)

## TRACK S2-A — Production RF Transport & Node Controller

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_2.md` — TRACK A

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S2-A1 | Promote POC RF adapter `rf_transport.*` thành production `IRfTransport` implementation; tách hoàn toàn khỏi USB debug Serial. | `[ ] QA Review` | (1) UART RF phải độc lập hoàn toàn khỏi `Serial` debug — kiểm tra bằng `pio run -e esp32-s3-devkitc-1` không còn conflict pin và không log trong ISR/timer callback. (2) Config baud/pin/timeout đọc từ POC decision record (`docs/RF_FLOW_POC_DECISION.md`), không hardcode; counter TX/RX/CRC/drop phải có và kiểm tra được bằng host unit test. |
| S2-A2 | Implement `RfFrameCodec` — bounded parser/encoder với CRC-16, version, length, node-id validation và duplicate response cache. | `[ ] QA Review` | (1) Parser fail-closed: frame vượt MAX_PAYLOAD_BYTES hoặc CRC sai phải return `ParseError` không allocation động — kiểm tra bằng fuzz test với ≥20 malformed inputs. (2) Duplicate response cache phải bounded (≤64 entries FIFO) và thread-safe; test encode→decode round-trip với test vectors từ `docs/RF_PROTOCOL.md`. |
| S2-A3 | Implement `PumpNodeController` — queue bounded, per-node sequence, ACK/NACK/retry/timeout, cancellation, no busy-wait. | `[ ] QA Review` | (1) Mỗi command có `command_id` + `boot_session_id` + sequence; duplicate command trả outcome cũ không actuate lần hai — kiểm tra bằng test replay command với cùng `command_id`. (2) Queue retry phải bounded (≤3 retry configurable), backoff không blocking FreeRTOS task khác; test gateway-loss-during-ON phải dẫn đến `COMMAND_TIMEOUT` và node force-OFF qua lease. |
| S2-A4 | Implement `NodeRegistry` — tối đa 4 node, heartbeat freshness tracking, reboot detection, state snapshot thread-safe. | `[ ] QA Review` | (1) Registry reject node_id ngoài dải 1–4; state snapshot access phải dùng mutex/critical section — kiểm tra bằng concurrent access test trên host. (2) Heartbeat freshness: node quá `STALE_THRESHOLD_MS` (configurable) phải chuyển health → `STALE`; reboot detection qua boot_session thay đổi phải emit event — kiểm tra bằng unit test simulate timeout. |

## TRACK S2-B — Treatment, Group & Dynamic Scheduler

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_2.md` — TRACK B

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S2-B1 | Implement treatment snapshot/version validation + NVS persistence: chỉ nhận `PUBLISHED` version, atomic update/rollback, không flash write trong timer loop. | `[ ] QA Review` | (1) Reject treatment version có status khác `PUBLISHED`; update NVS chỉ khi `config_version` mới hơn version hiện tại — kiểm tra bằng test gửi stale version phải bị reject. (2) NVS write phải atomic (write-then-verify); rollback về snapshot cũ nếu verify fail — test power-cut simulation sau write và kiểm tra recovery state. |
| S2-B2 | Implement versioned group assignment — node chỉ có một assignment active, change có hiệu lực time-safe, audit event emitted. | `[ ] QA Review` | (1) Reject assignment nếu node_id đã thuộc group active khác; reject group_id ngoài 1–4 và node_id ngoài 1–4 — kiểm tra bằng test duplicate assignment phải trả error code rõ ràng. (2) Audit event phải bao gồm `assignment_version`, `effective_at`, actor và old/new group_id; không mutate active assignment mà không emit event — test verify event structure. |
| S2-B3 | Replace 4 direct-relay scheduler path bằng `GroupScheduler` — fan-out per group/node, `UNASSIGNED` group không actuate, timezone day/night test PASS. | `[ ] QA Review` | (1) `UNASSIGNED` group phải không gửi bất kỳ RF command nào — kiểm tra bằng `rg` không còn relay GPIO call trong production path và test `UNASSIGNED` group không emit command. (2) Day/night boundary timezone Asia/Ho_Chi_Minh phải dùng UTC offset không hardcode locale; test boundary tại 06:00 và 18:00 ICT với mock clock. |
| S2-B4 | Implement manual override/pause/resume policy — override có TTL/audit, không bypass RF feedback/fail-safe, deterministic recovery to schedule. | `[ ] QA Review` | (1) Override OFF phải mang `run_lease_ms`/TTL; sau TTL node resume schedule đúng một lần không repeat — test confirm MEGA8 resume schedule sau override expiry không do ESP32 gửi command. (2) Override không bypass fail-safe FSM: nếu node ở FAULT state, override ON phải bị reject — kiểm tra bằng test sequence FAULT → override ON → expect REJECT. |

## TRACK S2-C — Pump Feedback, Flow & Safety FSM

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_2.md` — TRACK C

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S2-C1 | Parse/store node pump feedback + flow telemetry: `desired`/`reported`/`pumpFeedback` fields riêng biệt, dual timestamps node/gateway, invalid payload rejected. | `[ ] Pending` | (1) `reportedPumpState` và `pumpFeedbackState` không được suy ra từ `desiredPumpState` — kiểm tra bằng test inject mismatched states và verify các field độc lập. (2) Payload không có field bắt buộc hoặc có giá trị ngoài enum phải bị reject với error log; không store partial/corrupt telemetry — test với malformed JSON và missing required fields. |
| S2-C2 | Implement `FlowEvaluator`: `FLOW_CONFIRMED`, `NO_FLOW_FAULT`, `UNEXPECTED_FLOW_FAULT`, `SENSOR_FAULT`, over-range và stale conditions với unit tests. | `[ ] Pending` | (1) Tưới thành công chỉ sau sequence `RF_ACKED → PUMP_FEEDBACK_ON → FLOW_CONFIRMED`; ACK đơn lẻ không đủ — kiểm tra bằng test sequence missing PUMP_FEEDBACK_ON không được `FLOW_CONFIRMED`. (2) Threshold `min_flow_lpm`, `max_off_flow_lpm`, `max_flow_lpm`, `flow_start_timeout_s` phải configurable per node/treatment — test over-range (>6 L/min) và zero-flow-after-ON phải tạo đúng fault code. |
| S2-C3 | Implement fail-safe policy: RF timeout/node stale/RTC invalid/fault dẫn đến safe-off đã phê duyệt; event reason per node/group. | `[ ] Pending` | (1) Mỗi fail-safe trigger phải emit event với reason code rõ ràng (`RF_TIMEOUT`, `STALE_NODE`, `RTC_INVALID`, `FAULT_LATCH`) — kiểm tra bằng test inject từng trigger và verify event reason. (2) Safe-off không được tự clear fault latch chỉ vì node reconnect; phải có explicit fault-reset command từ backend — test node reconnect sau FAULT không tự resume ON. |
| S2-C4 | Persist only configuration và essential recovery snapshot vào NVS: không ghi NVS trong telemetry/timer loop; reboot recovery documented/tested. | `[ ] Pending` | (1) NVS write chỉ được gọi từ command handler hoặc config update path — kiểm tra bằng profiling không có NVS write trong task loop period. (2) Reboot recovery test: sau power-cut giữa ON command, gateway boot phải query node state và không assume pump ON — test boot sequence với ambiguous state. |

## TRACK S2-D — MQTT Production Integration

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_2.md` — TRACK D

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S2-D1 | Adapt MQTT client/topic ACL từ relay domain sang gateway/group/node domain: LWT QoS 1 retained, reconnect bounded, credentials secure, subscriptions least privilege. | `[ ] Pending` | (1) MQTT credentials phải đọc từ NVS/env không hardcode trong source — kiểm tra bằng `rg` không tìm thấy password literal trong production code. (2) Reconnect phải bounded (≤5 retry với exponential backoff) và không block scheduler loop; LWT phải được publish QoS 1 retained — test Mosquitto ACL denial với wrong credential. |
| S2-D2 | Implement config/override command routing: DTO/JSON validation, idempotent `command_id`, stale version rejection, no direct GPIO call từ MQTT callback. | `[ ] Pending` | (1) MQTT callback không được trực tiếp actuate GPIO hay RF — phải enqueue lên command manager queue; test direct-GPIO call không tồn tại trong `onMessage` handler. (2) Duplicate `command_id` trong 60s window phải trả cached outcome không actuate lần hai; stale `config_version` bị reject với negative ACK — test với replay command và stale version. |
| S2-D3 | Publish heartbeat, group/node snapshots và append-only events: payload schema versioned, bounded buffers, publish failures tracked, no false completion. | `[ ] Pending` | (1) `ack/{command_id}` chỉ publish `completed` sau RF outcome với irrigation result; không publish `completed` chỉ dựa trên MQTT publish() success — kiểm tra bằng test sequence gateway receives ACK → flow evaluation → then publish ack. (2) Publish buffer bounded; publish failure phải increment counter và retry bounded không block scheduler — test Mosquitto disconnect giữa publish. |
| S2-D4 | Mosquitto integration test: LWT behavior, ACL denial, command lifecycle và offline behavior evidenced. | `[ ] Pending` | (1) LWT message phải xuất hiện trong Mosquitto khi gateway disconnect đột ngột — test bằng kill gateway process và verify LWT retained message. (2) Command lifecycle test: từ publish command đến receive ACK đến update state phải có evidence log đầy đủ; ACL test phải show denial khi dùng wrong topic — kết quả lưu trong `WALKTHROUGH_LOG.md`. |

## TRACK S2-E — System Test & Production Readiness

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_2.md` — TRACK E

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S2-E1 | Build node simulator/test harness cho 4 node identities: deterministic ACK/drop/delay/feedback/flow/fault scenarios. | `[ ] Pending` | (1) Simulator phải cover ít nhất: ACK normal, ACK delayed >timeout, drop (no ACK), feedback mismatch, flow=0 sau ON, unexpected flow khi OFF — test matrix với expected outcome mỗi scenario. (2) Simulator phải chạy trên native env (`pio test -e native`) không cần hardware; seed xác định để reproducible. |
| S2-E2 | Hardware bench test 4 node: measured latency/loss/freshness/throughput, staggered telemetry prevents collision, results documented. | `[ ] Pending` | (1) Test plan phải sử dụng threshold đã phê duyệt từ `docs/RF_FLOW_POC_DECISION.md`; không thay đổi PASS/FAIL threshold sau khi có kết quả — evidence gồm timestamp, firmware version, RF config và raw latency log. (2) 4 node cùng channel phải test collision/time-slot policy: verify không có command drop do collision vượt ngưỡng đã chốt; p95 latency phải trong giới hạn. |
| S2-E3 | Power-cycle và fault injection: gateway/node reset, RF outage, no-flow, stuck-flow, sensor disconnect và MQTT loss verified. | `[ ] Pending` | (1) Mỗi fault scenario phải có detection log, node action, gateway action và recovery evidence — không chấp nhận scenario "no crash" mà không có proof state recovery. (2) Node reset giữa ON command phải dẫn đến gateway nhận boot_session mới và không assume pump state — test evidence phải show `STALE → ONLINE` transition với state requery. |
| S2-E4 | Production readiness review: tất cả QA blocker PASS, docs/pinout/BOM/config migration sẵn sàng cho Sprint 3. | `[ ] Pending` | (1) QA gates S2-RF-01 đến S2-QUALITY-08 phải PASS có evidence traceable; không đánh dấu S2-E4 Done nếu còn bất kỳ BLOCKER nào open — checklist phải được Senior Solution Architect sign off. (2) Handoff package cho Sprint 3 phải bao gồm: schema MQTT versioned, RF/flow decision record, node inventory, treatment/assignment command contract samples và configuration keys list. |

---

# 🔵 Sprint 3 — NestJS Backend (Season + Group + Node + Flow + RF Command + On-demand Measurement)

> **Gate mở:** Sprint 2 PASS toàn bộ (S2-RF-01 đến S2-QUALITY-08); RF gateway chạy và node telemetry vào MQTT.
> **Output:** NestJS backend đầy đủ: MQTT ingest → TimescaleDB, Tuya on-demand, REST API, WebSocket realtime.
> **Chiến lược:** Tái sử dụng boilerplate `mushroom-cp` (database/config/mqtt/auth), adapt cho aeroponics domain.
> **Nguồn phân rã:** [`sprint_3.md`](./sprint_3.md)

## TRACK S3-A — Boilerplate & Infrastructure Setup

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_3.md` — TRACK A

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S3-A1 | Bootstrap NestJS project `aeroponics-backend/`: copy boilerplate từ `mushroom-backend`, adapt `DatabaseModule`, `AppConfigModule`, `MqttModule`, `AuthModule`; xóa InfluxDB dependency. | `[ ] Pending` | (1) Không được có bất kỳ import `@influxdata/influxdb-client` hoặc `InfluxModule` nào trong production source — kiểm tra bằng `rg '@influxdata'` và `rg 'InfluxModule'` trả về 0 match. (2) `synchronize: false` bắt buộc trong TypeORM config; credential đọc từ `.env` qua `@nestjs/config` — kiểm tra bằng test start app với missing DATABASE_URL phải throw configurable error. |
| S3-A2 | Implement `DatabaseModule` + TypeORM config với `synchronize: false`; tất cả schema change qua migrations; connection string từ `DATABASE_URL` env. | `[ ] Pending` | (1) Migration phải chạy auto khi app start (`runMigrations: true`) nhưng không sync schema — kiểm tra bằng test fresh DB apply migrations thành công. (2) Không expose TimescaleDB port ra host trong `docker-compose.yml` — kiểm tra bằng `docker-compose config` không có port mapping cho timescaledb service ra 0.0.0.0. |
| S3-A3 | Adapt `MqttModule`/`MqttService` subscribe aeroponics topics; `onMessage` handler wrap trong try/catch toàn bộ; credentials từ env. | `[ ] Pending` | (1) `onMessage` phải catch tất cả exception — không có unhandled promise rejection trong MQTT handler; test inject malformed telemetry payload không crash service. (2) Subscribe topics theo Sprint 2 contract: `aeroponics/device/+/status`, `aeroponics/telemetry/node/+/snapshot`, `aeroponics/telemetry/node/+/event`, `aeroponics/ack/+` — verify bằng MQTT trace log. |

## TRACK S3-B — TypeORM Entities & Migrations

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_3.md` — TRACK B

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S3-B1 | Define entity `season.entity.ts`: `id`, `name`, `started_at`, `ended_at` (nullable), `status` ('ACTIVE'\|'ENDED'), `notes`; tạo migration. | `[ ] Pending` | (1) Constraint: không được có 2 season `ACTIVE` cùng lúc — enforce bằng DB partial unique index `WHERE status='ACTIVE'`; test insert second ACTIVE season phải fail. (2) `ended_at` chỉ set khi `status='ENDED'`; không auto-delete trước khi end và đối soát — test delete ACTIVE season phải bị reject ở service layer. |
| S3-B2 | Define entities `treatment.entity.ts` và `treatment_version.entity.ts` với versioning; tạo migration cho cả hai. | `[ ] Pending` | (1) `published_at` nullable; chỉ một version được publish lần (immutable sau publish) — enforce bằng check constraint hoặc service-layer; test republish đã-published version phải throw error. (2) `spray_day_s`, `cooldown_day_s`, `spray_night_s`, `cooldown_night_s` phải có range validation (>0, ≤ max từ POC decision record) — test out-of-range values bị reject với 400. |
| S3-B3 | Define entity `group_assignment.entity.ts`: `group_id` (1–4), `treatment_version_id` FK, `node_ids` (int[]), `season_id` FK, `assigned_at`, `active`; tạo migration. | `[ ] Pending` | (1) `group_id` constraint 1–4 và `node_ids` phải chỉ chứa values 1–4; không có node thuộc 2 group active — enforce bằng application-level check và DB trigger hoặc unique partial index. (2) `active` flag phải atomic update — sử dụng transaction khi unassign old và assign new; test concurrent assignment race condition. |
| S3-B4 | Define entity `node_registry.entity.ts`: `node_id` PK (1–4), `display_name`, `group_id` nullable, `calibration_pulses_per_litre`, `last_seen_at`, `health_status`; tạo migration. | `[ ] Pending` | (1) `calibration_pulses_per_litre` phải numeric precision (không float) để tránh rounding error trong flow calculation — dùng `decimal(10,4)`; test precision không bị truncate. (2) `health_status` enum: 'OK'\|'STALE'\|'FAULT'\|'SAFE_OFF' — test invalid enum value bị reject tại entity validation. |
| S3-B5 | Define hypertable entity `pump_command.entity.ts`: `time`, `command_id` uuid, `node_id`, `group_id`, `action`, `rf_seq`, `outcome` enum, `acked_at`, `flow_confirmed_at`, `fault_reason`; tạo migration + hypertable. | `[ ] Pending` | (1) `outcome` enum phải cover: 'PENDING', 'RF_ACKED', 'FLOW_CONFIRMED', 'FAULT_NO_ACK', 'FAULT_NO_FLOW', 'FAULT_UNEXPECTED_FLOW', 'FAULT_SENSOR', 'TIMEOUT' — không thiếu state nào. (2) Hypertable chunk interval phải configurable (default 7 days); `command_id` phải có unique index — test duplicate command_id insert phải fail. |
| S3-B6 | Define hypertable entity `flow_event.entity.ts`: `time`, `node_id`, `litres_total`, `pulse_count`, `flow_rate_lpm`, `is_fault`; define `measurement_reading.entity.ts`; tạo migrations + hypertables. | `[ ] Pending` | (1) `flow_event` không lưu raw RF frame — chỉ parsed fields; test verify no raw_frame column tồn tại trong migration. (2) `measurement_reading` trigger type phải là 'ON_DEMAND'\|'END_OF_SEASON' không 'SCHEDULED' — test insert với invalid trigger_type bị reject. |

## TRACK S3-C — Season Module

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_3.md` — TRACK C

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S3-C1 | Implement `SeasonService`: `create`, `getActive`, `endSeason`, `list`; validate không duplicate ACTIVE. | `[ ] Pending` | (1) `create` phải check không có ACTIVE season trước khi tạo — throw `ConflictException` nếu vi phạm; test create second season khi có ACTIVE. (2) `endSeason` phải set `ended_at = NOW()` và `status = 'ENDED'` trong một transaction; không cho phép end season đã ENDED — test idempotency. |
| S3-C2 | Implement `SeasonController`: REST endpoints `/api/season` (POST, GET list), `/api/season/active` (GET), `/api/season/:id/end` (PUT); JWT auth guard. | `[ ] Pending` | (1) Tất cả endpoints phải có JWT auth guard; unauthenticated request trả 401 — test with/without token. (2) DTO validation bằng class-validator cho POST body (`name` required, `notes` optional); invalid body trả 400 với error detail. |

## TRACK S3-D — Treatment Module

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_3.md` — TRACK D

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S3-D1 | Implement `TreatmentService`: `create`, `addVersion`, `publishVersion`, `clone`, `archive`; version immutable sau publish. | `[ ] Pending` | (1) `publishVersion` phải set `published_at` và reject nếu version đã được publish — throw `ConflictException`; test republish trả 409. (2) `clone` phải tạo treatment mới với versions copy nhưng `published_at = null` (draft) — test cloned version không inherit published status. |
| S3-D2 | Implement `TreatmentController`: REST endpoints `/api/treatment` (POST, GET list), `/:id/version` (POST), `/:id/version/:versionId/publish` (PUT), `/:id/clone` (POST). | `[ ] Pending` | (1) All endpoints JWT protected; DTO validation với range check (`spray_day_s > 0`, `cooldown_day_s > 0`) — test out-of-range values trả 400. (2) Publish endpoint phải idempotent trong 1 request; concurrent publish race phải handle bằng optimistic lock hoặc transaction. |

## TRACK S3-E — Group & Node Module

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_3.md` — TRACK E

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S3-E1 | Implement `GroupService`: `assignTreatmentVersion`, `unassign`, `getGroupStatus` với node list, treatment, current phase. | `[ ] Pending` | (1) `assignTreatmentVersion` phải validate: group_id 1–4, treatment version PUBLISHED, không có node trùng assignment active — reject với error code rõ ràng nếu vi phạm. (2) `getGroupStatus` phải tính current phase (DAY/NIGHT) dựa trên ICT timezone — không hardcode UTC offset, dùng `luxon` hoặc `date-fns-tz`; test boundary 06:00 ICT. |
| S3-E2 | Implement `NodeService`: `register`, `updateHealth`, `handleTelemetry` (upsert `last_seen_at`, emit staleness warning), `getNodeStatus`. | `[ ] Pending` | (1) `handleTelemetry` phải emit `staleness_alert` WebSocket event nếu `last_seen_at` > `STALE_THRESHOLD_MS` (default 120s, configurable qua env) — test với mock time advance. (2) `updateHealth` phải không cho phép transition từ `FAULT` → `OK` mà không có explicit fault-reset command — test inject fault then direct health update. |
| S3-E3 | Implement `GroupController` và `NodeController`: REST endpoints theo Sprint 3 API table; JWT auth; DTO validation. | `[ ] Pending` | (1) `PUT /api/group/:id/assign` phải validate `node_ids` array chỉ chứa integers 1–4, không duplicate, không thuộc group khác — test với invalid node IDs trả 400. (2) `PUT /api/node/:id/calibration` phải validate `calibration_pulses_per_litre > 0` và trong dải hợp lý (>0, <10000) — test boundary values. |

## TRACK S3-F — PumpCommand Module

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_3.md` — TRACK F

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S3-F1 | Implement `PumpCommandService.sendCommand`: publish MQTT với `command_id` + `rf_seq` + `deadman_lease`; save `pump_command` row với outcome `PENDING`. | `[ ] Pending` | (1) `command_id` phải là UUID v4; `rf_seq` phải monotonic per node trong session; không reuse sequence sau reboot — verify bằng test 2 commands cùng node có rf_seq khác nhau. (2) MQTT publish command phải dùng topic `aeroponics/command/node/{nodeId}/override` với schema từ Sprint 2 contract; test verify published payload structure. |
| S3-F2 | Implement `handleRfAck`, `handleFlowConfirmed`, `handleFault`; deadman timer cancel leases on module destroy. | `[ ] Pending` | (1) `handleFlowConfirmed` chỉ được update outcome → `FLOW_CONFIRMED` sau sequence `RF_ACKED → PUMP_FEEDBACK_ON → FLOW_CONFIRMED` — test inject `FLOW_CONFIRMED` tanpa `RF_ACKED` phải bị reject. (2) Deadman/lease cancel trong `onModuleDestroy`: tất cả PENDING commands phải receive `FAULT_BACKEND_DISCONNECT` outcome — test module destroy với pending commands. |
| S3-F3 | Implement anti-replay: reject duplicate `rf_seq` trong 60s window cho cùng node. | `[ ] Pending` | (1) Replay window 60s phải configurable qua env `MQTT_ANTIREPLAY_WINDOW_MS`; không hardcode — test với custom window value. (2) Rejected replay phải log warning với node_id và rf_seq; không actuate và không update DB — test replay inject và verify DB không change. |
| S3-F4 | Implement `PumpCommandController`: `POST /api/group/:groupId/command`, `GET /api/node/:nodeId/commands?limit=50`; auth guard. | `[ ] Pending` | (1) `POST /api/group/:groupId/command` phải validate group có ACTIVE assignment trước khi gửi command — trả 409 nếu group UNASSIGNED. (2) `GET commands` phải paginate tối đa `limit` records (default 50, max 200); invalid `limit` trả 400. |

## TRACK S3-G — Flow Module

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_3.md` — TRACK G

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S3-G1 | Implement `FlowService`: `getHistory(nodeId, hours)`, `getCalibration(nodeId)`, `updateCalibration(nodeId, pulsesPerLitre)` với version audit. | `[ ] Pending` | (1) `updateCalibration` phải lưu calibration history với timestamp và version — không overwrite active calibration nếu không có version/audit trail; test verify history table có entry trước và sau update. (2) `getHistory` phải default `hours=24` nếu không truyền; tối đa `hours=720` (30 ngày) — test query với hours=721 trả 400. |
| S3-G2 | Implement `FlowController`: REST endpoints `/api/node/:id/flow?hours=24`, `/api/node/:id/calibration` (GET/PUT). | `[ ] Pending` | (1) Flow history endpoint phải dùng TimescaleDB time_bucket hoặc range query efficient, không load toàn bộ hypertable — verify với EXPLAIN ANALYZE. (2) JWT auth required; calibration PUT phải có audit log entry với user_id và timestamp — test without auth trả 401. |

## TRACK S3-H — Tuya Bridge Module

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_3.md` — TRACK H

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S3-H1 | Implement `TuyaBridgeService.measureOnDemand()`: `device.get()` → `parseDps()` → save `measurement_reading` → return DTO; không có polling loop. | `[ ] Pending` | (1) `TuyaBridgeService` phải KHÔNG có `setInterval`, `setTimeout` tự gọi lại, hoặc bất kỳ polling mechanism nào — kiểm tra bằng code review và `rg 'setInterval\|setInterval'` trong tuya-bridge module. (2) `parseDps()` phải handle Tuya DP index miss (device chưa có reading) gracefully — trả null cho missing DPs, không throw; test với empty DP response. |
| S3-H2 | Implement `TuyaBridgeController`: `POST /api/measurement/trigger` (auth required), `GET /api/measurement/latest`, `GET /api/measurement/history?limit=20`. | `[ ] Pending` | (1) `POST /api/measurement/trigger` phải idempotent trong timeout window (60s) — reject duplicate triggers trong window với 429; không tạo 2 concurrent Tuya connections. (2) History endpoint phải paginate với `limit` max 100; `trigger_type` filter optional — test với `trigger_type=SCHEDULED` trả 400 (invalid enum). |

## TRACK S3-I — MQTT Topics & WebSocket Events

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_3.md` — TRACK I & J

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S3-I1 | Implement MQTT subscribe routing: `aeroponics/gateway/+/heartbeat`, `aeroponics/node/+/telemetry`, `aeroponics/node/+/flow`, `aeroponics/node/+/ack`, `aeroponics/node/+/fault`. | `[ ] Pending` | (1) Wildcard `+` phải extract node_id và validate 1–4; message với node_id ngoài dải phải log warning và discard — test với node_id=5 phải discard. (2) `aeroponics/node/+/ack` handler phải call `PumpCommandService.handleRfAck` — test end-to-end: publish mock ACK → verify DB outcome update. |
| S3-I2 | Implement WebSocket EventsGateway: broadcast `node_telemetry`, `node_flow`, `pump_command_update`, `group_status`, `staleness_alert`; native WebSocket không Socket.IO. | `[ ] Pending` | (1) Native WebSocket phải dùng `@nestjs/websockets` với adapter `WsAdapter` không `SocketIoAdapter` — kiểm tra bằng import scan không có `socket.io`. (2) `staleness_alert` phải emit khi `NodeService` detect stale node; test WebSocket client nhận event sau node last_seen > STALE_THRESHOLD. |

## TRACK S3-J — REST API Completion & QA Rules

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_3.md` — TRACK K & L

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S3-J1 | Implement `/health` endpoint, serve `index.html` static, configure JWT auth module; verify no RelayModule import. | `[ ] Pending` | (1) `rg 'RelayModule\|relay_events\|relay_profiles\|/api/relay'` trong `aeroponics-backend/src/` phải trả 0 match — blocker để đánh Done. (2) `/health` phải check DB connection và return `{ status: 'ok', db: 'connected' }` hoặc 503; test với DB down phải return 503. |
| S3-J2 | E2E validation toàn bộ REST API table Sprint 3 (22 endpoints): status codes, auth, DTO validation và error messages. | `[ ] Pending` | (1) Mỗi endpoint phải có ít nhất: test success case (2xx), auth fail case (401), và invalid input case (400/409) — không accept endpoint không có test coverage. (2) QA rules S3-REUSE-01 đến S3-NO-RELAY-11 phải PASS trước khi mark S3-J2 Done; evidence ghi vào `WALKTHROUGH_LOG.md`. |

---

# 🔵 Sprint 4 — HTML Dashboard (Season + Group + Node + Flow + Command Lifecycle + On-demand Measurement)

> **Gate mở:** Sprint 3 PASS toàn bộ; WebSocket `/ws` hoạt động; TimescaleDB có data thật.
> **Output:** Single-file `aeroponics-ui/index.html` — 4 group cards, 4 node cards, season/treatment management, command lifecycle, Tuya on-demand.
> **Design System bắt buộc:** [`.codex/design-system/aeroponics-smart-farm/MASTER.md`](../../.codex/design-system/aeroponics-smart-farm/MASTER.md) — mọi component, màu sắc, font, icon đều phải tuân thủ nghiêm ngặt. Nếu có page-specific file trong `design-system/pages/`, rules đó override MASTER.
> **Nguồn phân rã:** [`sprint_4.md`](./sprint_4.md)

## TRACK S4-A — HTML Structure & Responsive Layout

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_4.md` — TRACK A | Design System: MASTER.md §Mobile-First Responsive Breakpoints

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S4-A1 | Tạo `aeroponics-ui/index.html` single-file với semantic sections: `header`, `#season-panel`, `#group-section`, `#node-section` (mobile-first single-column, tablet 2-col, desktop 4-col), `#treatment-panel`, `#measurement-panel`, modals. | `[ ] Pending` | (1) Mobile-first layout bắt buộc: tại 375px tất cả cards phải single-column stack không có horizontal overflow — kiểm tra bằng browser DevTools tại viewport 375px; không có `overflow-x: hidden` ẩn lỗi. (2) Responsive breakpoints phải tuân thủ MASTER.md: `sm=640px` 2-col flex, `md=768px` 2-col grid, `lg=1024px` 3-col, `xl=1440px+` 4-col — verify bằng resize test tại mỗi breakpoint; không dùng fixed pixel width cho card containers. |
| S4-A2 | Active season check: nếu không có ACTIVE season phải hiển thị CTA 'Tạo mùa vụ mới'; Configure NestJS `ServeStatic` serve `index.html`. | `[ ] Pending` | (1) Nếu không có ACTIVE season phải render CTA form có `name` input và submit button — không render dashboard trống hoặc crash; `S4-SEASON-11` blocker; test với API trả `null` active season. (2) `API_BASE = window.location.origin` dynamic — `rg 'localhost:3001\|127.0.0.1:3001'` trong index.html phải 0 match; test với backend chạy trên port khác. |

## TRACK S4-B — Design System Foundation (CSS & Typography)

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_4.md` — TRACK B | Design System: MASTER.md §Color Palette, §Typography, §Bio-Glassmorphism

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S4-B1 | Implement CSS Custom Properties theo MASTER.md color palette: `--color-background: #07130E`, `--color-surface: rgba(15,35,27,0.70)`, `--color-primary: #10B981`, `--color-secondary: #34D399`, `--color-accent-amber: #F59E0B`, `--color-accent-indigo: #818CF8`, `--color-danger: #EF4444`, `--color-text: #F0FDF4`, `--color-text-muted: #86EFAC`, `--color-text-subtle: #4B7260`, `--color-border: rgba(52,211,153,0.20)`. | `[ ] Pending` | (1) Tất cả 11 CSS variables bắt buộc phải có trong `:root {}`; không có hex/rgba color literal nào ngoài `:root {}` block — `rg '#[0-9A-Fa-f]{3,8}\|rgba(' index.html` phải chỉ match trong `:root` section. (2) Background phải dùng `var(--color-background)` = `#07130E` (Deep Forest Midnight OLED) — verify bằng computed style `body { background-color }` tại 375px viewport; không dùng `#000`, `#111`, hay `#1a1a1a` thay thế. |
| S4-B2 | Implement Typography System: `@import` Outfit + JetBrains Mono từ Google Fonts CDN; set `--font-sans: 'Outfit'`, `--font-mono: 'JetBrains Mono'`; apply mobile-first type scale (h1=1.75rem/700, h2=1.35rem/600, h3=1.125rem/600, `.metric-value` mono 1.75rem/700, `.timer-countdown` mono 2.25rem/700 tabular-nums). | `[ ] Pending` | (1) Font import phải dùng Google Fonts URL chính xác theo MASTER.md: `family=JetBrains+Mono:wght@400;500;600;700&family=Outfit:wght@300;400;500;600;700;800&display=swap` — kiểm tra bằng DevTools Network tab font load thành công; không dùng Inter, Roboto, hay system font thay thế. (2) Telemetry values (`flow_lpm`, `deliveredVolumeL`, timer countdown) phải dùng `.metric-value` hoặc `.timer-countdown` với `font-family: var(--font-mono)` và `font-variant-numeric: tabular-nums` — test với số thay đổi không gây layout shift. |
| S4-B3 | Implement `.glass-card` Bio-Glassmorphism component và `.relay-glow-active` animation theo MASTER.md spec; implement `.outcome-badge` color map per outcome enum; implement `.staleness-dot` pulse animation. | `[ ] Pending` | (1) `.glass-card` phải có: `background: rgba(15,35,27,0.70)`, `backdrop-filter: blur(16px)`, `border: 1px solid rgba(52,211,153,0.20)`, `border-radius: 16px`, `box-shadow: 0 8px 32px 0 rgba(0,0,0,0.45)` — verify bằng computed CSS; hover state phải tăng `border-color` lên `rgba(52,211,153,0.45)` và box-shadow green glow. (2) Node card khi pump ACTIVE (FLOW_CONFIRMED) phải có class `.relay-glow-active` với `@keyframes pulse-emerald` (0%/100%: shadow 20px rgba(16,185,129,0.25); 50%: shadow 35px 4px rgba(16,185,129,0.45)) — test inject FLOW_CONFIRMED state và verify animation chạy. |
| S4-B4 | Enforce MASTER.md Anti-patterns: zero emoji icons, no desktop-only layout, no layout shifting on state change, WCAG AAA contrast ≥7:1 cho tất cả text. | `[ ] Pending` | (1) Zero emoji: `rg '[\u{1F300}-\u{1FAFF}]' --encoding utf-8 aeroponics-ui/index.html` phải 0 match — BLOCKER per MASTER.md §Iconography "Zero Emojis"; tất cả icons phải là Lucide SVG inlined (Droplets, Timer, Activity, Sun, Moon, Zap, Thermometer, ShieldAlert, RefreshCw, Sliders, CheckCircle2, AlertTriangle). (2) Text contrast: `--color-text: #F0FDF4` trên `--color-background: #07130E` phải ≥7:1 — verify bằng WCAG contrast checker; `--color-text-muted: #86EFAC` trên surface phải ≥4.5:1; không dùng `--color-text-subtle` (#4B7260) cho body text. |

## TRACK S4-C — Touch Ergonomics & Micro-Interactions

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_4.md` — TRACK C | Design System: MASTER.md §Mobile Touch Ergonomics

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S4-C1 | Enforce minimum touch target sizes: primary buttons (Manual Override, End Season, Đo ngay) ≥48×48px; secondary actions (tabs, badges) ≥44×44px; clearance ≥8px giữa adjacent interactive elements. | `[ ] Pending` | (1) Đo `offsetHeight`/`offsetWidth` của mọi interactive element tại 375px viewport — không có element nào < 44px; primary CTA buttons phải ≥48px; verify bằng DevTools element inspector. (2) Thumb-zone optimization: primary controls (Manual Override, Emergency Stop nếu có, Zone Switcher) phải nằm trong lower 60% viewport height tại 375px — kiểm tra bằng position check trong mobile view. |
| S4-C2 | Implement micro-interactions: `active:scale(0.95)` trên tất cả buttons và clickable cards; state transition `150ms–250ms cubic-bezier(0.4,0,0.2,1)`; zero layout shift khi state change (Spraying ↔ Cooldown). | `[ ] Pending` | (1) Tất cả `<button>` và `.glass-card[role=button]` phải có CSS: `transition: transform 150ms cubic-bezier(0.4,0,0.2,1)` và `:active { transform: scale(0.95) }` — kiểm tra bằng DevTools CSS inspect; không có button nào thiếu `cursor: pointer`. (2) Card state transition (FLOW_CONFIRMED → COOLDOWN → OFF) phải giữ nguyên card dimensions — test inject state changes và measure card height trước/sau; không dùng conditional padding/margin làm thay đổi layout. |

## TRACK S4-D — JavaScript Modules & WebSocket

*Nguồn phân rã:* `.ai/planning/aeroponics-lean/sprint_4.md` — TRACK C | Design System: MASTER.md §No Page Reloads, §No Tiny Touch Targets

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S4-D1 | Implement Constants: `GROUP_LABELS` (1–4), `NODE_LABELS` (1–4), `OUTCOME_CONFIG` per outcome với màu từ MASTER.md palette, `STALE_THRESHOLD_MS=120000`. | `[ ] Pending` | (1) `OUTCOME_CONFIG` màu sắc phải dùng CSS variables từ MASTER.md palette: `FLOW_CONFIRMED` → `var(--color-primary)` (#10B981), `FAULT_*` → `var(--color-danger)` (#EF4444), `RF_ACKED` → `var(--color-accent-indigo)` (#818CF8), `TIMEOUT` → `var(--color-accent-amber)` (#F59E0B), `PENDING` → `var(--color-text-subtle)` (#4B7260) — không hardcode hex trong JS. (2) Constants phải là `Object.freeze({})`; test gán mới vào frozen object throws TypeError. |
| S4-D2 | Implement `WebSocketManager` với exponential backoff và WS disconnect banner theo MASTER.md §No Page Reloads. | `[ ] Pending` | (1) Backoff bounded: max delay 30s, sau 10 failures phải show error banner rõ ràng — không reconnect vô hạn im lặng; banner phải dùng `--color-danger` (#EF4444) background. (2) Live telemetry updates phải qua WebSocket không trigger `window.location.reload()` hoặc DOM reinitialize — test WS event handler không gọi reload. |
| S4-D3 | Implement Group Card Renderer: phase DAY (Sun icon, `--color-accent-amber`) / NIGHT (Moon icon, `--color-accent-indigo`); phase countdown `.timer-countdown` (JetBrains Mono tabular-nums); ACTIVE/UNASSIGNED badge. | `[ ] Pending` | (1) Phase indicator phải dùng Lucide SVG icon: DAY = `<Sun>` (24×24px, stroke `--color-accent-amber`), NIGHT = `<Moon>` (24×24px, stroke `--color-accent-indigo`) — không dùng emoji ☀️🌙; `rg '☀\|🌙' index.html` phải 0 match. (2) Countdown timer phải dùng class `.timer-countdown` (JetBrains Mono, 2.25rem, tabular-nums); update mỗi giây client-side không tạo duplicate intervals — test multiple WS reconnect không stack intervals. |
| S4-D4 | Implement Node Card Renderer: staleness dot (green/amber/red với pulse animation khi STALE); outcome badge per OUTCOME_CONFIG; pump state glow (`.relay-glow-active` khi FLOW_CONFIRMED); flow metric (`.metric-value`, JetBrains Mono). | `[ ] Pending` | (1) Staleness dot: `last_seen < 60s` → `--color-primary` (#10B981), `60s–120s` → `--color-accent-amber` (#F59E0B), `≥120s` → `--color-danger` (#EF4444) với `@keyframes pulse` animation — màu phải từ CSS variables, không hardcode; test boundary 59s/60s/119s/120s. (2) `flow_lpm` và `deliveredVolumeL` phải dùng class `.metric-value` (font-mono, 1.75rem, 700) — không dùng generic `<p>` plain text; test font-family computed = JetBrains Mono. |
| S4-D5 | Implement REST API Helpers (`api.get/put/post/delete`), Season Panel, Treatment Panel, Command Log modal, On-demand Measurement modal. | `[ ] Pending` | (1) Season Panel: 'End Season' button ≥48×48px, có confirmation modal trước khi gọi API; 'Tạo mùa vụ mới' CTA nếu không có ACTIVE season — button phải có `cursor: pointer` và `active:scale(0.95)`. (2) On-demand 'Đo ngay' button: gọi `POST /api/measurement/trigger` không auto-poll — `rg 'setInterval.*trigger\|trigger.*setInterval' index.html` phải 0 match; result modal hiển thị 7 values (pH, EC, TDS, Temp, Salinity, ORP, Turbidity) với `.metric-value` style. |
| S4-D6 | Implement Initialization: load all data → render → connect WebSocket → safe-area iOS padding. | `[ ] Pending` | (1) `Promise.allSettled([loadSeason(), loadGroups(), loadNodes(), loadTreatments(), loadMeasurement()])` — lỗi 1 endpoint không block phần còn lại; test mock một endpoint fail không blank page. (2) iOS safe area: bottom sticky elements (nếu có nav bar) phải dùng `padding-bottom: env(safe-area-inset-bottom)` — kiểm tra bằng iOS Safari simulator không có content bị che bởi home indicator. |

## TRACK S4-E — Iconography (Lucide SVG — Zero Emoji)

*Nguồn phân rã:* Design System MASTER.md §Iconography & Visual Asset Standards

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S4-E1 | Inline tất cả Lucide SVG icons cần thiết: `Droplets` (flow/pump), `Timer` (countdown/schedule), `Activity` (telemetry/status), `Sun` (DAY phase), `Moon` (NIGHT phase), `Zap` (power/command), `Thermometer` (measurement), `ShieldAlert` (fault/alarm), `RefreshCw` (reconnect/reload), `Sliders` (calibration/config), `CheckCircle2` (FLOW_CONFIRMED), `AlertTriangle` (fault/warning). | `[ ] Pending` | (1) Mỗi SVG icon phải có `width="24" height="24" viewBox="0 0 24 24"` (hoặc 20×20 cho badges); stroke color phải từ CSS variable không hardcode hex — kiểm tra bằng `grep -o 'stroke="#[^"]*"' index.html` phải 0 match (dùng `stroke="currentColor"` hoặc CSS variable). (2) `rg '💧\|⚙️\|🌱\|🔴\|🟢\|🟡\|[\u{1F300}-\u{1FAFF}]' --encoding utf-8 index.html` phải 0 match — zero emoji absolute blocker per MASTER.md. |

## TRACK S4-F — Pre-Delivery Quality Checklist & QA

*Nguồn phân rã:* Design System MASTER.md §Pre-Delivery Quality Checklist

| Task ID | Mô tả Task | Status | Note hoặc các thông tin cần thiết để thực hiện chuẩn chỉnh |
| :--- | :--- | :--- | :--- |
| S4-F1 | Mobile-First Verification: test tại 5 breakpoints 375px, 640px, 768px, 1024px, 1440px; no horizontal overflow, no layout shift, touch targets ≥44px. | `[ ] Pending` | (1) Tại mỗi breakpoint phải chụp screenshot hoặc record evidence: không có `overflow-x scroll`, cards không wrap awkwardly, touch targets ≥44px — evidence ghi vào `WALKTHROUGH_LOG.md`. (2) Group/Node cards tại 375px phải single-column full-width; tại 1440px phải 4-column — test column count bằng CSS computed `grid-template-columns` value. |
| S4-F2 | Verify toàn bộ MASTER.md Pre-Delivery Checklist: Outfit/JetBrains Mono fonts load, cursor-pointer + active:scale-95 trên tất cả interactive, zero emoji, WCAG AAA contrast, safe-area iOS. | `[ ] Pending` | (1) WCAG AAA contrast audit: dùng browser a11y tools hoặc axe-core để scan — `--color-text` (#F0FDF4) trên `--color-background` (#07130E) phải ≥7:1; bất kỳ fail nào phải fix trước Done. (2) Font load verify: `document.fonts.check('700 1rem Outfit')` và `document.fonts.check('700 1rem "JetBrains Mono"')` phải return `true` trong DevTools Console; nếu Google Fonts fail (offline) phải fallback sang `-apple-system, sans-serif`. |
| S4-F3 | Integration test end-to-end với backend thật: WS events update UI realtime không reload; command lifecycle log; staleness detection; Tuya trigger. | `[ ] Pending` | (1) Publish mock MQTT telemetry → verify WebSocket event → verify node card update trong 2s — không có `window.location.reload()` trong MQTT/WS handler; test evidence screenshot/video. (2) Tuya trigger: click 'Đo ngay' (Thermometer icon, không emoji) → `POST /api/measurement/trigger` → modal hiển thị 7 values với `.metric-value` JetBrains Mono — evidence ghi vào `WALKTHROUGH_LOG.md`. |

---

## 📋 Quy ước trạng thái (Sprint 2–4)

| Status | Ý nghĩa |
|---|---|
| `[ ] Pending` | Chưa bắt đầu; không có source/evidence thực hiện. |
| `[ ] In Progress` | Execution Agent đang thực hiện; chưa đủ evidence để review. |
| `[ ] QA Review` | Implementation/evidence hoàn tất; chờ Senior Solution Architect review độc lập. |
| `[x] Done` | PASS toàn bộ gate áp dụng; evidence ghi vào `WALKTHROUGH_LOG.md`; được duyệt nghiêm ngặt. |

---

## 🛡️ QA Gateways — Sprint 2 (checklist trước khi mở Sprint 3)

| Rule ID | PASS khi | Severity |
|---|---|---|
| **S2-RF-01** | Frame validation/CRC/duplicate sequence/ACK retry timeout có host regression tests; không duplicate actuation. | 🔴 BLOCKER |
| **S2-GROUP-02** | Treatment dynamic/versioned; `UNASSIGNED` group không chạy; 4 node assignment dynamic không trùng active group. | 🔴 BLOCKER |
| **S2-PUMP-03** | Desired/reported/pump feedback khác biệt rõ; command ON không success chỉ vì RF ACK. | 🔴 BLOCKER |
| **S2-FLOW-04** | Flow L/min/volume/calibration/status đúng; no-flow, stuck-flow, sensor fault và >6 L/min xử lý an toàn. | 🔴 BLOCKER |
| **S2-SAFE-05** | RF timeout/stale/RTC invalid/power-cycle fail-safe theo policy approved, bounded retry, audited reason. | 🔴 BLOCKER |
| **S2-MQTT-06** | LWT QoS1 retained, credentials không hard-code, ACL least privilege, MQTT loss không phá scheduler local. | 🔴 BLOCKER |
| **S2-4NODE-07** | 4-node simulator + hardware bench evidence thỏa threshold latency/loss/freshness được phê duyệt. | 🔴 BLOCKER |
| **S2-QUALITY-08** | `pio test -e native`, RF integration tests và `pio run -e esp32-s3-devkitc-1` PASS. | 🔴 BLOCKER |

## 🛡️ QA Gateways — Sprint 3 (checklist trước khi mở Sprint 4)

| Rule ID | PASS khi | Severity |
|---|---|---|
| **S3-REUSE-01** | Boilerplate database/config/mqtt/auth từ mushroom-cp được tái sử dụng đúng pattern. | 🟠 CRITICAL |
| **S3-NO-INFLUX-02** | Không có `@influxdata/influxdb-client` hay `InfluxModule` nào trong source. | 🔴 BLOCKER |
| **S3-DB-03** | `synchronize: false` trong TypeORM config production. | 🔴 BLOCKER |
| **S3-TUYA-ON-DEMAND-04** | `TuyaBridgeService` KHÔNG có `setInterval` polling loop. | 🔴 BLOCKER |
| **S3-MQTT-05** | `onMessage` catch tất cả exceptions, không có unhandled rejection. | 🔴 BLOCKER |
| **S3-WS-NATIVE-06** | Native WebSocket, không Socket.IO. | 🔴 BLOCKER |
| **S3-DTO-07** | class-validator cho tất cả request bodies. | 🟠 CRITICAL |
| **S3-DEADMAN-08** | `PumpCommandService` implement deadman/lease cancel trong `onModuleDestroy`. | 🔴 BLOCKER |
| **S3-ANTIREPLAY-09** | MQTT command handler reject duplicate `rf_seq` trong 60s window. | 🔴 BLOCKER |
| **S3-STALENESS-10** | `NodeService` emit `staleness_alert` nếu `last_seen_at` > `STALE_THRESHOLD_MS`. | 🔴 BLOCKER |
| **S3-NO-RELAY-11** | Không có `RelayModule`, `relay_events`, `relay_profiles`, `/api/relay/*` trong production. | 🔴 BLOCKER |

## 🛡️ QA Gateways — Sprint 4 (checklist production ready — Design System Enforced)

| Rule ID | PASS khi | Severity |
|---|---|---|
| **S4-WS-01** | Native WebSocket (không Socket.IO); live updates không trigger page reload. | 🔴 BLOCKER |
| **S4-API-02** | `API_BASE = window.location.origin` dynamic; không hardcode `localhost:3001`. | 🔴 BLOCKER |
| **S4-NULL-03** | Null-safe rendering — không crash khi API trả null field; missing values hiển thị `'—'`. | 🔴 BLOCKER |
| **S4-NO-RELAY-04** | `rg '/api/relay\|relay_update\|relay card'` trong index.html = 0 match. | 🔴 BLOCKER |
| **S4-OUTCOME-05** | Node cards show `.outcome-badge` với màu per OUTCOME_CONFIG (MASTER.md palette), không chỉ ON/OFF text. | 🔴 BLOCKER |
| **S4-STALENESS-06** | Staleness dot: `<60s`=`--color-primary` (#10B981), `60–120s`=`--color-accent-amber` (#F59E0B), `≥120s`=`--color-danger` (#EF4444); pulse animation khi STALE. | 🔴 BLOCKER |
| **S4-SEASON-07** | Dashboard show active season; nếu không có thì show 'Tạo mùa vụ mới' CTA (không render blank dashboard). | 🔴 BLOCKER |
| **S4-ON-DEMAND-08** | 'Đo ngay' (Thermometer Lucide icon) call `POST /api/measurement/trigger`, không có `setInterval` polling. | 🔴 BLOCKER |
| **S4-DS-FONT-09** | `Outfit` cho UI text; `JetBrains Mono` với `tabular-nums` cho metric values và timers — verify `document.fonts.check()` PASS. | 🔴 BLOCKER |
| **S4-DS-COLOR-10** | 11 CSS variables MASTER.md đều có trong `:root {}`; không có hex/rgba literal ngoài `:root` block; `--color-background: #07130E`. | 🔴 BLOCKER |
| **S4-DS-GLASS-11** | `.glass-card`: `backdrop-filter: blur(16px)`, `border-radius: 16px`, emerald border glow; pump ACTIVE có `.relay-glow-active` `@keyframes pulse-emerald`. | 🔴 BLOCKER |
| **S4-DS-ICON-12** | Zero emoji trong toàn bộ UI — `rg '[\u{1F300}-\u{1FAFF}]'` = 0 match; tất cả icons là Lucide SVG 24×24px với `stroke="currentColor"`. | 🔴 BLOCKER |
| **S4-DS-TOUCH-13** | Primary buttons ≥48×48px; secondary ≥44×44px; clearance ≥8px; `cursor: pointer` + `active: scale(0.95)` trên mọi interactive element. | 🔴 BLOCKER |
| **S4-DS-CONTRAST-14** | WCAG AAA contrast ≥7:1 cho `--color-text` trên `--color-background`; verify bằng axe-core hoặc browser a11y audit. | 🔴 BLOCKER |
| **S4-DS-MOBILE-15** | Mobile-first verify tại 375px, 640px, 768px, 1024px, 1440px — no horizontal overflow, no layout shift on state change. | 🔴 BLOCKER |
| **S4-BANNER-16** | WS disconnect banner dùng `--color-danger`; auto-hide khi reconnect; backoff bounded max 30s. | 🟠 CRITICAL |
| **S4-CASE-17** | camelCase field names từ NestJS được handle đúng trong JS renderers. | 🟠 CRITICAL |
| **S4-CDN-18** | CDN resources (Google Fonts, Chart.js nếu dùng) có version pinned và `crossorigin="anonymous"`. | 🟠 CRITICAL |

---

*Progress Tracker cập nhật ngày 2026-09-12: Sprint 1.5 PASS toàn bộ gate; Sprint 2 mở — ma trận thực thi Sprint 2/3/4 đã được phân rã đầy đủ theo baseline ESP32-S3 + 4 MEGA8 autonomous schedule nodes. Sprint 4 enforce Design System `.codex/design-system/aeroponics-smart-farm/MASTER.md` (OLED Dark Glassmorphism, Outfit + JetBrains Mono, Lucide SVG, Mobile-First 375px–1440px, WCAG AAA).*

