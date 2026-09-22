# Hợp đồng Giao tiếp Tầng Mạng

## Interface & Wire Contract

**Dự án:** Aeroponics Lab  
**Phạm vi use case:** `UC-GW-01`, `UC-GW-04`, `UC-GW-05`, `UC-GW-06`, `UC-BE-11`, `UC-BE-12`, `UC-BE-14`  
**Phiên bản:** 1.0  
**Trạng thái:** Normative integration contract  
**Nguồn sự thật:** [`RF_PROTOCOL.md`](./RF_PROTOCOL.md), [`STATE_MACHINE_MATRIX.md`](./STATE_MACHINE_MATRIX.md), [`TELEMETRY_ANALYTICS_CONTRACT.md`](./TELEMETRY_ANALYTICS_CONTRACT.md), [`RF_FLOW_POC_DECISION.md`](./RF_FLOW_POC_DECISION.md), [`SPRINT_3_HANDOFF_PACKAGE.md`](./SPRINT_3_HANDOFF_PACKAGE.md), `aeroponics-firmware/include/agu_legacy_codec.h`, `aeroponics-firmware/src/agu_legacy_codec.cpp`

## 1. Quy ước trạng thái đặc tả

| Nhãn | Ý nghĩa |
|---|---|
| **NORMATIVE** | Quy tắc bắt buộc đối với mọi implementation tương thích. |
| **IMPLEMENTED** | Đã có implementation trong repository được dẫn chiếu. |
| **VERIFIED** | Có test/evidence tương ứng trong tài liệu hoặc test suite. |
| **ASSUMPTION** | Giả định tích hợp, không được dùng để suy ra byte trên wire. |
| **TBD** | Chưa đủ evidence để khóa hợp đồng. |
| **PRODUCTION BLOCKER** | Không được tuyên bố production-ready cho đến khi có evidence độc lập. |

Tài liệu này không hợp nhất hai giao thức thành một frame. Production RF là một lớp authenticated protocol; AGU Legacy là một lớp codec tương thích Delphi/ATmega8 nằm sau gateway adapter.

## 2. Ranh giới kiến trúc

```text
Backend / MQTT
    -> semantic command, command_id, admission ACK
ESP32-S3 Gateway
    -> production RF codec: HMAC, CRC, session, sequence
    -> semantic translation / stateful safety proxy
    -> AguLegacyCodec: Length/Opcode/Params/Checksum
ATmega8 / AGU legacy node
    -> relay or MOSFET output, legacy response
    -> gateway feedback and flow evidence
```

Gateway là chủ sở hữu FSM runtime, lease, timeout/retry, correlation và safe-off. Node legacy chỉ xử lý transaction AGU, relay và EEPROM/configuration. Raw RF/AGU bytes là transport data tạm thời; không được persist vào MQTT, database hay audit business record.

## 3. Production RF 433 MHz

### 3.1 Frame layout

**NORMATIVE / IMPLEMENTED:** mọi số nhiều byte là little-endian. `SOF` không nằm trong vùng tính HMAC/CRC.

```text
[SOF 2B][Version 1B][MessageType 1B][TargetNodeId 1B]
[SourceNodeId 1B][BootSessionId 4B][Sequence 2B][CommandId 4B]
[PayloadLength 1B][Payload 0..64B][HMAC-16 16B][CRC16 2B]
```

| Offset tương đối | Trường | Kích thước | Giá trị / quy tắc |
|---:|---|---:|---|
| 0 | `SOF` | 2 | `AA 55` |
| 2 | `version` | 1 | `0x01` |
| 3 | `message_type` | 1 | `0x01..0x07` |
| 4 | `target_node_id` | 1 | `0` gateway, production node `1..4`; `5..12` chưa acceptance |
| 5 | `source_node_id` | 1 | `0` gateway, node ID tương ứng |
| 6 | `boot_session_id` | 4 | `uint32_t`, counter tăng khi boot |
| 10 | `sequence` | 2 | `uint16_t`, serial sequence theo peer/session |
| 12 | `command_id` | 4 | Correlation ID do gateway cấp |
| 16 | `payload_length` | 1 | `0..64` byte |
| 17 | `payload` | 0..64 | Theo message type |
| sau payload | `hmac` | 16 | 16 byte đầu của HMAC-SHA256 |
| cuối | `crc` | 2 | CRC-16/CCITT-FALSE |

