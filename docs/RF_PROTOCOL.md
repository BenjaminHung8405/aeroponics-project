# Aeroponics RF 433 MHz Wire Protocol Specification (Version 1.0)

> **Document Status:** Official Wire Contract (Specification & Test Vectors)
> **Target Hardware:** ESP32 RF Gateway ↔ 12 Remote Pump Nodes (433 MHz Transceiver via UART)

---

## 1. Frame Structure & Byte Layout

All multibyte integers are transmitted in **Little-Endian** order.
Frames include both a 16-byte HMAC-SHA256 authentication tag (`mac[16]`) and a trailing 2-byte CRC-16 check sequence for dual integrity & authenticity enforcement.

```text
+----------+------------+----------+------------+------------+--------------+----------+------------+-------------+----------------+----------+---------+
| SOF (2B) | Ver (1B)   | Msg (1B) | Target(1B) | Source(1B) | Session (4B) | Seq (2B) | Cmd ID (4B)| PayloadLen  | Payload (0..64)| MAC (16B) | CRC(2B) |
+----------+------------+----------+------------+------------+--------------+----------+------------+-------------+----------------+----------+---------+
| 0xAA 0x55| 0x01       | Enum     | 0..12      | 0..12      | uint32_t     | uint16_t | uint32_t   | 0..64       | Raw bytes      | HMAC-256 | CRC-16  |
+----------+------------+----------+------------+------------+--------------+----------+------------+-------------+----------------+----------+---------+
```

### 1.1 Header & Footer Field Definitions

| Field | Size (Bytes) | Range / Value | Description |
|---|---|---|---|
| **SOF (Start of Frame)** | 2 | `0xAA 0x55` | Fixed 2-byte preamble for frame synchronization. |
| **Protocol Version** | 1 | `0x01` | Protocol version identifier (must match `0x01`). |
| **Message Type** | 1 | `0x01 .. 0x07` | Numeric enum identifying frame payload schema. |
| **Target Node ID** | 1 | `0` (GW), `1..12` (Nodes) | Destination node address (0 = Gateway). |
| **Source Node ID** | 1 | `0` (GW), `1..12` (Nodes) | Originator node address. |
| **Gateway / Node Boot Session ID** | 4 | `0x00000001 .. 0xFFFFFFFF` | Persisted counter incremented at boot. Used for anti-replay and reboot detection. |
| **Sequence Number** | 2 | `0x0000 .. 0xFFFF` | Monotonically increasing sequence per session. Wraps back to `0x0000`. |
| **Command ID** | 4 | `uint32_t` | Unique command correlation ID assigned by Gateway. |
| **Payload Length** | 1 | `0 .. 64` | Byte count of payload field (max 64 bytes). |
| **Payload** | *Length* | Var (max 64B) | Payload data specific to Message Type. |
| **MAC (Message Auth Code)** | 16 | `uint8_t[16]` | First 128 bits of HMAC-SHA256 calculated over Header + Payload using a provisioned 16-byte PSK. |
| **CRC-16** | 2 | `uint16_t` | Frame check sequence (CRC-16/CCITT-FALSE calculated over Header + Payload + MAC). |

### 1.2 Canonical Byte-Level Test Vectors

These vectors are normative and are tested at byte level. `MAC` is calculated
over the canonical serialized bytes and the CRC is calculated over those same
bytes plus `MAC`; neither calculation may use a C/C++ object representation.

```text
Header: SET_PUMP, target=2, source=0, session=0x11223344,
        seq=0x5566, command_id=0x778899AA, payload_len=9
AA 55 01 03 02 00 44 33 22 11 66 55 AA 99 88 77 09

SET_PUMP: ON, lease=0x11223344, max_on=0x55667788
01 44 33 22 11 88 77 66 55

COMMAND_ACK: seq=0x1234, SUCCESS, reported=ON, driver=ON
34 12 00 01 01 00 00 00

TELEMETRY: reported=ON, driver=OFF, flow=0x1234, volume=0x55667788,
           pulses=0x99AABBCC, flags=0x05, last_command_id=0xDDEEFF00
01 00 34 12 88 77 66 55 CC BB AA 99 05 00 FF EE DD

FAULT_REPORT: code=3, timestamp=0x01020304, reserved=0, command_id=0xA1B2C3D4
03 04 03 02 01 00 D4 C3 B2 A1
```

---

## 2. Integrity & Cryptographic Security (HMAC-SHA256 & CRC-16)

### 2.0 Shared Codec Boundary

