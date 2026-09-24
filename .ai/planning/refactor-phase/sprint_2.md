# Sprint 2: Gateway Virtual FSM & Safety Timers (ESP32)

> **Phụ thuộc:** Sprint 1 PASS (RF Wire Codec + UART Core 1 Isolation).
> **Output bàn giao:** Virtual FSM `(MacroState, EvidenceStage)` vận hành ổn định 4 trạm, Deadman Lease Timer hoạt động, `pending_command_table` TTL cleanup đúng hạn, 273/273 tests PASS.
> **Golden Baseline tham chiếu:** [`docs/STATE_MACHINE_MATRIX.md`](../../docs/STATE_MACHINE_MATRIX.md) §3–§5, [`docs/interface-wire-contract.md`](../../docs/interface-wire-contract.md) §8–§9.

---

## 1. PHẠM VI & MỤC TIÊU

### 1.1 Modules / Components bị tác động trực tiếp

| Module / File | Hành động | Mô tả |
|---|---|---|
| `aeroponics-firmware/include/node_fsm.h` | **Tạo mới** | Header Virtual FSM: enum `MacroState`, `EvidenceStage`, struct `NodeFsmState`, `PendingCommandEntry` |
| `aeroponics-firmware/src/node_fsm.cpp` | **Tạo mới** | Implement: transition(), evaluateGuards(), leaseTick(), commandTableCleanup() |
| `aeroponics-firmware/include/config.h` | **Sửa** | Thêm safety timer constants: `T_FLOW_SETTLE_MS`, `T_COOLDOWN_MIN_MS`, `T_POLL_0x0E_MS`, `COMMAND_TABLE_TTL_MS`, `COMMAND_TABLE_MAX_ENTRIES` |
| `aeroponics-firmware/src/main.cpp` | **Sửa** | Tích hợp FSM tick vào `loop()`, thay thế `LegacyOverride` bằng `NodeFsmState`, cập nhật `serviceScheduleTick()`, `serviceLegacyOverrideExpiry()` |
| `aeroponics-firmware/include/node_registry.h` | **Sửa** | Thêm `getFsmState()`/`setFsmState()` accessor, thêm `evidenceStage` vào `NodeState` |
| `aeroponics-firmware/src/node_registry.cpp` | **Sửa** | Thêm `getFsmState()`, `transitionMacroState()`, `advanceEvidenceStage()` |
| `aeroponics-firmware/src/agu_legacy_rf_host.cpp` | **Sửa** | Gọi `readRamBurst()` trong polling tick, cập nhật evidence pipeline |
| `aeroponics-firmware/src/mqtt_client.cpp` | **Sửa** | Publish `FLOW_CONFIRMED` event khi evidence stage đạt `FLOW_CONFIRMED` |
| `aeroponics-firmware/test/test_fsm/test_fsm.cpp` | **Tạo mới** | Unit test cho FSM transitions, evidence pipeline, lease expiry, command table TTL |

### 1.2 Mục tiêu cụ thể Sprint 2

- [ ] 6 Macro States hiện thực: `BOOT_OFF`, `SCHEDULE_SPRAY`, `SCHEDULE_COOLDOWN`, `OVERRIDE_RUN`, `OVERRIDE_HOLD_OFF`, `FAULT_LATCH`.
- [ ] Evidence Pipeline 5 stage: `NONE → COMMAND_DISPATCHED → RF_ACKNOWLEDGED → GATE_FEEDBACK_ON → CURRENT_DETECTED → FLOW_CONFIRMED`.
- [ ] Tuple `(MacroState, EvidenceStage)` quản lý độc lập cho 4 trạm.
- [ ] `run_lease_ms` trong khoảng `[1000, 300000]`, sau khi lease hết hạn → `OVERRIDE_RUN` → `SCHEDULE_COOLDOWN`, KHÔNG tự chuyển sang ON mới.
- [ ] `T_flow_settle = 2500 ms`: Sau khi RF ACK, chờ tối đa 2500ms để evidence flow; nếu không có → `FAULT_LATCH`.
- [ ] `T_cooldown_min = 60 s`: Sau khi OFC expired hoặc scheduled OFF, phải chờ tối thiểu 60s trước khi ON lại (tránh water shock).
- [ ] `T_poll = 1000 ms`: Opcode `0x0E` polling mỗi node mỗi 1s để cập nhật evidence.
- [ ] `pending_command_table` lưu `rf_command_id (uint32)` ↔ `mqtt_command_id (string)`, TTL cleanup 2000ms sau khi command terminal.
- [ ] Không cấp phát heap động (`new`/`malloc`) trong vòng lặp FSM tick.
- [ ] Toàn bộ 273/273 tests PASS.

---

## 2. KIẾN TRÚC & LUỒNG DỮ LIỆU

### 2.1 Sơ đồ Dependency của các Module Sprint 2