Message types: `0x01 PING`, `0x02 PONG`, `0x03 SET_PUMP`, `0x04 COMMAND_ACK`, `0x05 TELEMETRY`, `0x06 HEARTBEAT`, `0x07 FAULT_REPORT`.

### 3.2 HMAC và CRC

**NORMATIVE target:**

```text
hmac_input = [Version .. Payload]
hmac = first_16_bytes(HMAC-SHA256(psk_16, hmac_input))
crc_input = [Version .. Payload][HMAC-16]
crc = CRC16_CCITT_FALSE(crc_input)
```

`SOF` không tham gia HMAC hoặc CRC. CRC có polynomial `0x1021`, initial `0xFFFF`, `RefIn=false`, `RefOut=false`, `XorOut=0x0000`; vector chuẩn ASCII `123456789` cho kết quả `0x29B1`. Receiver phải so sánh HMAC constant-time và reject fail-closed khi key thiếu, MAC sai, CRC sai, version sai, length sai hoặc node ID ngoài allow-list.

**IMPLEMENTED drift cần xử lý:** `RfFrameCodec::encodeFrame()` hiện tính HMAC và CRC trên buffer bắt đầu từ offset `0` với độ dài `RF_HEADER_SIZE + payload_len`, nên `SOF` đang được đưa vào cả hai phép tính. Đây là khác biệt so với target contract `[Version..Payload]`. Cho đến khi có quyết định và test vector thống nhất giữa gateway/node, đây là **PRODUCTION BLOCKER**; không được triển khai hai cách tính song song.

PSK là 16 byte, lưu trong manufacturing partition `rf_config`, không commit/log/persist ra telemetry. Repository chưa có bằng chứng độc lập cho encrypted NVS, Flash Encryption, Secure Boot và factory provisioning kiểm soát PSK. Đây là **PRODUCTION BLOCKER** cho đến khi có `RF_PROVISIONING_INDEPENDENT_SIGNOFF=1`; không được coi HMAC là confidentiality. Muốn mã hóa payload phải đổi protocol version và chọn AEAD riêng.

### 3.3 Payload production

`SET_PUMP` có đúng 9 byte:

```text
offset 0: desired_state       uint8 (OFF=0, ON=1)
offset 1: run_lease_ms        uint32 LE
offset 5: max_on_duration_ms  uint32 LE
```

`run_lease_ms` production hợp lệ trong khoảng `1000..300000`; override semantic-level có `override_duration_ms` trong khoảng `1000..86400000`. `COMMAND_ACK` có 8 byte: `ack_sequence uint16`, `ack_outcome`, `reported_pump_state`, `driver_feedback`, `reserved[3]`. ACK không phải bằng chứng flow.

`command_id` có hai biểu diễn có chủ đích: MQTT dùng chuỗi opaque (`1..64` ký tự, thường UUID); production RF header dùng `uint32_t`. Gateway không được parse UUID bằng `atoi()`. Gateway cấp RF correlation ID `uint32_t` tăng dần cho mỗi logical command, giữ mapping bounded trong pending-command table (`rf_command_id -> mqtt_command_id`), rồi dùng mapping đó khi publish ACK/lifecycle event và khi correlate telemetry. RF `sequence` vẫn là counter độc lập; không được dùng sequence thay cho command ID.