`RfFrameCodec` is a pure C++ shared module used by both gateway and node
firmware. Its frame-encode API receives the full `source_node_id`,
`target_node_id`, `boot_session_id`, `sequence`, `command_id`, message type,
and typed payload before calculating MAC and CRC. A node therefore creates
`COMMAND_ACK`, `TELEMETRY`, `HEARTBEAT`, and `FAULT_REPORT` directly as
node-to-gateway frames (`source=node_id`, `target=0`) using its own boot
session. No caller may modify raw header bytes after authentication or repair
MAC/CRC manually; integration and host tests use the same node codec API.

### 2.1 HMAC-SHA256 Specification & Test Vectors
- **Key Provisioning:** A unique 16-byte PSK is provisioned through `rf_config` and is never tracked in Git, logged, or included in evidence. Missing/read-invalid provisioning disables RF transmit and receive paths. This contract does **not** claim NVS encryption.
- **HMAC Truncation:** First 16 bytes (128 bits) of SHA-256 HMAC output.
- **Constant-Time Verification:** Receivers MUST use constant-time byte comparison (`constantTimeCompare`) to prevent timing side-channel attacks.

### 2.2 CRC-16/CCITT-FALSE Specification
- **Algorithm:** CRC-16 / CCITT-FALSE
- **Polynomial:** `0x1021` ($x^{16} + x^{12} + x^5 + 1$)
- **Initial Value:** `0xFFFF`
- **RefIn / RefOut:** `false`
- **XorOut:** `0x0000`
- **Test Vector:** ASCII `"123456789"` $\rightarrow$ `0x29B1`.

---

## 3. Anti-Replay & Session Semantics

1. Each node and gateway tracks the `last_boot_session_id` and `last_sequence_num` for every remote peer.
2. A frame is ACCEPTED if:
   - `boot_session_id > last_boot_session_id` (peer rebooted, update session and reset expected sequence), OR
   - `boot_session_id == last_boot_session_id` AND its modulo-65536 serial distance from `last_sequence_num` is in `1..32767` (valid monotonic progression, including wrap).
3. A frame is REJECTED if `boot_session_id < last_boot_session_id`, is duplicate, or is at/behind the bounded serial window. Gateway boot session is persisted/rotated as `uint32_t` in NVS; NVS failure or `uint32_t` exhaustion fail-closes RF pending explicit credential rotation/factory reset.
4. A retry is a retransmission of the exact original wire frame: identical `gateway_boot_session_id`, `sequence`, `command_id`, payload, MAC, and CRC bytes. A sender increments `sequence` only when it creates a new logical command. Nodes cache the terminal outcome by `{gateway_boot_session_id, sequence, command_id}` and, on a duplicate, return the cached `COMMAND_ACK` without actuating or extending a lease.
5. **Gateway feedback correlation policy:** For each node boot session, the gateway admits a non-zero telemetry `last_command_id` or fault `command_id` only when it equals the currently dispatched gateway command for that node and session. A valid MAC/CRC frame that fails this semantic correlation is discarded completely: it cannot change reported state, driver feedback, flow/volume, `last_seen_ms`, or the outcome of another command. `command_id = 0` is reserved for an autonomous/no-command node condition and is processed under the normal safety policy. A node boot-session transition invalidates every prior feedback correlation, cancels any prior pending command, and queues a new explicit safe-OFF command for the new session.

---

## 4. Numeric Enums

### 4.1 Message Types (`message_type_t`)
```cpp
enum class MessageType : uint8_t {
    PING            = 0x01,  // Health check query
    PONG            = 0x02,  // Health check response
    SET_PUMP        = 0x03,  // Actuator control command with lease
    COMMAND_ACK     = 0x04,  // End-to-end command outcome response
    TELEMETRY       = 0x05,  // Periodic pump & flow sensor state update
    HEARTBEAT       = 0x06,  // Node alive heartbeat signal
    FAULT_REPORT    = 0x07   // Asynchronous fault notification from node
};
```

### 4.2 Desired / Reported Pump State
```cpp
enum class PumpState : uint8_t {
    OFF = 0x00,
    ON  = 0x01
};
```

### 4.3 ACK Outcome (`ack_outcome_t`)
```cpp
enum class AckOutcome : uint8_t {
    SUCCESS                 = 0x00,  // Command accepted & queued/executed
    REJECTED_INVALID_LEASE  = 0x01,  // Run lease ms invalid or zero
    REJECTED_AUTH_FAIL      = 0x02,  // MAC/HMAC failure or bad boot session
    FAULT_LOCKOUT           = 0x03,  // Node in active fault state (e.g. no flow)
    REJECTED_UNKNOWN_NODE   = 0x04   // Node ID mismatch
};
```

---

## 5. Payload Schemas

