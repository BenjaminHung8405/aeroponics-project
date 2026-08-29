# Normalized Telemetry & Analytics Contract Specification (Version 1.0)

> **Document Status:** Official Architectural Contract & Metrics Specification (`SPEC-TELEMETRY-ANALYTICS-001`)  
> **Target Scope:** ESP32-S3 Gateway ↔ 4 MEGA8 Remote Nodes ↔ TimescaleDB & NestJS Backend  
> **Date of Enforcement:** 2026-08-22 (Sprint 1.5 Baseline)  
> **Governing Specifications:** [`docs/RF_PROTOCOL.md`](./RF_PROTOCOL.md), [`docs/RF_FLOW_POC_TEST_PLAN.md`](./RF_FLOW_POC_TEST_PLAN.md), [`docs/RF_FLOW_POC_DECISION.md`](./RF_FLOW_POC_DECISION.md), [`database/schema.sql`](../database/schema.sql)

---

## 1. Executive Ingestion & Storage Policy (Rule S1.5-PARSE-11)

### 1.1 Absolute Zero Raw RF Frame Ingestion Invariant
In strict adherence to rule **`S1.5-PARSE-11`**, the production data tier and persistence engines (TimescaleDB, PostgreSQL, MQTT Event Stream) **SHALL NOT persist raw RF wire frames, preamble headers, MAC tags, or trailing CRC checksums**.

```text
[Raw RF Wire Frame] ── UART ──► [RfFrameCodec Parser]
                                         │
                   ┌─────────────────────┴─────────────────────┐
                   │                                           │
                   ▼ (Malformed / Bad MAC / Bad CRC)           ▼ (Authenticated & Validated)
            [FAIL-CLOSED DROP]                      [TelemetryNormalizer Engine]
        (Counter Increment Only)                               │
                                            ┌──────────────────┴──────────────────┐
                                            ▼                                     ▼
                                  [Normalized Records]                [Analytics Metric Engines]
                              (Structured Domain Entities)             (Latency, Rates, Runtimes)
                                            │                                     │
                                            └──────────────────┬──────────────────┘
                                                               ▼
                                                  [TimescaleDB & MQTT JSON]
                                                  (Strictly Parsed Schema)
```

1. **Gateway Boundary:** All incoming RF frames from UART must be immediately decoded, authenticated via HMAC-SHA256, verified via CRC-16, and checked against anti-replay windows.
2. **Normalizer Execution:** Decoded packets are transformed into strongly-typed `Normalized` domain structures. Raw byte buffers are discarded in memory before any database write or MQTT serialization.
3. **Transport Error Accounting:** Transport anomalies (CRC failures, HMAC rejections, sequence drops, duplicate drops, framing timeouts) are recorded strictly as **monotonic metric counters**, not raw byte dumps.

---

## 2. Normalized Data Models

### 2.1 Pump Command Lifecycle Event (`pump_commands`)
Tracks the end-to-end progression of an actuation command from dispatch to physical flow confirmation.