TELEMETRY binary gồm `reported_pump_state`, `driver_feedback`, `flow_lpm_x100`, `delivered_volume_ml`, `pulse_count`, `fault_flags`, `last_command_id`. Flow hợp lệ `0..600` (`0.00..6.00 L/min`); trên 600 là over-range/invalid.

## 4. Session, sequence và anti-replay

Mỗi peer theo dõi `{last_boot_session_id, last_sequence_num}`. Frame hợp lệ khi:

```text
boot_session_id > last_boot_session_id
OR
boot_session_id == last_boot_session_id
AND serial_distance(sequence, last_sequence_num) in 1..32767
```

Reject nếu session cũ, sequence trùng, sequence ở phía sau cửa sổ modulo hoặc HMAC/CRC không hợp lệ. Quy tắc retry là retransmit nguyên frame, không tạo sequence mới:

```text
same boot_session_id + same sequence + same command_id
same payload + same HMAC + same CRC
```

Node cache terminal outcome theo `{boot_session_id, sequence, command_id}`. Duplicate chỉ trả ACK đã cache, không actuate lần hai và không gia hạn lease. Gateway retry tối đa 3 attempts, deadline AGU 300 ms; sequence wrap `65535 -> 0` được xử lý bằng serial distance.

Khi reboot, boot session tăng từ NVS; state sequence peer cũ bị vô hiệu hóa, correlation cũ bị hủy và gateway queue `SET_PUMP(OFF)`. NVS lỗi hoặc counter cạn phải fail-closed.

## 5. AGU Legacy / Delphi frame

### 5.1 Wire shape

AGU Legacy **không có** production header, HMAC, boot session hoặc sequence. Codec triển khai envelope Delphi `TSCI.SendCom`:

```text
[Length][Opcode][Params...][Checksum]
```

`Length = payloadLen + 1`, trong đó payload là `[Opcode][Params...]`. Checksum là two's-complement zero-sum sao cho:

```text
sum([Length][Opcode][Params][Checksum]) mod 256 == 0
```

Độ rộng/byte order của Params phải lấy từ `AguLegacyCodec`; không được tự viết frame bằng tay.

### 5.2 Opcode và encoding đã implement

| Opcode | Codec | Payload cụ thể |
|---:|---|---|
| `0x05` | `encodePing(value, nodeId)` | `[0x05, value, nodeId]` |
| `0x06` | `encodePumpOn(nodeId)` | `[0x06, nodeId]` |
| `0x07` | `encodePumpOff(nodeId)` | `[0x07, nodeId]` |
| `0x08` | `encodeReadEeprom(addr)` | `[0x08, addr_hi, addr_lo]` |
| `0x09` | `encodeWriteEeprom(addr, value)` | `[0x09, addr_hi, addr_lo, value]` |
| `0x0A` | `encodeGetId()` / `encodeSetId(newId)` | GET `[0x0A, 0x00]`; SET `[0x0A, 0x01, newId]` |
| `0x0E` | `encodeReadRamBurst(nodeId, addr, count=8)` | `[0x0E, addr_lo, addr_hi, count, nodeId]` | `Length=0x06` for the current five-byte payload; address little-endian; deployed telemetry uses `count=0x08`. Response is 8 RAM bytes plus one zero-sum checksum byte. |
| `0x01`, `0x04` | additional codec | read word, write RAM | Use the exact function signatures and byte order in `AguLegacyCodec`; do not infer fields not present in the codec. |

Lưu ý: EEPROM address trong codec là **big-endian**; `READ_RAM_BURST` address là **little-endian**. Đây là khác biệt legacy đã được implementation chứng minh. `MAX_CMD_SIZE=16`, burst data tối đa 8 byte. `0x5A` chỉ là legacy ACK transaction, không có nghĩa pump chạy hoặc flow đã xác nhận. Discovery response có sync `FF 5A`; không dùng sync này làm production RF SOF.

### 5.3 Production-to-legacy translation

