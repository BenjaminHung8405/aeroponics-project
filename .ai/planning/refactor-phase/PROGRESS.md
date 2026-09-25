# PROGRESS — Refactor Phase Tracking

> Tài liệu theo dõi tiến độ thực thi (Progress Register) cho kế hoạch Refactor được khởi tạo bởi **Gemini**. Mọi Agent thực thi phải cập nhật Status tại đây sau mỗi tác vụ theo đúng quy ước markdown checkbox (Pending / In Progress / QA Review / Done). Không được sửa cột Note trừ khi có thay đổi hướng dẫn kỹ thuật được phê duyệt ở `.ai/planning/refactor-phase/README.md`.

---

## 1. Started

| Trường | Giá trị |
|---|---|
| **Thời điểm khởi tạo** | `2026-09-24T12:19:19Z` (UTC) |
| **Execution Agent** | **Gemini** |
| **Vai trò** | Kỹ sư thực thi (Execution Agent) theo kế hoạch Sprint |
| **Baseline Agent** | Đã khởi tạo master plan `README.md` ngày `2026-09-24` (không thay đổi trong phạm vi refactor) |

---

## 2. Reference Plan

| Trường | Giá trị |
|---|---|
| **Thư mục kế hoạch** | `.ai/planning/refactor-phase/` |
| **Master Planning Context** | `.ai/planning/refactor-phase/README.md` (Single Source of Truth — bắt buộc đọc trước khi bắt đầu bất kỳ Sprint nào) |
| **Sprint hiện tại (đang tham chiếu)** | `.ai/planning/refactor-phase/sprint_2.md` — **Sprint 2: Gateway Virtual FSM & Safety Timers (ESP32)** |
| **Thứ tự Sprint roadmap** | `Sprint 1 (Codec ESP32)` → `Sprint 2 (Virtual FSM & Safety Timers)` → `Sprint 3 (Backend NestJS & TimescaleDB)` → `Sprint 4 (Dashboard Next.js & Nginx)` |
| **Golden Baseline tham chiếu Sprint 2** | `docs/STATE_MACHINE_MATRIX.md` §3–§5, `docs/interface-wire-contract.md` §8–§9 |
| **Phụ thuộc Sprint 1** | Sprint 1 PASS (RF Wire Codec) |
| **Output bàn giao Sprint 2** | Virtual FSM `(MacroState, EvidenceStage)` vận hành ổn định 4 trạm, Deadman Lease Timer hoạt động, `pending_command_table` TTL cleanup đúng hạn, 273/273 tests PASS |

---

## 3. Addition Plan (Yêu cầu phát sinh)

**Chưa có yêu cầu phát sinh nào được bổ sung.** Mọi yêu cầu mới vượt phạm vi Golden Baseline hoặc Sprint hiện tại phải được ghi vào bảng dưới đây trước khi triển khai, và phải được phê duyệt tại `.ai/planning/refactor-phase/README.md`.

| ID | Yêu cầu phát sinh | Trạng thái | Ghi chú |
|---|---|---|---|
| (trống) | Chưa có | — | — |

---

## 4. Track Status — Sprint 2

> Quy ước Status (bắt buộc, không thay đổi ký hiệu):
> - `[ ] Pending` — Task chưa chạm vào.
> - `[ ] In Progress` — Execution Agent đang viết code.
> - `[ ] QA Review` — Code đã viết xong, đang chờ rà soát chất lượng.
> - `[x] Done` — Đã qua vòng review nghiêm ngặt và được duyệt.

> **Chỉ thị kỹ thuật bắt buộc (đóng vai Note):** Mỗi Task Note phải tuân theo các quy tắc trong `.ai/planning/refactor-phase/README.md` (Clean Architecture, Dependency Inversion, Strangler Fig, Fail‑safe, Zero‑hardcode credential, Non‑retained transactional topics, `synchronize: false`, v.v.) và các rule chuẩn sprint tương ứng (S2‑FSM‑01…S2‑TABLE‑06, v.v.).

### 4.1 TRACK A — Virtual FSM Core (Data Structures & Transitions)

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| A1 | `aeroponics-firmware/include/node_fsm.h` — FSM header definitions (enum MacroState, EvidenceStage, NodeFsmState, PendingCommandEntry) | [ ] QA Review | Áp dụng design pattern Virtual FSM; enum MUST follow SCREAMING_SNAKE_CASE; KHÔNG hardcode nodeId, KHÔNG dùng malloc/new trong header; invariant: `sum(frame) & 0xFF == 0` cho checksum Zero‑Sum tuân theo contract; RAM ≤ 20 KB. |
| A2 | `aeroponics-firmware/src/node_fsm.cpp` — FSM transition logic (transitionMacroState, advanceEvidenceStage, leaseTick, canScheduleOn) | [ ] QA Review | Mỗi transition phải có guard check (FAULT_LATCH → BOOT_OFF only via preflight, OVERRIDE_RUN → SCHEDULE_COOLDOWN only via lease expiry); KHÔNG bao giờ return void quan trọng; static_assert cho bounds `RUN_LEASE_MIN_MS ≥ 1000`, `RUN_LEASE_MAX_MS ≤ 300000`; leaseTick trả bool; canScheduleOn so sánh `now_ms ≥ cooldown_boundary_ms`. |