```text
main.cpp
  │
  ├──▶ config.h                 (Thêm: timer constants, table size)
  │
  ├──▶ node_fsm.h/cpp           [TẠO MỚI]
  │     ├── NodeFsmState        — Struct FSM state per node
  │     ├── PendingCommandEntry — Struct command mapping table
  │     ├── transitionMacroState() — Core FSM transition logic
  │     ├── advanceEvidence()   — Evidence pipeline progression
  │     ├── leaseTick()         — Deadman lease countdown
  │     ├── commandTableInsert/Tick/Cleanup — Correlation table
  │     └── evaluateGuards()    — Pre-condition checks
  │
  ├──▶ node_registry.h/cpp      (Sửa: thêm FSM accessor methods)
  │     └── getFsmState(), setFsmState(), transitionMacroState()
  │
  ├──▶ agu_legacy_rf_host.cpp   (Sửa: tích hợp evidence pipeline after transact())
  │
  ├──▶ mqtt_client.cpp          (Sửa: publish FLOW_CONFIRMED event)
  │
  └──▶ test_fsm/test_fsm.cpp   [TẠO MỚI]
        └── Unit test cho mọi macro state transition, evidence pipeline, lease, TTL
```

### 2.2 Luồng FSM Transition (6 Macro States)

```text
┌──────────────────────────────────────────────────────────────────────────┐
│                        VIRTUAL FSM — PER NODE (×4)                       │
│                                                                          │
│  ┌──────────┐    Schedule Window Open    ┌──────────────────┐           │
│  │ BOOT_OFF │──────────────────────────▶│  SCHEDULE_SPRAY  │           │
│  └────┬─────┘    + node ONLINE           │ (Spray phase)    │           │
│       │                                   │ ON via evidence  │           │
│       │ Override ON                       │ pipeline only    │           │
│       ▼                                   └────────┬─────────┘           │
│  ┌──────────────────┐                  Spray elapsed or                  │
│  │   OVERRIDE_RUN   │◀── Override ON    Override OFF         │           │
│  │ (Manual ON)      │                   ──────────────────▶  │           │
│  │ Lease bounded    │                  ┌──────────────────┐  │           │
│  └────────┬─────────┘                  │SCHEDULE_COOLDOWN │  │           │
│           │ Lease expired              │(Pause between    │  │           │
│           │                            │ sprays)          │  │           │
│           │                            └──────────────────┘  │           │
│           │                                                    │           │
│           │ Override OFF                                        │           │
│           ▼                                                    │           │
│  ┌──────────────────┐    Any state    ┌────────────────────┐ │           │
│  │OVERRIDE_HOLD_OFF │◀────────────────│   Any state +     │ │           │
│  │(Manual OFF)      │                  │   FAULT detected  │ │           │
│  └──────────────────┘                  └────────┬───────────┘ │           │
│       Hold-off expires                         ▼              │           │
│       ──────▶ SCHEDULE_COOLDOWN         ┌─────────────┐      │           │
│                                         │ FAULT_LATCH │      │           │
│                                         │(Safe-off)   │      │           │
│                                         └──────┬──────┘      │           │
│                                                │              │           │
│                                         FAULT_RESET +         │           │
│                                         Pre-flight PASS       │           │
│                                                │              │           │
│                                                ▼              │           │
│                                           BOOT_OFF           │           │
└──────────────────────────────────────────────────────────────────────────┘
```

### 2.3 Luồng Evidence Pipeline (Per-Command, Orthogonal to Macro State)

```text
[NONE] ──dispatch ON──▶ [COMMAND_DISPATCHED]
                              │
                              │ AGU 0x5A ACK within 300ms (AGU_LEGACY_ACK_TIMEOUT_MS)
                              ▼
                        [RF_ACKNOWLEDGED]
                              │
                              │ Correlated gate/driver feedback = ON
                              │ (from 0x0E RAM burst telemetry, bit driver_feedback)
                              ▼
                        [GATE_FEEDBACK_ON]
                              │
                              │ Current in configured valid load range
                              │ (1600–2600 mA nominal, from 0x0E telemetry)
                              ▼
                        [CURRENT_DETECTED]
                              │
                              │ flow >= 0.50 L/min (FEEDBACK_FLOW_CONFIRMED_MIN_LPM)
                              │ within T_flow_settle = 2500ms
                              ▼
                        [FLOW_CONFIRMED] ← UI may show RUNNING

Any failure at any stage →
    1. Dispatch OFF transaction (best-effort, 3 retries)
    2. Record fault code / evidence
    3. Enter FAULT_LATCH
    4. Publish MQTT lifecycle event
    5. UI must NOT show RUNNING
```

### 2.4 Luồng Deadman Lease Timer