| Semantic event | Legacy opcode | Gateway interpretation |
|---|---:|---|
| PING | `0x05` | Echo đúng = online; không suy ra pump state |
| ON / schedule ON | `0x06` | ACK transaction, tiếp tục evidence pipeline |
| OFF / lease expiry / safe-off | `0x07` | ACK transaction, vẫn phải verify OFF/current/flow |
| Discovery / GET_ID | `0x0A` | DISCOVERED candidate, chưa active |
| Schedule read | `0x08` | Đọc EEPROM, parse và đối soát |
| Schedule write | `0x09` | ACK + bắt buộc read-after-write verification |

Gateway không được map `0x5A` trực tiếp thành `COMPLETED` hay `FLOW_CONFIRMED`.

## 6. Discovery, claim và provisioning

Allow-list production là gateway `0`, node `1..4`; address space `5..12` chưa được production acceptance. Vòng đời chuẩn:

```text
DISCOVERED -> CLAIMED -> PROVISIONED -> ACTIVE
```

`GET_ID` thành công chỉ tạo discovery result. Claim cần unique ID, allow-list, identity check nếu có, provision PSK/config, ping/health check, flow/lease policy hợp lệ và hoàn tất read-after-write/config verification. Backend hiện chưa có enum `CLAIMED` thống nhất toàn hệ thống; đây là **TBD** cần chuẩn hóa trước khi dùng làm API enum chính thức.

Repository đang có dấu hiệu physical legacy topology `4..7` trong firmware host trong khi production RF contract là `1..4`. Đây là discrepancy tích hợp và **PRODUCTION BLOCKER** cho acceptance: không được tự đổi ID trong tài liệu; phải có quyết định topology/adapter được ký.

## 7. MQTT v1 contract

Topic được yêu cầu ở lớp integration semantic là:

```text
aeroponics/node/{nodeId}/command
aeroponics/node/{nodeId}/ack
aeroponics/node/{nodeId}/telemetry
aeroponics/gateway/heartbeat
```

Trong implementation hiện tại, topic production chi tiết đang dùng namespace `aeroponics/device/{device_id}/...`. Vì vậy hai dạng dưới đây phải được coi là **logical alias mapping**, không publish đồng thời nếu chưa có migration decision:

| Logical interface | Current implementation topic | QoS | Retain |
|---|---|---:|---:|
| admission ACK | `aeroponics/device/{device_id}/ack/{command_id}` | 1 | **Không** |
| telemetry | `aeroponics/device/{device_id}/telemetry` hoặc normalized node telemetry | 1 | Không |
| lifecycle event | `aeroponics/device/{device_id}/telemetry/command/{command_id}/event` | 1 | Không |
| override | `aeroponics/device/{device_id}/command/override` | 1 | Không |
| gateway status/LWT | `aeroponics/device/{device_id}/status` | 1 | Có |

Topic `node/{nodeId}` là contract facade dành cho backend/client; gateway adapter phải map rõ `nodeId` sang `device_id`. Transactional topics (`command`, `ack`, `event`, `telemetry`) luôn non-retained để subscriber mới không nhận lại giao dịch cũ. Chỉ status/LWT và các state snapshot được chọn mới được retain. MQTT 5 `message expiry`, `correlation data`, `response topic` là **OPTIONAL/TBD**; không được đặt thành MUST khi deployment vẫn MQTT 3.1.1-compatible.

## 8. JSON payload schemas

Các schema dưới đây là schema contract ở mức JSON. Khi tạo file schema chính thức, dùng JSON Schema Draft 2020-12 và giữ `schema_version: "1.0"`. Breaking change phải tăng major topic (`aeroponics/v2/...`); thay đổi tương thích ngược không đổi major và không đổi âm thầm ý nghĩa field. Dải JSON `node_id=1..16` là envelope tương thích với address space; production RF acceptance hiện vẫn chỉ `1..4`, còn `5..16` phải bị gateway policy chặn cho đến khi có acceptance riêng.