### 5.1 `PING` Payload (Type `0x01`) — Size: 4 Bytes
| Offset | Field | Type | Description |
|---|---|---|---|
| 0 | `ping_timestamp_ms` | `uint32_t` | Sender system timestamp in ms. |

### 5.2 `PONG` Payload (Type `0x02`) — Size: 4 Bytes
| Offset | Field | Type | Description |
|---|---|---|---|
| 0 | `echo_timestamp_ms` | `uint32_t` | Echoed timestamp from PING request. |

### 5.3 `SET_PUMP` Payload (Type `0x03`) — Size: 9 Bytes
| Offset | Field | Type | Description |
|---|---|---|---|
| 0 | `desired_state` | `uint8_t` | `0x00` (OFF) or `0x01` (ON) |
| 1 | `run_lease_ms` | `uint32_t` | Mandatory lease duration in ms (node auto-off after expiry). |
| 5 | `max_on_duration_ms` | `uint32_t` | Maximum hard safety timeout for pump ON state. |

### 5.4 `COMMAND_ACK` Payload (Type `0x04`) — Size: 8 Bytes
| Offset | Field | Type | Description |
|---|---|---|---|
| 0 | `ack_sequence` | `uint16_t` | Sequence number of the command being ACKed. |
| 2 | `ack_outcome` | `uint8_t` | Outcome code (`ack_outcome_t`). |
| 3 | `reported_pump_state`| `uint8_t` | Node reported state (`0x00` = OFF, `0x01` = ON). |
| 4 | `driver_feedback` | `uint8_t` | Physical driver sense feedback (`0x00` = LOW, `0x01` = HIGH). |
| 5 | `reserved` | `uint8_t[3]` | Reserved alignment padding (set to 0). |

`COMMAND_ACK` is command-receipt/outcome evidence only. The gateway MUST NOT treat its state fields as pump/driver/flow confirmation; those control-state fields are updated only from correlated `TELEMETRY`.

### 5.5 `TELEMETRY` Payload (Type `0x05`) — Size: 17 Bytes
| Offset | Field | Type | Description |
|---|---|---|---|
| 0 | `reported_pump_state`| `uint8_t` | Node reported state (`0x00` = OFF, `0x01` = ON). |
| 1 | `driver_feedback` | `uint8_t` | Driver feedback (`0x00` = LOW, `0x01` = HIGH). |
| 2 | `flow_lpm_x100` | `uint16_t` | Quantitative flow rate in L/min x 100 (e.g. 520 = 5.20 L/min). |
| 4 | `delivered_volume_ml`| `uint32_t` | Cumulative delivered volume in milliliters. |
| 8 | `pulse_count` | `uint32_t` | Raw cumulative pulse count from flow sensor ISR. |
| 12| `fault_flags` | `uint8_t` | Bit 0: NO_FLOW, Bit 1: UNEXPECTED_FLOW, Bit 2: LEASE_EXPIRED. |
| 13| `last_command_id` | `uint32_t` | Mandatory correlation key of last received SET_PUMP command (0 if none). A non-zero value is applied only when it matches the gateway's active command correlation for this node boot session. |

### 5.6 `HEARTBEAT` Payload (Type `0x06`) — Size: 6 Bytes
| Offset | Field | Type | Description |
|---|---|---|---|
| 0 | `uptime_s` | `uint32_t` | Node system uptime in seconds. |
| 4 | `rssi_dbm` | `int8_t` | RF link signal strength RSSI. |
| 5 | `battery_percent` | `uint8_t` | Battery level 0..100% (or 255 if AC powered). |

### 5.7 `FAULT_REPORT` Payload (Type `0x07`) — Size: 10 Bytes
| Offset | Field | Type | Description |
|---|---|---|---|
| 0 | `fault_code` | `uint8_t` | Fault classification code (1=NO_FLOW, 2=UNEXPECTED_FLOW, 3=LEASE_EXPIRED, 4=HARDWARE_MISMATCH). |
| 1 | `timestamp_ms` | `uint32_t` | Time of fault occurrence. |
| 5 | `reserved` | `uint8_t` | Alignment padding. |
| 6 | `command_id` | `uint32_t` | Correlation key of command during which fault occurred (0 if autonomous). A non-zero value is acted on only when it matches the gateway's active command correlation for this node boot session. |

---

## 6. Timing, Retry, Staleness & Recovery Contracts

- **Max RX Buffer:** 256 bytes.
- **Max Payload Size:** 64 bytes.
- **Inter-Byte Timeout:** 50 ms (partial frame byte reception timeout).
- **Heartbeat Interval:** 5000 ms (Node transmits HEARTBEAT frame every 5s when idle).
- **Telemetry Rates:**
  - **PUMP ON:** Every 1000 ms.
  - **PUMP OFF:** Every 10000 ms.
  - **FAULT / STATE CHANGE:** Immediate asynchronous transmission.