```text
[OVERRIDE_RUN state]
        │
        │ lease_expiry_ms = now_ms + run_lease_ms (1000..300000)
        │
        ▼
[Every 100ms in main loop: serviceLeaseTick()]
        │
        ├── if now_ms >= lease_expiry_ms:
        │     │
        │     ├── Send AGU PUMP_OFF transaction (up to 3 retries)
        │     ├── If ACKED → Override → SCHEDULE_COOLDOWN
        │     ├── If not ACKED → FAULT_LATCH + SAFE_OFF_UNCONFIRMED
        │     ├── Publish MQTT lifecycle event LEASE_EXPIRED_SAFE_OFF
        │     └── Suppress immediate re-ON until T_cooldown_min (60s) boundary
        │
        └── else:
              └── Continue countdown (no action)
```

### 2.5 Luồng pending_command_table (Correlation Table)

```text
[MQTT command arrives via mqtt_client]
        │
        ├── Tạo rf_command_id (uint32): monotonic counter per node
        ├── Lấy mqtt_command_id (string): từ command JSON payload
        │
        ▼
[pending_command_table.insert(rf_command_id, mqtt_command_id)]
        │
        ├── Table size: COMMAND_TABLE_MAX_ENTRIES = 16 (bounded RAM)
        ├── Entry TTL: COMMAND_TABLE_TTL_MS = 2000ms
        │
        ▼
[RF transaction completes (ACKED / TIMEOUT / FAULT)]
        │
        ├── Lookup mqtt_command_id = pending_command_table.find(rf_command_id)
        ├── Publish MQTT ACK with mqtt_command_id
        └── Mark entry for cleanup
        │
        ▼
[Every 2000ms: serviceCommandTableCleanup()]
        │
        ├── for each entry in table:
        │     if (now_ms - entry.inserted_ms) > COMMAND_TABLE_TTL_MS:
        │         remove entry
        │         log WARNING if entry was never resolved
        │
        └── Invariant: table RAM ≤ COMMAND_TABLE_MAX_ENTRIES × sizeof(PendingCommandEntry)
            ≤ 16 × (4 + 64 + 4) = 1120 bytes << 20 KB limit
```

---

## 3. PHÂN RÃ CHI TIẾU TÁC VỤ

### TRACK A — Virtual FSM Core (Data Structures & Transitions)

#### Task A-1: `aeroponics-firmware/include/node_fsm.h` — FSM header definitions

- **File:** `aeroponics-firmware/include/node_fsm.h`

```cpp
#pragma once
#include <cstdint>
#include <cstddef>
#include "config.h"

// ─────────────────────────────────────────────
// §3.1 from STATE_MACHINE_MATRIX: 6 Macro States
// ─────────────────────────────────────────────
enum class MacroState : uint8_t {
    BOOT_OFF,            // Gateway/node boot, resync, unknown state
    SCHEDULE_SPRAY,      // Scheduled irrigation permitted and active
    SCHEDULE_COOLDOWN,   // Scheduled pause between sprays
    OVERRIDE_RUN,        // Manual/authorized temporary ON
    OVERRIDE_HOLD_OFF,   // Manual temporary OFF suppressing schedule
    FAULT_LATCH          // Safety fault or unsafe evidence
};

// ─────────────────────────────────────────────
// §3.2 Evidence Pipeline Substates (orthogonal)
// ─────────────────────────────────────────────
enum class EvidenceStage : uint8_t {
    NONE,                  // No pending command evidence
    COMMAND_DISPATCHED,    // AGU frame sent, awaiting ACK
    RF_ACKNOWLEDGED,       // 0x5A received within t_ack (300ms)
    GATE_FEEDBACK_ON,      // Driver feedback = ON from telemetry
    CURRENT_DETECTED,      // Current in valid load range
    FLOW_CONFIRMED         // Flow >= min_flow within T_flow_settle
};

// ─────────────────────────────────────────────
// §4: Terminal/error lifecycle events
// ─────────────────────────────────────────────
enum class LifecycleEvent : uint8_t {
    NONE,
    ACCEPTED,
    REJECTED,
    RF_ACKED,
    PUMP_FEEDBACK_ON,
    CURRENT_DETECTED,
    FLOW_CONFIRMED,
    COMPLETED,
    RF_TIMEOUT_OR_NACK,
    SAFE_OFF_UNCONFIRMED,
    FAULT_LATCHED,
    LEASE_EXPIRED_SAFE_OFF,
    RESET_REJECTED
};

// ─────────────────────────────────────────────
// Per-node FSM runtime state (in RAM)
// ─────────────────────────────────────────────
struct NodeFsmState {
    uint8_t node_id;              // 4..7 (production)
    MacroState macro_state;       // Current macro state
    EvidenceStage evidence_stage; // Current evidence substate

    // Lease tracking
    uint32_t lease_expiry_ms;     // Absolute timestamp when lease expires
    uint32_t lease_start_ms;      // When current lease started
    uint32_t run_lease_ms;        // Configured lease duration
    bool lease_active;            // Whether lease is currently running

    // Cooldown boundary enforcement
    uint32_t cooldown_boundary_ms; // Earliest timestamp for next ON

    // Fault tracking
    uint8_t fault_flags;          // Hardware fault flags from telemetry
    LifecycleEvent last_lifecycle_event;

    // Timestamps
    uint32_t last_transition_ms;  // When last macro state changed
    uint32_t last_evidence_ms;    // When last evidence stage changed
};

// ─────────────────────────────────────────────
// Command correlation table entry
// ─────────────────────────────────────────────
struct PendingCommandEntry {
    uint32_t rf_command_id;              // Local monotonic counter
    char mqtt_command_id[65];            // From MQTT payload (max 64 chars)
    uint8_t node_id;                     // Target node (4..7)
    uint32_t inserted_ms;                // Timestamp for TTL cleanup
    bool resolved;                       // True after terminal lifecycle event
};

// ─────────────────────────────────────────────
// Command table manager
// ─────────────────────────────────────────────
class PendingCommandTable {
public:
    PendingCommandTable();

    // Insert a new command mapping
    uint32_t insert(uint8_t node_id, const char* mqtt_command_id);

    // Find mqtt_command_id by rf_command_id
    const char* find(uint32_t rf_command_id) const;

    // Mark entry as resolved (terminal event received)
    void resolve(uint32_t rf_command_id);

    // Remove entries older than TTL
    void cleanup(uint32_t now_ms);

    // Get current table size
    size_t size() const;

private:
    PendingCommandEntry entries_[16]; // COMMAND_TABLE_MAX_ENTRIES
    uint32_t next_rf_command_id_;
    size_t count_;
};

// ─────────────────────────────────────────────
// Core FSM functions
// ─────────────────────────────────────────────

// Transition macro state with guard check
bool transitionMacroState(NodeFsmState& fsm, MacroState target, uint32_t now_ms);

// Advance evidence pipeline stage (one step forward only)
bool advanceEvidenceStage(NodeFsmState& fsm, EvidenceStage target, uint32_t now_ms);

// Reset evidence pipeline to NONE (after OFF transaction or fault)
void resetEvidenceStage(NodeFsmState& fsm, uint32_t now_ms);

// Deadman lease tick — returns true if lease expired this tick
bool leaseTick(NodeFsmState& fsm, uint32_t now_ms);

// Cooldown boundary check — returns true if ON is allowed
bool canScheduleOn(const NodeFsmState& fsm, uint32_t now_ms);

// Pre-flight guard for FAULT_RESET
bool preflightFaultReset(const NodeFsmState& fsm);

// Initialize FSM state for a node
void initNodeFsm(NodeFsmState& fsm, uint8_t node_id);
```