### 8.1 Command: `.../command`

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://aeroponics.local/schema/v1/command.json",
  "type": "object",
  "additionalProperties": false,
  "required": ["schema_version", "command_id", "node_id", "command", "source"],
  "properties": {
    "schema_version": {"const": "1.0"},
    "command_id": {"type": "string", "minLength": 1, "maxLength": 64},
    "node_id": {"type": "integer", "minimum": 1, "maximum": 16},
    "command": {"enum": ["PING", "SET_PUMP", "RESET_FAULT", "DISCOVERY", "SCHEDULE_READ", "SCHEDULE_WRITE", "FLOW_POLICY", "GROUP_ASSIGNMENT"]},
    "desired_state": {"enum": ["ON", "OFF"]},
    "run_lease_ms": {"type": "integer", "minimum": 1000, "maximum": 300000},
    "override_duration_ms": {"type": "integer", "minimum": 1000, "maximum": 86400000},
    "source": {"enum": ["MANUAL_OVERRIDE", "FAIL_SAFE", "SCHEDULE"]},
    "issued_at": {"type": "string", "format": "date-time"}
  },
  "allOf": [{"if": {"properties": {"command": {"const": "SET_PUMP"}}}, "then": {"required": ["desired_state", "run_lease_ms"]}}]
}
```

### 8.2 Admission ACK: `.../ack`

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://aeroponics.local/schema/v1/admission-ack.json",
  "type": "object",
  "additionalProperties": false,
  "required": ["schema_version", "command_id", "node_id", "status", "gateway_timestamp_ms"],
  "properties": {
    "schema_version": {"const": "1.0"},
    "command_id": {"type": "string", "minLength": 1, "maxLength": 64},
    "node_id": {"type": "integer", "minimum": 1, "maximum": 16},
    "status": {"enum": ["ACCEPTED", "REJECTED"]},
    "reason": {"type": "string", "maxLength": 256},
    "gateway_timestamp_ms": {"type": "integer", "minimum": 0}
  }
}
```

Admission ACK chỉ trả quyết định nhận/không nhận command. Consumer deduplicate theo `{device_id, command_id}`. Nó không đại diện `RF_ACKED`, pump running hay flow.