- **Stale Threshold:** 15000 ms (Gateway marks node `STALE` if no telemetry or heartbeat received within 15s).
- **Stale Fail-Safe Policy:**
  - When gateway evaluates node as `STALE`, node's `desired_state` is set to `OFF`, `fault_latched` is set to `true`, pending commands are canceled, and gateway issues audit log `STALE_SAFE_OFF`.
  - Node actuators MUST boot with output forced `OFF` before application layer initializes.
  - If a node reboots or reconnects after being `STALE`, it transmits a new `boot_session_id`. The gateway detects session change, invalidates previous sequence state, and sends an explicit `SET_PUMP(OFF)` frame to ensure node remains safely off until reset.
- **RF Command Retry Limit:** Maximum 3 retries per pending command.
- **Retry Backoff Interval:** 1000 ms between retries.
- **Terminal Timeout Action:** On 3rd retry timeout, Gateway marks pending command `TIMED_OUT` and node status `FAULT`. Node pump stays/forces OFF.
- **Command Evidence FSM:** Gateway keeps the immutable command/correlation pending through
  `AWAITING_ACK` → `AWAITING_PUMP_FEEDBACK` → `AWAITING_FLOW_CONFIRMATION` for an ON
  command. A successful ACK publishes `RF_ACKED` only. A correlated telemetry report with
  `reported_pump_state=ON` and `driver_feedback=ON` publishes `PUMP_FEEDBACK_ON` only.
  Gateway publishes `COMPLETED` exclusively at `FLOW_CONFIRMED`, when a correlated ON
  telemetry report is fault-free, within `flow_start_timeout_ms`, and its flow is at least the
  commissioned node/treatment/calibration `min_flow_lpm_x100`.
- **MQTT ACK Lifecycle:** `aeroponics/device/{device_id}/ack/{command_id}` carries exactly one
  command-admission decision (`ACCEPTED` or `REJECTED`) and is never reused for RF progression.
  RF lifecycle observations (`QUEUED`, `RF_ACKED`, `PUMP_FEEDBACK_ON`, `COMPLETED`, or terminal
  fault/cancel outcomes) are ordered, non-retained events on
  `aeroponics/device/{device_id}/telemetry/command/{command_id}/event`. Consumers must treat the
  ACK as acceptance only; ON success remains evidenced solely by the FSM flow confirmation.
- **Flow Policy & Faults:** `min_flow_lpm_x100`, `max_off_flow_lpm_x100`,
  `max_flow_lpm_x100`, `flow_start_timeout_ms`, lease limits, `policy_version`,
  `treatment_version_id`, and `calibration_id` are provisioned per node by the authenticated
  control-plane `config/flow-policy` command. A node starts with no flow or lease policy and
  rejects every ON request until both valid policies are present; there are no production fallback
  thresholds. The approved flow sensor physical range is **0.00–6.00 L/min**: every provisioned
  threshold must be `<= 600` x100, and telemetry above `600` x100 is invalid/faulted regardless
  of the provisioned threshold. Flow at
  or beyond the start deadline without confirmation latches `NO_FLOW_FAULT`; flow above the
  configured maximum, any telemetry `fault_flags`, or OFF flow above `max_off_flow_lpm_x100`
  latches the relevant fault (`UNEXPECTED_FLOW_FAULT` for the latter). Gateway safe-offs and
  queues one internal `SET_PUMP(OFF)` command; it never publishes `COMPLETED` on these paths.
- **Node Lease Fail-Safe:** Nodes MUST auto-off pump if no valid lease or lease expires (`LEASE_EXPIRED_SAFE_OFF`).
- **PSK Provisioning & Rotation Policy:**
  - PSK key (16 bytes) is provisioned into NVS manufacturing partition `rf_config/psk_word_0` … `rf_config/psk_word_3`.
  - Boot session ID is persisted/incremented in `rf_config/boot_session`.
  - Current repository configuration has **no approved evidence** for encrypted NVS, Flash Encryption, Secure Boot, or a factory procedure that protects PSK material. This risk is **not accepted for production**.
  - Release firmware remains RF fail-closed unless an independent security evidence package explicitly supplies `RF_PROVISIONING_INDEPENDENT_SIGNOFF=1`. This non-secret flag must not be set merely to bypass the gate.
  - Required sign-off evidence: encrypted-NVS key management, Flash Encryption and Secure Boot enablement, a factory write procedure that does not log/export PSK, and a release audit confirming no fallback key. Until then, PSK rotation is physical factory work only; no encrypted-NVS update command is implemented or claimed.