#### Task A-2: `aeroponics-firmware/src/node_fsm.cpp` — FSM transition logic

- **File:** `aeroponics-firmware/src/node_fsm.cpp`

```cpp
bool transitionMacroState(NodeFsmState& fsm, MacroState target, uint32_t now_ms) {
    // Guard: FAULT_LATCH → BOOT_OFF only via FAULT_RESET with preflight pass
    if (fsm.macro_state == MacroState::FAULT_LATCH &&
        target != MacroState::BOOT_OFF) {
        return false; // Must go through BOOT_OFF first
    }

    // Guard: OVERRIDE_RUN → SCHEDULE_COOLDOWN only via lease expiry or explicit OFF
    if (fsm.macro_state == MacroState::OVERRIDE_RUN &&
        target == MacroState::SCHEDULE_SPRAY) {
        return false; // Must go through SCHEDULE_COOLDOWN first
    }

    // Guard: T_cooldown_min enforcement
    if (target == MacroState::SCHEDULE_SPRAY && !canScheduleOn(fsm, now_ms)) {
        return false; // Cooldown boundary not yet reached
    }

    // Perform transition
    fsm.macro_state = target;
    fsm.last_transition_ms = now_ms;
    resetEvidenceStage(fsm, now_ms);
    return true;
}

bool canScheduleOn(const NodeFsmState& fsm, uint32_t now_ms) {
    return now_ms >= fsm.cooldown_boundary_ms;
}

bool leaseTick(NodeFsmState& fsm, uint32_t now_ms) {
    if (!fsm.lease_active) return false;
    if (now_ms >= fsm.lease_expiry_ms) {
        fsm.lease_active = false;
        return true; // LEASE EXPIRED
    }
    return false;
}

void initNodeFsm(NodeFsmState& fsm, uint8_t node_id) {
    fsm.node_id = node_id;
    fsm.macro_state = MacroState::BOOT_OFF;
    fsm.evidence_stage = EvidenceStage::NONE;
    fsm.lease_expiry_ms = 0;
    fsm.lease_start_ms = 0;
    fsm.run_lease_ms = 0;
    fsm.lease_active = false;
    fsm.cooldown_boundary_ms = 0;
    fsm.fault_flags = 0;
    fsm.last_lifecycle_event = LifecycleEvent::NONE;
    fsm.last_transition_ms = 0;
    fsm.last_evidence_ms = 0;
}
```

---