### 8.3 Telemetry: `.../telemetry`

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://aeroponics.local/schema/v1/telemetry.json",
  "type": "object",
  "additionalProperties": false,
  "required": ["schema_version", "node_id", "timestamp", "state", "feedback", "flow", "diagnostics"],
  "properties": {
    "schema_version": {"const": "1.0"},
    "node_id": {"type": "integer", "minimum": 1, "maximum": 16},
    "timestamp": {"type": "string", "format": "date-time"},
    "command_id": {"type": "string", "maxLength": 64},
    "state": {"type": "object", "additionalProperties": false, "required": ["desired", "reported", "schedule_state", "override_state", "fsm_state"], "properties": {"desired": {"enum": ["ON", "OFF"]}, "reported": {"enum": ["ON", "OFF"]}, "schedule_state": {"enum": ["UNKNOWN", "SPRAYING", "COOLING_DOWN", "IDLE", "PAUSED"]}, "override_state": {"enum": ["NONE", "OVERRIDE_OFF", "OVERRIDE_ON"]}, "fsm_state": {"type": "string"}}},
    "feedback": {"type": "object", "additionalProperties": false, "required": ["driver_feedback", "current_ma", "voltage_v", "fault_flags"], "properties": {"driver_feedback": {"type": "boolean"}, "load_feedback": {"enum": ["ON", "OFF", "UNKNOWN"]}, "driver_feedback_mismatch": {"type": "boolean"}, "current_ma": {"type": "integer", "minimum": 0, "maximum": 6000}, "voltage_v": {"type": "number", "minimum": 0, "maximum": 25}, "fault_flags": {"type": "integer", "minimum": 0}}},
    "flow": {"type": "object", "additionalProperties": false, "required": ["flow_lpm", "delivered_volume_ml", "total_litres", "pulse_count", "flow_confirmed", "quality_flag", "is_fault", "fault_code"], "properties": {"flow_lpm": {"type": "number", "minimum": 0, "maximum": 6}, "delivered_volume_ml": {"type": "integer", "minimum": 0}, "total_litres": {"type": "number", "minimum": 0}, "pulse_count": {"type": "integer", "minimum": 0}, "flow_confirmed": {"type": "boolean"}, "flow_stability_pct": {"type": "number", "minimum": 0, "maximum": 100}, "quality_flag": {"enum": ["OK", "SUSPECT", "INVALID"]}, "is_fault": {"type": "boolean"}, "fault_code": {"enum": ["NONE", "NO_FLOW_FAULT", "UNEXPECTED_FLOW_FAULT", "OVER_RANGE_FAULT", "SENSOR_FAULT"]}}},
    "diagnostics": {"type": "object", "additionalProperties": false, "required": ["gateway_timestamp_ms", "boot_session_id"], "properties": {"boot_session_id": {"type": "integer", "minimum": 1, "maximum": 4294967295}, "rf_seq": {"type": ["integer", "null"], "minimum": 0, "maximum": 65535}, "node_timestamp_ms": {"type": ["integer", "null"], "minimum": 0}, "gateway_timestamp_ms": {"type": "integer", "minimum": 0}, "node_uptime_s": {"type": ["integer", "null"], "minimum": 0}}}
  }
}
```

Đơn vị bắt buộc: flow `L/min`, volume cycle `mL`, total `L`, current `mA`, voltage `V`, pulse `count`; `node_timestamp_ms` và `gateway_timestamp_ms` là monotonic, không được diễn giải là UTC. Với legacy telemetry, `boot_session_id` trong diagnostics là gateway boot session; `rf_seq`, `node_timestamp_ms` và `node_uptime_s` có thể là `null` vì ATmega8 không phát production session/sequence/clock.

### 8.4 Gateway heartbeat: `.../gateway/heartbeat`

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://aeroponics.local/schema/v1/gateway-heartbeat.json",
  "type": "object",
  "additionalProperties": false,
  "required": ["schema_version", "status", "device_id", "uptime_s", "timestamp_utc", "mqtt_connected", "rf_statistics", "free_heap_b", "reset_reason", "ntp_synced", "rtc_valid", "boot_session_id"],
  "properties": {
    "schema_version": {"const": "1.0"},
    "status": {"enum": ["online", "degraded", "offline"]},
    "device_id": {"type": "string", "minLength": 1, "maxLength": 64},
    "uptime_s": {"type": "integer", "minimum": 0},
    "timestamp_utc": {"type": "string", "format": "date-time"},
    "mqtt_connected": {"type": "boolean"},
    "rf_statistics": {"type": "object", "additionalProperties": true},
    "free_heap_b": {"type": "integer", "minimum": 0},
    "reset_reason": {"type": "string", "minLength": 1},
    "ntp_synced": {"type": "boolean"},
    "rtc_valid": {"type": "boolean"},
    "boot_session_id": {"type": "integer", "minimum": 1, "maximum": 4294967295}
  }
}
```

Heartbeat định kỳ mỗi 5 giây, stale sau 15 giây, không retain. Chỉ status/LWT retain. Message đầu tiên sau reboot phải có `reset_reason` và `boot_session_id`; field set này cần được kiểm tra đồng nhất trong implementation hiện tại.

## 9. ACK lifecycle và safety evidence

Admission và execution là hai luồng khác nhau:

```text
ACCEPTED
  -> QUEUED
  -> RF_ACKED
  -> PUMP_FEEDBACK_ON
  -> CURRENT_DETECTED
  -> FLOW_CONFIRMED
  -> COMPLETED
```