| Field Name | Type / Format | Constraints / Range | Semantic Description |
|---|---|---|---|
| `time` | `TIMESTAMPTZ` | UTC, microsecond precision | Gateway timestamp of command dispatch. |
| `command_id` | `UUID` / `VARCHAR(64)` | Non-empty, topic-safe | Unique correlation UUID assigned by control plane. |
| `season_id` | `INT` | FK $\to$ `seasons.id` | Active agricultural season identifier. |
| `node_id` | `SMALLINT` | `1 .. 4` (support to `12`) | Target remote MEGA8 node ID. |
| `group_id` | `SMALLINT` | `1 .. 4` (nullable) | Assigned Timer Group ID at dispatch time. |
| `treatment_version_id` | `INT` | FK $\to$ `treatment_versions.id` | Associated treatment version configuration. |
| `action` | `VARCHAR(8)` | `'ON'`, `'OFF'` | Commanded pump target state. |
| `rf_seq` | `INT` | `0 .. 65535` | 16-bit monotonic sequence number in RF frame. |
| `run_lease_ms` | `INT` | `5000 .. 300000` | Node autonomous safety lease duration in milliseconds. |
| `source` | `VARCHAR(32)` | `'MANUAL_OVERRIDE'`, `'FAIL_SAFE'`, `'SCHEDULE'` | Dispatch authority source. |
| `boot_session_id` | `INT` | `1 .. 0xFFFFFFFF` | Gateway boot session ID at dispatch. |
| `retry_count` | `INT` | `0 .. 3` | Bounded RF transmission retransmission attempts. |
| `outcome` | `VARCHAR(32)` | Enum | `'PENDING'`, `'ACKED'`, `'FLOW_CONFIRMED'`, `'FAULT_NO_ACK'`, `'FAULT_NO_FLOW'`, `'FAULT_UNEXPECTED_FLOW'`, `'REJECTED'` |
| `acked_at` | `TIMESTAMPTZ` | Nullable | Timestamp when RF `COMMAND_ACK` was received. |
| `feedback_at` | `TIMESTAMPTZ` | Nullable | Timestamp when Tier 1 / Tier 2 electrical feedback arrived. |
| `flow_confirmed_at` | `TIMESTAMPTZ` | Nullable | Timestamp when Tier 3 hydraulic flow was confirmed. |
| `node_timestamp_ms` | `BIGINT` | Monotonic uptime ms | Reporting node's local clock at response. |
| `gateway_timestamp_ms`| `BIGINT` | Monotonic uptime ms | Gateway's local clock at frame reception. |
| `command_to_ack_latency_ms` | `INT` | $\ge 0$ | Turnaround time $T_{\text{ack}} - T_{\text{dispatch}}$. |
| `flow_start_latency_ms` | `INT` | $\ge 0$ | Time until flow start $T_{\text{flow\_confirm}} - T_{\text{dispatch}}$. |
| `execution_duration_ms` | `INT` | $\ge 0$ | Actual continuous runtime until OFF transition. |
| `fault_reason` | `TEXT` | Nullable | Quantitative diagnostic string if command failed. |

---

### 2.2 Pump State & Schedule vs Override Event (`pump_state_events`)
Records explicit state transitions, autonomous MEGA8 schedule states, and temporary override lifecycle.

| Field Name | Type / Format | Constraints / Range | Semantic Description |
|---|---|---|---|
| `time` | `TIMESTAMPTZ` | UTC | Time of state event generation. |
| `season_id` | `INT` | FK $\to$ `seasons.id` | Active season ID. |
| `node_id` | `SMALLINT` | `1 .. 4` | Target node ID. |
| `group_id` | `SMALLINT` | `1 .. 4` (nullable) | Active timer group. |
| `desired_state` | `VARCHAR(8)` | `'ON'`, `'OFF'` | Commanded state in registry. |
| `reported_state` | `VARCHAR(8)` | `'ON'`, `'OFF'` | Explicit state reported by physical node actuator. |
| `source` | `VARCHAR(32)` | `'SCHEDULE'`, `'MANUAL_OVERRIDE'`, `'FAIL_SAFE'` | Origin of state change. |
| `schedule_state` | `VARCHAR(16)` | `'UNKNOWN'`, `'SPRAYING'`, `'COOLING_DOWN'`, `'IDLE'`, `'PAUSED'` | Autonomous MEGA8 internal timer state. |
| `override_state` | `VARCHAR(16)` | `'NONE'`, `'OVERRIDE_OFF'`, `'OVERRIDE_ON'` | Active manual override condition. |
| `resume_reason` | `VARCHAR(32)` | `'NONE'`, `'OVERRIDE_EXPIRED'`, `'CYCLE_BOUNDARY'`, `'MANUAL_RESUME'`, `'FAIL_SAFE_RESUME'` | Cause of override termination and schedule resumption. |
| `boot_session_id` | `INT` | `1 .. 0xFFFFFFFF` | Node boot session ID. |
| `rf_seq` | `INT` | `0 .. 65535` | Frame sequence number. |
| `node_timestamp_ms` | `BIGINT` | Node uptime ms | Dual timestamp: Node local clock. |
| `gateway_timestamp_ms`| `BIGINT` | Gateway uptime ms | Dual timestamp: Gateway local clock. |
| `reason` | `TEXT` | Nullable | Descriptive transition reason. |

---

### 2.3 Pump Multi-Tier Feedback Event (`pump_feedback_events`)
Audits Tier 1 (Driver Gate Sense) and Tier 2 (Electrical Load Current) hardware state.