### TRACK B — Safety Timer Constants & Guard Integration

#### Task B-1: `aeroponics-firmware/include/config.h` — Thêm safety timer constants

- **File:** `aeroponics-firmware/include/config.h`
- **Section:** Thêm SECTION 13: SAFETY TIMERS & FSM CONSTANTS

```cpp
// ============================================================================
// SECTION 13: Virtual FSM Safety Timers & Evidence Pipeline Constants
// ============================================================================

// Flow settle timeout: max wait after RF_ACK for flow evidence
constexpr uint32_t T_FLOW_SETTLE_MS = 2500;

// Cooldown minimum: min pause between consecutive ON commands
constexpr uint32_t T_COOLDOWN_MIN_MS = 60000;  // 60 seconds

// Polling interval for opcode 0x0E per node
constexpr uint32_t T_POLL_0x0E_MS = 1000;  // 1 second

// Deadman lease bounds (from interface-wire-contract §3.3)
constexpr uint32_t RUN_LEASE_MIN_MS = 1000;       // 1 second minimum
constexpr uint32_t RUN_LEASE_MAX_MS = 300000;     // 5 minutes maximum
constexpr uint32_t DEFAULT_DEADMAN_LEASE_MS = 60000; // 60 seconds default

// Command correlation table
constexpr size_t COMMAND_TABLE_MAX_ENTRIES = 16;
constexpr uint32_t COMMAND_TABLE_TTL_MS = 2000;  // 2 seconds TTL cleanup

// Evidence pipeline timing
constexpr uint32_t AGU_ACK_TIMEOUT_MS = 300;     // From AGU_LEGACY_ACK_TIMEOUT_MS
constexpr uint32_t GATE_FEEDBACK_TIMEOUT_MS = 1000; // Max wait for gate feedback
constexpr uint32_t CURRENT_DETECT_TIMEOUT_MS = 500;  // Max wait for current

// Flow thresholds (from Section 8 of config.h, re-aliased for FSM context)
constexpr uint16_t FSM_FLOW_CONFIRMED_MIN_LPM_X100 = 50;  // 0.50 L/min
constexpr uint16_t FSM_FLOW_LEAKAGE_MAX_LPM_X100 = 20;    // 0.20 L/min

// Compile-time invariants
static_assert(RUN_LEASE_MIN_MS >= 1000, "Minimum lease must be >= 1 second");
static_assert(RUN_LEASE_MAX_MS <= 300000, "Maximum lease must be <= 5 minutes");
static_assert(T_FLOW_SETTLE_MS >= 1000, "Flow settle must be >= 1 second");
static_assert(T_COOLDOWN_MIN_MS >= 30000, "Cooldown minimum must be >= 30 seconds");
static_assert(COMMAND_TABLE_MAX_ENTRIES <= 32, "Command table bounded to 32 entries max");
```

---

### TRACK C — Command Correlation Table

#### Task C-1: `aeroponics-firmware/src/node_fsm.cpp` — Implement PendingCommandTable

- **File:** `aeroponics-firmware/src/node_fsm.cpp`

```cpp
PendingCommandTable::PendingCommandTable()
    : entries_{}, next_rf_command_id_(1), count_(0) {}

uint32_t PendingCommandTable::insert(uint8_t node_id, const char* mqtt_command_id) {
    if (count_ >= COMMAND_TABLE_MAX_ENTRIES) {
        // Table full: cleanup expired entries first
        // If still full after cleanup, reject (return 0)
    }
    // Find empty slot or expired entry
    for (size_t i = 0; i < COMMAND_TABLE_MAX_ENTRIES; ++i) {
        if (!entries_[i].resolved && entries_[i].mqtt_command_id[0] != '\0')
            continue;
        // Insert
        entries_[i].rf_command_id = next_rf_command_id_++;
        strncpy(entries_[i].mqtt_command_id, mqtt_command_id, 64);
        entries_[i].mqtt_command_id[64] = '\0';
        entries_[i].node_id = node_id;
        entries_[i].inserted_ms = millis();  // or caller-provided timestamp
        entries_[i].resolved = false;
        count_++;
        return entries_[i].rf_command_id;
    }
    return 0; // Should not reach here
}

void PendingCommandTable::cleanup(uint32_t now_ms) {
    for (size_t i = 0; i < COMMAND_TABLE_MAX_ENTRIES; ++i) {
        if (entries_[i].mqtt_command_id[0] == '\0') continue;
        if ((now_ms - entries_[i].inserted_ms) > COMMAND_TABLE_TTL_MS) {
            if (!entries_[i].resolved) {
                ESP_LOGW(TAG, "Command TTL expired without resolution: rf_id=%u mqtt_id=%s",
                         entries_[i].rf_command_id, entries_[i].mqtt_command_id);
            }
            entries_[i] = PendingCommandEntry{};
            count_--;
        }
    }
}

const char* PendingCommandTable::find(uint32_t rf_command_id) const {
    for (size_t i = 0; i < COMMAND_TABLE_MAX_ENTRIES; ++i) {
        if (entries_[i].rf_command_id == rf_command_id && !entries_[i].resolved)
            return entries_[i].mqtt_command_id;
    }
    return nullptr;
}
```