Terminal/error events gồm `REJECTED`, `RF_TIMEOUT_OR_NACK`, `SAFE_OFF_UNCONFIRMED`, `FAULT_LATCHED`. `ACCEPTED != RF_ACKED != PUMP_RUNNING != FLOW_CONFIRMED`; ON chỉ được `COMPLETED` khi flow hợp lệ, correlated, fault-free trong start timeout. Lifecycle event publish non-retained trên topic command event tương ứng.

Safety FSM phải giữ `BOOT_OFF`, `SCHEDULE_SPRAY`, `SCHEDULE_COOLDOWN`, `OVERRIDE_RUN`, `OVERRIDE_HOLD_OFF`, `FAULT_LATCH`. Stale, timeout, mismatch, no-current, no-flow, unexpected-flow và reboot đều fail-closed, cancel ON và force OFF. `FAULT_RESET` chỉ được clear latch sau pre-flight; telemetry không được tự clear fault.

## 10. Test vectors và acceptance checklist

Test tối thiểu phải bao gồm:

- CRC16 ASCII `123456789` = `0x29B1`.
- Canonical little-endian SET_PUMP header/payload từ `RF_PROTOCOL.md`.
- HMAC test fixture dùng PSK giả lập, không dùng PSK manufacturing; kiểm tra rõ phạm vi có/không có SOF.
- Duplicate sequence không actuate lần hai và không gia hạn lease.
- Serial wrap `65535 -> 0`; reject distance `0` và `>=32768`.
- Old boot session, invalid length, invalid node ID, malformed payload.
- Legacy zero-sum frame cho mọi encoder; address order EEPROM/burst RAM.
- Valid/invalid telemetry, flow over-range, fault flags và dual timestamps.
- Admission ACK deduplication và toàn bộ lifecycle event.
- Heartbeat interval/stale threshold, reset reason và safe-off sau reboot.

Các invariant bắt buộc: không raw frame trong persistence; retry giữ nguyên wire frame; `0x5A` không suy ra flow; node không production-accepted ngoài `1..4`; không claim security provisioning hoàn chỉnh khi thiếu evidence độc lập.

## 11. Open items và production blockers

| Mục | Trạng thái | Hành động đóng |
|---|---|---|
| JSON Schema file chính thức làm source of truth | TBD | Tạo schema Draft 2020-12, validate DTO và firmware constraints chung |
| Enum `CLAIMED/PROVISIONED` backend | TBD | Chuẩn hóa persistence/API/state transition |
| MQTT facade `node/{nodeId}` so với namespace `device/{device_id}` | TBD | Chốt alias/migration, không publish mơ hồ hai namespace |
| MQTT auth TLS/mTLS profile | TBD | Chốt deployment security profile và acceptance evidence |
| Physical legacy IDs `4..7` so với production IDs `1..4` | PRODUCTION BLOCKER | Quyết định adapter/topology và test acceptance; schema envelope `1..16` không tự mở production RF |
| Encrypted NVS, Flash Encryption, Secure Boot, factory PSK procedure | PRODUCTION BLOCKER | Independent security sign-off `RF_PROVISIONING_INDEPENDENT_SIGNOFF=1` |
| Heartbeat `rf_statistics`, `reset_reason`, `mqtt_connected` đầy đủ | TBD | Đồng nhất firmware, gateway MQTT và schema |
| MQTT 5 properties | OPTIONAL/TBD | Chỉ bắt buộc khi broker/client profile được nâng cấp |
| MQTT string `command_id` ↔ RF `uint32_t command_id` | IMPLEMENTED pattern | Gateway phải dùng bounded pending/correlation table; không `atoi()` UUID và không trộn RF sequence với command ID |
| `encodeReadRamBurst(nodeId, addr, count)` multi-node support | IMPLEMENTED | Codec nhận node ID và giới hạn `count=0x08` vì decoder response hiện cố định 8 byte; giữ vector node `4`/`7` |