### 4.2 TRACK B — Safety Timer Constants & Guard Integration

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| B1 | `aeroponics-firmware/include/config.h` — Thêm safety timer constants (T_FLOW_SETTLE_MS, T_COOLDOWN_MIN_MS, T_POLL_0x0E_MS, RUN_LEASE_MIN/MAX, COMMAND_TABLE_TTL, v.v.) | [ ] QA Review | Thêm toàn bộ constant theo sprint_2.md Task B‑1; dùng `SCREAMING_SNAKE_CASE`; bóc `static_assert` cho `RF_UART_RING_BUFFER_SIZE ≥ 256` và `RF_UART_RX_TASK_PRIORITY > MQTT_TASK_PRIORITY`; KHÔNG hardcode giá trị vào code logic. |

### 4.3 TRACK C — Command Correlation Table

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| C1 | `aeroponics-firmware/src/node_fsm.cpp` — Implement PendingCommandTable (insert, find, resolve, cleanup) | [ ] Pending | Bounded static array `entries_[16]` (COMMAND_TABLE_MAX_ENTRIES = 16); TTL cleanup mỗi 2000ms (`COMMAND_TABLE_TTL_MS`); KHÔNG dùng `new`/`malloc` (sử dụng static array); RAM invariant: `16 × sizeof(PendingCommandEntry) ≤ 1.2 KB << 20 KB`; fail‑closed: `find` trả `nullptr` nếu entry unresolved hoặc đã hết TTL. |

### 4.4 TRACK D — FSM Integration into Main Loop

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| D1 | `aeroponics-firmware/src/main.cpp` — Replace LegacyOverride với NodeFsmState và PendingCommandTable | [ ] Pending | Thay `static LegacyOverride g_legacy_overrides[]` bằng `static NodeFsmState g_node_fsm[]` và `static PendingCommandTable g_pending_commands;`; KHÔNG thay đổi logic RF transaction; duy trì invariants: `g_node_fsm[id].node_id ∈ [4..7]`. |
| D2 | `aeroponics-firmware/src/main.cpp` — Implement `serviceFsmTick(uint32_t current_ms)` | [ ] Pending | Mỗi tick: (1) `leaseTick` → nếu hết hạn → OFF transaction, transition `SCHEDULE_COOLDOWN`, publish `LEASE_EXPIRED_SAFE_OFF`; (2) flow settle timeout check (≥ `T_FLOW_SETTLE_MS` → `FAULT_LATCH`); (3) `g_pending_commands.cleanup(current_ms)`; KHÔNG block, KHÔNG malloc/new trong tick. |
| D3 | `aeroponics-firmware/src/main.cpp` — Implement `servicePollTelemetry(uint32_t current_ms)` | [ ] Poll opcode `0x0E` mỗi 1s (`T_POLL_0x0E_MS`); KHÔNG chạy trên Core 0 cùng Wi‑Fi driver; parse 8‑byte RAM burst, gọi `updateNodeEvidenceFromTelemetry`; KHÔNG blocking call, KHÔNH `malloc`; dùng `vTaskDelay(pdMS_TO_TICKS(20))` giữa các node. |

### 4.5 TRACK E — MQTT Integration & Lifecycle Events

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| E1 | `aeroponics-firmware/src/mqtt_client.cpp` — Publish lifecycle events (hàm `publishLifecycleEvent`) | [ ] Pending | Topic format: `aeroponics/v1/node/{nodeId}/event`; `retain: false` bắt buộc cho mọi topic transactional (`command`, `ack`, `event`, `telemetry`); chỉ `status`/`LWT` retain `true`; KHÔNG hardcode credential, dùng `Logger` NestJS; JSON payload theo interface‑wire‑contract §8. |
| E2 | `aeroponics-firmware/src/main.cpp` — Update `executeAguPump` với FSM integration | [ ] Pending | Gọi `g_pending_commands.insert(node_id, command_id)` sau ACKED; `advanceEvidenceStage` từ `COMMAND_DISPATCHED` → `RF_ACKNOWLEDGED`; nếu `turn_on` → set `lease_active`, `lease_expiry_ms = millis() + run_lease_ms`; publish `RF_ACKED` lifecycle event; KHÔNG publish `RUNNING` khi evidence stage < `FLOW_CONFIRMED`. |

### 4.6 TRACK F — Unit Tests

| Task ID | Mô tả Task | Status | Note (chỉ thị kỹ thuật bắt buộc) |
|---|---|---|---|
| F1 | `aeroponics-firmware/test/test_fsm/test_fsm.cpp` — FSM transition tests (BOOT_OFF↔SCHEDULE_SPRAY, FAULT_LATCH guards, lease expiry, cooldown prevention, insert/find/cleanup, TTL) | [ ] Pending | Test mọi macro state transition, evidence pipeline progression, leaseTick, canScheduleOn, PendingCommandTable insert/find/resolve/cleanup, TTL cleanup; KHÔNG sửa tổng test count 273/273; test fail‑closed (checksum sai → return false); tất cả test phải PASS cùng baseline 273 tests. |

---

*File PROGRESS.md đã được khởi tạo tại `.ai/planning/refactor-phase/PROGRESS.md` kèm theo cấu trúc định dạng Markdown chuẩn, Track A‑F từ Sprint 2 đã được chuyển hóa thành các bảng 4 cột (Task ID / Mô tả Task / Status / Note chỉ thị kỹ thuật bắt buộc) và tất cả Status khởi tạo là `[ ] Pending`.*