- **RAM budget invariant:** `sizeof(PendingCommandEntry) = 4 + 65 + 1 + 4 + 1 = 75 bytes`. `16 entries = 1200 bytes` << 20 KB limit.

---

### TRACK D — FSM Integration into Main Loop

#### Task D-1: `aeroponics-firmware/src/main.cpp` — Replace LegacyOverride with NodeFsmState

- **File:** `aeroponics-firmware/src/main.cpp`

```cpp
// TRƯỚC:
static LegacyOverride g_legacy_overrides[RF_PRODUCTION_MAX_NODE_ID + 1] = {};

// SAU:
static NodeFsmState g_node_fsm[RF_PRODUCTION_MAX_NODE_ID + 1] = {};
static PendingCommandTable g_pending_commands;
```

- **Hàm bị ảnh hưởng:** `setup()` → thêm `initNodeFsm()` cho từng node.

#### Task D-2: `aeroponics-firmware/src/main.cpp` — Implement `serviceFsmTick()`

- **File:** `aeroponics-firmware/src/main.cpp`
- **Hàm mới:** `serviceFsmTick(uint32_t current_ms)`

```cpp
static void serviceFsmTick(uint32_t current_ms) {
    if (!g_gateway_operational) return;

    for (uint8_t id = RF_PRODUCTION_MIN_NODE_ID; id <= RF_PRODUCTION_MAX_NODE_ID; ++id) {
        NodeFsmState& fsm = g_node_fsm[id];

        // 1. Lease tick — check for expired deadman
        if (leaseTick(fsm, current_ms)) {
            // Lease expired: dispatch OFF, transition OVERRIDE_RUN → SCHEDULE_COOLDOWN
            executeAguPump(id, false, nullptr);
            fsm.cooldown_boundary_ms = current_ms + T_COOLDOWN_MIN_MS;
            transitionMacroState(fsm, MacroState::SCHEDULE_COOLDOWN, current_ms);
            publishNodeLifecycleEvent(id, LifecycleEvent::LEASE_EXPIRED_SAFE_OFF);
        }

        // 2. Flow settle timeout check
        if (fsm.evidence_stage == EvidenceStage::RF_ACKNOWLEDGED ||
            fsm.evidence_stage == EvidenceStage::GATE_FEEDBACK_ON ||
            fsm.evidence_stage == EvidenceStage::CURRENT_DETECTED) {
            if ((current_ms - fsm.last_evidence_ms) > T_FLOW_SETTLE_MS) {
                // Flow not confirmed within settle time → fault
                executeAguPump(id, false, nullptr);
                transitionMacroState(fsm, MacroState::FAULT_LATCH, current_ms);
                publishNodeLifecycleEvent(id, LifecycleEvent::FAULT_LATCHED);
            }
        }

        // 3. Command table cleanup
        g_pending_commands.cleanup(current_ms);
    }
}
```

#### Task D-3: `aeroponics-firmware/src/main.cpp` — Implement `servicePollTelemetry()`

- **File:** `aeroponics-firmware/src/main.cpp`
- **Hàm mới:** `servicePollTelemetry(uint32_t current_ms)`

```cpp
static void servicePollTelemetry(uint32_t current_ms) {
    if (!g_gateway_operational || !g_agu_legacy_host) return;
    static uint32_t last_poll_ms = 0;
    if (current_ms - last_poll_ms < T_POLL_0x0E_MS) return;
    last_poll_ms = current_ms;

    for (uint8_t id = RF_PRODUCTION_MIN_NODE_ID; id <= RF_PRODUCTION_MAX_NODE_ID; ++id) {
        if (g_agu_bus_busy) break;
        NodeFsmState& fsm = g_node_fsm[id];

        // Only poll if node is in active state (not BOOT_OFF or FAULT_LATCH)
        if (fsm.macro_state == MacroState::BOOT_OFF ||
            fsm.macro_state == MacroState::FAULT_LATCH) {
            continue;
        }

        // Execute 0x0E readRamBurst and update evidence pipeline
        uint8_t ram_data[8] = {};
        AguRfTransactionResult result = g_agu_legacy_host->readRamBurst(id, 0x0100, ram_data);

        if (result.result == AguRfResult::ACKED) {
            // Parse 8-byte RAM block:
            //   byte 0: reported_pump_state
            //   byte 1: driver_feedback
            //   byte 2-3: flow_lpm_x100 (uint16 LE)
            //   byte 4-5: pulse_count (uint16 LE)
            //   byte 6: fault_flags
            //   byte 7: reserved
            updateNodeEvidenceFromTelemetry(id, ram_data, current_ms);
        } else {
            // Timeout or error → potential stale
            g_node_registry.updateHealth(id, NodeHealthStatus::STALE);
        }

        vTaskDelay(pdMS_TO_TICKS(20)); // Bus guard delay between nodes
    }
}
```