| Field Name | Type / Format | Constraints / Range | Semantic Description |
|---|---|---|---|
| `time` | `TIMESTAMPTZ` | UTC | Feedback reception timestamp. |
| `season_id` | `INT` | FK $\to$ `seasons.id` | Active season ID. |
| `node_id` | `SMALLINT` | `1 .. 4` | Target node ID. |
| `group_id` | `SMALLINT` | `1 .. 4` (nullable) | Active timer group. |
| `command_id` | `UUID` | Nullable | Correlated command ID. |
| `driver_feedback` | `VARCHAR(8)` | `'ON'`, `'OFF'` | Tier 1 Optocoupler/Gate sense state. |
| `load_feedback` | `VARCHAR(8)` | `'ON'`, `'OFF'`, `'UNKNOWN'` | Tier 2 ACS712 current threshold state ($\ge 150\text{mA}$). |
| `driver_feedback_mismatch` | `BOOLEAN` | `TRUE` / `FALSE` | `TRUE` if commanded state $\ne$ driver feedback after settling. |
| `fault_flags` | `INT` | Bitmask | Bit 0: No Flow, Bit 1: Unexpected Flow, Bit 2: Lease Expired, Bit 3: Open Load, Bit 4: Stall. |
| `voltage_v` | `NUMERIC(6,2)` | $0.00 .. 25.00\text{V}$ | Node DC supply rail voltage. |
| `current_ma` | `INT` | $0 .. 6000\text{mA}$ | Instantaneous pump current consumption. |
| `boot_session_id` | `INT` | `1 .. 0xFFFFFFFF` | Node boot session ID. |
| `rf_seq` | `INT` | `0 .. 65535` | Monotonic sequence number. |
| `node_timestamp_ms` | `BIGINT` | Node uptime ms | Dual timestamp: Node clock. |
| `gateway_timestamp_ms`| `BIGINT` | Gateway uptime ms | Dual timestamp: Gateway clock. |

---

### 2.4 Hydraulic Flow Event (`flow_events`)
Captures quantitative flow measurements verified against immutable sensor calibrations.

| Field Name | Type / Format | Constraints / Range | Semantic Description |
|---|---|---|---|
| `time` | `TIMESTAMPTZ` | UTC | Flow event timestamp. |
| `season_id` | `INT` | FK $\to$ `seasons.id` | Active season ID. |
| `node_id` | `SMALLINT` | `1 .. 4` | Target node ID. |
| `group_id` | `SMALLINT` | `1 .. 4` (nullable) | Active timer group. |
| `command_id` | `UUID` | Nullable | Correlated command ID. |
| `litres_total` | `NUMERIC(10,3)`| $\ge 0.000\text{ L}$ | Cumulative delivered volume across node lifetime. |
| `pulse_count` | `BIGINT` | $\ge 0$ | Raw monotonic pulse count from hardware ISR. |
| `flow_rate_lpm` | `NUMERIC(6,2)` | $0.00 .. 6.00\text{ L/min}$ | Current instantaneous flow rate. |
| `delivered_volume_ml`| `INT` | $\ge 0\text{ mL}$ | Delivered volume in current actuation cycle. |
| `sample_window_ms` | `INT` | $> 0$ (default $1000\text{ms}$) | Measurement snapshot window width. |
| `sensor_calibration_id`| `INT` | FK $\to$ `sensor_calibrations.id` | Mandatory active calibration profile ID. |
| `flow_confirmed` | `BOOLEAN` | `TRUE` / `FALSE` | `TRUE` if flow satisfies $[\text{min\_flow}, \text{max\_flow}]$. |
| `flow_stability_pct` | `NUMERIC(5,2)` | $0.00 .. 100.00\%$ | Quantitative stability index of fluid stream. |
| `quality_flag` | `VARCHAR(16)` | `'OK'`, `'SUSPECT'`, `'INVALID'` | Measurement quality indicator. |
| `is_fault` | `BOOLEAN` | `TRUE` / `FALSE` | Fail-safe fault indicator. |
| `fault_code` | `VARCHAR(32)` | Enum | `'NONE'`, `'NO_FLOW_FAULT'`, `'UNEXPECTED_FLOW_FAULT'`, `'OVER_RANGE_FAULT'`, `'SENSOR_FAULT'` |
| `boot_session_id` | `INT` | `1 .. 0xFFFFFFFF` | Node boot session ID. |
| `rf_seq` | `INT` | `0 .. 65535` | Frame sequence number. |
| `node_timestamp_ms` | `BIGINT` | Node uptime ms | Dual timestamp: Node clock. |
| `gateway_timestamp_ms`| `BIGINT` | Gateway uptime ms | Dual timestamp: Gateway clock. |

