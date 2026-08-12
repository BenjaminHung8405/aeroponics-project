# Aeroponics RF 433 MHz Wire Protocol Specification (Version 1.0)

> **Document Status:** Official Wire Contract (Specification & Test Vectors)
> **Target Hardware:** ESP32 RF Gateway ↔ 12 Remote Pump Nodes (433 MHz Transceiver via UART)

---

## 1. Frame Structure & Byte Layout

All multibyte integers are transmitted in **Little-Endian** order.
Frames include both a 16-byte HMAC-SHA256 authentication tag (`mac[16]`) and a trailing 2-byte CRC-16 check sequence for dual integrity & authenticity enforcement.

```text
+----------+------------+----------+------------+------------+--------------+----------+------------+-------------+----------------+----------+---------+
| SOF (2B) | Ver (1B)   | Msg (1B) | Target(1B) | Source(1B) | Session (2B) | Seq (2B) | Cmd ID (4B)| PayloadLen  | Payload (0..64)| MAC (16B) | CRC(2B) |
+----------+------------+----------+------------+------------+--------------+----------+------------+-------------+----------------+----------+---------+
| 0xAA 0x55| 0x01       | Enum     | 0..12      | 0..12      | uint16_t     | uint16_t | uint32_t   | 0..64       | Raw bytes      | HMAC-256 | CRC-16  |
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
| **Boot Session ID** | 2 | `0x0001 .. 0xFFFF` | Session counter generated at boot. Used for anti-replay and reboot detection. |
| **Sequence Number** | 2 | `0x0000 .. 0xFFFF` | Monotonically increasing sequence per session. Wraps back to `0x0000`. |
| **Command ID** | 4 | `uint32_t` | Unique command correlation ID assigned by Gateway. |
| **Payload Length** | 1 | `0 .. 64` | Byte count of payload field (max 64 bytes). |
| **Payload** | *Length* | Var (max 64B) | Payload data specific to Message Type. |
| **MAC (Message Auth Code)** | 16 | `uint8_t[16]` | First 128 bits of HMAC-SHA256 calculated over Header + Payload using a provisioned 16-byte PSK. |
| **CRC-16** | 2 | `uint16_t` | Frame check sequence (CRC-16/CCITT-FALSE calculated over Header + Payload + MAC). |

---

## 2. Integrity & Cryptographic Security (HMAC-SHA256 & CRC-16)

### 2.1 HMAC-SHA256 Specification & Test Vectors
- **Key Provisioning:** A unique 16-byte PSK is provisioned through the manufacturing NVS boundary and is never tracked in Git, logged, or included in evidence. Missing/read-invalid provisioning disables RF transmit and receive paths.
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
3. A frame is REJECTED if `boot_session_id < last_boot_session_id`, is duplicate, or is at/behind the bounded serial window. Gateway boot session is persisted/rotated in NVS; NVS failure fail-closes RF.

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

### 5.5 `TELEMETRY` Payload (Type `0x05`) — Size: 13 Bytes
| Offset | Field | Type | Description |
|---|---|---|---|
| 0 | `reported_pump_state`| `uint8_t` | Node reported state (`0x00` = OFF, `0x01` = ON). |
| 1 | `driver_feedback` | `uint8_t` | Driver feedback (`0x00` = LOW, `0x01` = HIGH). |
| 2 | `flow_lpm_x100` | `uint16_t` | Quantitative flow rate in L/min x 100 (e.g. 520 = 5.20 L/min). |
| 4 | `delivered_volume_ml`| `uint32_t` | Cumulative delivered volume in milliliters. |
| 8 | `pulse_count` | `uint32_t` | Raw cumulative pulse count from flow sensor ISR. |
| 12| `fault_flags` | `uint8_t` | Bit 0: NO_FLOW, Bit 1: UNEXPECTED_FLOW, Bit 2: LEASE_EXPIRED. |

### 5.6 `HEARTBEAT` Payload (Type `0x06`) — Size: 6 Bytes
| Offset | Field | Type | Description |
|---|---|---|---|
| 0 | `uptime_s` | `uint32_t` | Node system uptime in seconds. |
| 4 | `rssi_dbm` | `int8_t` | RF link signal strength RSSI. |
| 5 | `battery_percent` | `uint8_t` | Battery level 0..100% (or 255 if AC powered). |

### 5.7 `FAULT_REPORT` Payload (Type `0x07`) — Size: 6 Bytes
| Offset | Field | Type | Description |
|---|---|---|---|
| 0 | `fault_code` | `uint8_t` | Fault classification code (1=NO_FLOW, 2=UNEXPECTED_FLOW, 3=LEASE_EXPIRED, 4=HARDWARE_MISMATCH). |
| 1 | `timestamp_ms` | `uint32_t` | Time of fault occurrence. |
| 5 | `reserved` | `uint8_t` | Alignment padding. |

---

## 6. Timing, Retry & Safety Contracts

- **Max RX Buffer:** 256 bytes.
- **Max Payload Size:** 64 bytes.
- **Inter-Byte Timeout:** 50 ms (partial frame byte reception timeout).
- **RF Command Retry Limit:** Maximum 3 retries per pending command.
- **Retry Backoff Interval:** 1000 ms between retries.
- **Terminal Timeout Action:** On 3rd retry timeout, Gateway marks pending command `TIMED_OUT` and node status `FAULT`. Node pump stays/forces OFF.
- **Node Lease Fail-Safe:** Nodes MUST auto-off pump if no valid lease or lease expires (`LEASE_EXPIRED_SAFE_OFF`).