---

### TRACK E — MQTT Integration & Lifecycle Events

#### Task E-1: `aeroponics-firmware/src/mqtt_client.cpp` — Publish lifecycle events

- **File:** `aeroponics-firmware/src/mqtt_client.cpp`
- **Hàm mới:** `publishLifecycleEvent(uint8_t node_id, const char* mqtt_command_id, LifecycleEvent event)`

```cpp
void MqttClient::publishLifecycleEvent(uint8_t node_id,
                                       const char* mqtt_command_id,
                                       LifecycleEvent event) {
    if (!isConnected() || !mqtt_command_id) return;

    // Topic: aeroponics/v1/node/{nodeId}/event
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    snprintf(topic, sizeof(topic), "aeroponics/v1/node/%u/event", node_id);

    // JSON payload per interface-wire-contract §8
    char payload[MQTT_TELEMETRY_DOC_SIZE];
    snprintf(payload, sizeof(payload),
        "{\"schema_version\":\"1.0\",\"command_id\":\"%s\",\"node_id\":%u,"
        "\"event\":\"%s\",\"gateway_timestamp_ms\":%lu}",
        mqtt_command_id, node_id, lifecycleEventToString(event), millis());

    // CRITICAL: Non-retained for transactional topics (per contract §7)
    publish(topic, payload, { qos: 1, retain: false });
}
```

- **RETAIN invariant:** `retain: false` bắt buộc cho mọi topic transactional (`command`, `ack`, `event`, `telemetry`). Chỉ status/LWT mới được retain.

#### Task E-2: `aeroponics-firmware/src/main.cpp` — Update `executeAguPump` with FSM integration

- **File:** `aeroponics-firmware/src/main.cpp`
- **Hàm bị ảnh hưởng:** `executeAguPump()`

```cpp
static bool executeAguPump(uint8_t node_id, bool turn_on, const char* command_id) {
    // ... existing RF transaction logic ...

    NodeFsmState& fsm = g_node_fsm[node_id];

    if (result.result == AguRfResult::ACKED) {
        // Register in pending command table
        uint32_t rf_cmd_id = g_pending_commands.insert(node_id, command_id ? command_id : "LOCAL");

        // Advance evidence: COMMAND_DISPATCHED → RF_ACKNOWLEDGED
        advanceEvidenceStage(fsm, EvidenceStage::RF_ACKNOWLEDGED, millis());

        // If turning ON: set lease
        if (turn_on) {
            fsm.lease_active = true;
            fsm.lease_start_ms = millis();
            fsm.lease_expiry_ms = millis() + fsm.run_lease_ms;
        }

        // Publish lifecycle event
        publishNodeLifecycleEvent(node_id, LifecycleEvent::RF_ACKED);
    } else {
        // RF failure → transition to FAULT_LATCH
        transitionMacroState(fsm, MacroState::FAULT_LATCH, millis());
        publishNodeLifecycleEvent(node_id, LifecycleEvent::RF_TIMEOUT_OR_NACK);
    }

    // ... rest of existing logic ...
}
```

---

### TRACK F — Unit Tests

#### Task F-1: `aeroponics-firmware/test/test_fsm/test_fsm.cpp` — FSM transition tests

- **File:** `aeroponics-firmware/test/test_fsm/test_fsm.cpp`