---

## 3. Quantitative Analytics & Metric Formulas

The Analytics Engine computes the following mathematical metrics per node and across the entire 4-node cluster:

### 3.1 Command-to-ACK Latency ($T_{\text{cmd\_to\_ack}}$)
Turnaround time from Gateway command dispatch to valid authenticated `COMMAND_ACK` receipt:
$$T_{\text{cmd\_to\_ack}} = T_{\text{gateway\_ack\_rx}} - T_{\text{gateway\_dispatch}}$$
- **Operational Target:** $p50 \le 180\text{ms}$, $p95 \le 250\text{ms}$, $p99 \le 750\text{ms}$ (with retries).

### 3.2 Flow-Start Latency ($T_{\text{flow\_start}}$)
Delay between command dispatch and arrival of the first confirmed fluid flow telemetry:
$$T_{\text{flow\_start}} = T_{\text{flow\_confirmed}} - T_{\text{gateway\_dispatch}}$$
- **Safety Ceiling:** Must be $< \text{flow\_start\_timeout\_ms}$ (typically $3000\text{ms}$); otherwise triggers `NO_FLOW_FAULT`.

### 3.3 Flow Confirmation Rate ($\eta_{\text{confirm}}$)
Ratio of successfully flow-confirmed actuation cycles relative to dispatched ON commands:
$$\eta_{\text{confirm}} = \frac{N_{\text{FLOW\_CONFIRMED}}}{N_{\text{ON\_DISPATCHED}}} \times 100\%$$
- **Reliability Target:** $\ge 99.0\%$ under nominal operating conditions.

### 3.4 Actual Runtime Fidelity ($\Delta T_{\text{runtime}}$)
Discrepancy between commanded lease duration and actual physical execution time:
$$T_{\text{runtime\_actual}} = T_{\text{flow\_end}} - T_{\text{flow\_start}}$$
$$\Delta T_{\text{runtime}} = T_{\text{runtime\_actual}} - T_{\text{commanded\_lease}}$$

### 3.5 Delivered Volume per Cycle ($V_{\text{cycle}}$)
Accumulated liquid volume dispensed during an active spray cycle:
$$V_{\text{cycle}} = \sum_{k=1}^{M} \Delta V_k = \frac{\Delta \text{pulses}_{\text{cycle}}}{K_{\text{factor}}}$$

### 3.6 Flow Stability Index ($\text{Stability}_{\text{pct}}$)
Quantifies steady-state spray uniformity during an active irrigation cycle using the Coefficient of Variation ($CV_Q$):
$$\mu_Q = \frac{1}{M} \sum_{k=1}^M Q_k, \quad \sigma_Q = \sqrt{\frac{1}{M} \sum_{k=1}^M (Q_k - \mu_Q)^2}$$
$$\text{Stability}_{\text{pct}} = \max\left(0.00\%, \left(1.00 - \frac{\sigma_Q}{\mu_Q}\right) \times 100\%\right)$$
- **Acceptable Bound:** $\ge 85.00\%$ for stable atomizing nozzles.

### 3.7 Packet Loss Ratio ($P_{\text{loss}}$) & Retry Rate ($R_{\text{retry}}$)
Measures wireless channel degradation and interference:
$$P_{\text{loss}} = \frac{N_{\text{timed\_out\_commands}}}{N_{\text{total\_commands\_sent}}} \times 100\%$$
$$R_{\text{retry}} = \frac{N_{\text{retransmitted\_frames}}}{N_{\text{total\_frames\_sent}}} \times 100\%$$

### 3.8 Schedule-vs-Override Mismatch Metric
Tracks duration and occurrence count where autonomous MEGA8 schedule was actively superseded by a manual or fail-safe override:
$$N_{\text{mismatch}} = \sum \mathbf{1}_{(\text{override\_state} \ne \text{'NONE'})}$$
$$T_{\text{mismatch\_duration}} = \sum \Delta t_{\text{override\_active}}$$

### 3.9 Staleness Duration & Fault Lockout Rate
$$T_{\text{stale\_total}} = \sum (T_{\text{resync}} - T_{\text{stale\_threshold\_exceeded}})$$
$$\text{Fault\_Rate} = \frac{N_{\text{fault\_latched}}}{N_{\text{irrigation\_cycles}}} \times 100\%$$