```cpp
TEST_CASE("BOOT_OFF → SCHEDULE_SPRAY on valid schedule window") {
    NodeFsmState fsm;
    initNodeFsm(fsm, 4);
    fsm.macro_state = MacroState::BOOT_OFF;
    fsm.cooldown_boundary_ms = 0;  // No cooldown pending
    REQUIRE(transitionMacroState(fsm, MacroState::SCHEDULE_SPRAY, 10000));
    REQUIRE(fsm.macro_state == MacroState::SCHEDULE_SPRAY);
}

TEST_CASE("FAULT_LATCH blocks direct transition to SCHEDULE_SPRAY") {
    NodeFsmState fsm;
    initNodeFsm(fsm, 4);
    fsm.macro_state = MacroState::FAULT_LATCH;
    REQUIRE_FALSE(transitionMacroState(fsm, MacroState::SCHEDULE_SPRAY, 10000));
}

TEST_CASE("FAULT_LATCH → BOOT_OFF via preflight") {
    NodeFsmState fsm;
    initNodeFsm(fsm, 4);
    fsm.macro_state = MacroState::FAULT_LATCH;
    fsm.fault_flags = 0;
    REQUIRE(preflightFaultReset(fsm));
    REQUIRE(transitionMacroState(fsm, MacroState::BOOT_OFF, 10000));
}

TEST_CASE("Lease expiry transitions OVERRIDE_RUN → SCHEDULE_COOLDOWN") {
    NodeFsmState fsm;
    initNodeFsm(fsm, 4);
    fsm.macro_state = MacroState::OVERRIDE_RUN;
    fsm.lease_active = true;
    fsm.lease_expiry_ms = 5000;
    REQUIRE_FALSE(leaseTick(fsm, 4000));  // Not yet
    REQUIRE(leaseTick(fsm, 5000));        // Expired!
    REQUIRE_FALSE(fsm.lease_active);
}

TEST_CASE("T_cooldown_min prevents premature ON") {
    NodeFsmState fsm;
    initNodeFsm(fsm, 4);
    fsm.macro_state = MacroState::SCHEDULE_COOLDOWN;
    fsm.cooldown_boundary_ms = 60000; // Must wait until 60s
    REQUIRE_FALSE(canScheduleOn(fsm, 30000));  // Too early
    REQUIRE(canScheduleOn(fsm, 60000));         // Just right
}

TEST_CASE("PendingCommandTable insert/find/cleanup") {
    PendingCommandTable table;
    uint32_t rf_id = table.insert(4, "cmd-abc-123");
    REQUIRE(rf_id > 0);
    const char* found = table.find(rf_id);
    REQUIRE(found != nullptr);
    REQUIRE(strcmp(found, "cmd-abc-123") == 0);
    table.resolve(rf_id);
    REQUIRE(table.find(rf_id) == nullptr);  // Resolved → not found
}

TEST_CASE("PendingCommandTable TTL cleanup") {
    PendingCommandTable table;
    uint32_t rf_id = table.insert(4, "cmd-ttl-test");
    table.cleanup(0);         // Not expired yet
    REQUIRE(table.size() == 1);
    table.cleanup(3000);      // Past 2000ms TTL
    REQUIRE(table.size() == 0);
}
```

---

## 4. TIÊU CHUẨN RÀ SOÁT CỨNG (Sprint 2)

### Rule S2-FSM-01: Macro State Transition Guard — No Jump
```
PASS: Mỗi transition đều qua guard check và set cooldown_boundary_ms nếu cần
PASS: FAULT_LATCH chỉ reset qua BOOT_OFF sau preflight pass
FAIL: Bắt gặp SCHEDULE_SPRAY trực tiếp từ FAULT_LATCH mà không qua BOOT_OFF
FAIL: OVERRIDE_RUN → SCHEDULE_SPRAY mà không qua SCHEDULE_COOLDOWN
```

### Rule S2-FSM-02: Evidence Pipeline Ordering — No Skip
```
PASS: Evidence progression luôn theo thứ tự: DISPATCHED → ACKED → FEEDBACK → CURRENT → FLOW
PASS: FLOW_CONFIRMED chỉ được publish khi có correlated evidence hợp lệ
FAIL: Nhảy từ COMMAND_DISPATCHED → FLOW_CONFIRMED bỏ qua RF_ACKNOWLEDGED
FAIL: UI hiển thị RUNNING khi evidence stage < FLOW_CONFIRMED
```

### Rule S2-TIMER-03: Deadman Lease — Bounded RAM Timer
```
PASS: run_lease_ms nằm trong [1000, 300000]
PASS: Lease expiry → OFF transaction → SCHEDULE_COOLDOWN (KHÔNG tự ON mới)
PASS: Lease expiry tại O(n) nơi n = 4 nodes, không malloc trong tick
FAIL: Lease expiry không dispatch OFF
FAIL: Sau lease expiry, node tự quay lại ON mà không qua cooldown
```

### Rule S2-TIMER-04: T_flow_settle Enforcement
```
PASS: Sau RF_ACK, evidence stage phải tiến đến FLOW_CONFIRMED trong 2500ms
PASS: Nếu không có flow evidence trong 2500ms → FAULT_LATCH
FAIL: FLOW_CONFIRMED được publish mà không có flow evidence hợp lệ (>= 0.50 L/min)
```

### Rule S2-TIMER-05: T_cooldown_min Water Shock Prevention
```
PASS: Sau OFC expired hoặc scheduled OFF, T_cooldown_min_ms = 60s phải được tôn trọng
PASS: earliest_next_on = last_off_timestamp + T_cooldown_min_ms
FAIL: Node ON lại trước khi T_cooldown_min trôi qua
```

### Rule S2-TABLE-06: Command Table RAM Budget
```
PASS: pending_command_table dùng ≤ COMMAND_TABLE_MAX_ENTRIES × sizeof(PendingCommandEntry)
PASS: Table cleanup chạy định kỳ, không để entry tồn tại > 2000ms
PASS: Không malloc trong insert/find/cleanup — static array only
FAIL: Bắt gặp new/malloc trong PendingCommandTable methods
FAIL: Table RAM usage > 20 KB aggregate
```

---

*Sprint 2 Planning — Gateway Virtual FSM & Safety Timers. Golden Baseline: STATE_MACHINE_MATRIX v1.0.0.*