---

## 4. MQTT Topic & JSON Serialization Contract

### 4.1 Normalized Telemetry (`aeroponics/device/{device_id}/telemetry/node/{node_id}`)
```json
{
  "timestamp": "2026-08-29T06:55:00.123Z",
  "node_id": 2,
  "group_id": 1,
  "command_id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  "state": {
    "desired": "ON",
    "reported": "ON",
    "schedule_state": "SPRAYING",
    "override_state": "NONE"
  },
  "feedback": {
    "driver_sense": 1,
    "load_sense": 1,
    "driver_mismatch": false,
    "current_ma": 1950,
    "voltage_v": 12.15,
    "fault_flags": 0
  },
  "flow": {
    "flow_lpm": 2.45,
    "delivered_volume_ml": 820,
    "total_litres": 142.350,
    "pulse_count": 68420,
    "flow_confirmed": true,
    "stability_pct": 96.2,
    "quality": "OK"
  },
  "diagnostics": {
    "boot_session_id": 42,
    "rf_seq": 1054,
    "node_uptime_s": 84200,
    "gateway_timestamp_ms": 12450890,
    "node_timestamp_ms": 84200120
  }
}
```

### 4.2 Analytics Summary (`aeroponics/device/{device_id}/telemetry/node/{node_id}/analytics`)
```json
{
  "timestamp": "2026-08-29T06:55:00.123Z",
  "node_id": 2,
  "season_id": 1,
  "metrics": {
    "total_commands": 150,
    "commands_acked": 148,
    "commands_timed_out": 2,
    "on_commands": 75,
    "flow_confirmed_count": 74,
    "confirmation_rate_pct": 98.67,
    "total_runtime_s": 2250,
    "total_volume_litres": 112.50,
    "latency": {
      "cmd_to_ack_avg_ms": 178,
      "cmd_to_ack_min_ms": 48,
      "cmd_to_ack_max_ms": 706,
      "flow_start_avg_ms": 420,
      "flow_start_min_ms": 380,
      "flow_start_max_ms": 650
    },
    "transport": {
      "frames_sent": 210,
      "retries_count": 8,
      "retry_rate_pct": 3.81,
      "packet_loss_pct": 0.95,
      "crc_errors": 0,
      "auth_errors": 0
    },
    "quality": {
      "flow_stability_avg_pct": 95.8,
      "schedule_override_mismatches": 3,
      "stale_events_count": 1,
      "stale_total_duration_s": 15,
      "fault_lockouts_count": 0
    }
  }
}
```

---

## 5. Architectural Verification & Sign-off Matrix

| Verification Aspect | Method | Expected Outcome | Status |
|---|---|---|---|
| **Zero Raw RF Frame Persistence** | Firmware Code Audit & Host Test | Raw frame buffers stripped before normalizer output; TimescaleDB receives only normalized domain columns. | ✅ PASS |
| **Command-to-ACK Latency** | Synthetic Turnaround Test | Accurately calculates $T_{\text{ack}} - T_{\text{dispatch}}$ across nominal ($178\text{ms}$) and retry ($706\text{ms}$) scenarios. | ✅ PASS |
| **Flow-Start Latency** | Safety FSM Integration Test | Accurately measures elapsed time to flow confirmation; flags timeout if $> \text{threshold}$. | ✅ PASS |
| **Confirmation Rate Calculation** | Multi-Cycle Statistical Test | Ratio computed with zero-division protection; accounts for nominal vs faulted cycles. | ✅ PASS |
| **Delivered Volume & Stability** | Piecewise Interpolation & $CV_Q$ Math | Computes cycle volume and flow stability index with precision $\ge 0.1\%$. | ✅ PASS |
| **Schedule vs Override Mismatch** | State Tracker Evaluation | Detects and records manual override durations and reason codes (`OVERRIDE_EXPIRED`, etc.). | ✅ PASS |
| **Multi-Node Isolation** | 4-Node Registry Test | Independent metric accumulators and stats across Node 1, Node 2, Node 3, and Node 4. | ✅ PASS |
| **JSON Serialization Parity** | Schema & Parser Parity Check | Generated JSON conforms 100% with MQTT topic specs and TimescaleDB ingestion tables. | ✅ PASS |
