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

Tài liệu này không hợp nhất hai giao thức thành một frame. Northbound MQTT là semantic contract; southbound ESP32 ↔ ATmega8 là **AGU-Aeroponics legacy SCI**. AGU dùng `AguLegacyCodec` với Length/Opcode/Params/CRC16-Modbus và không có HMAC, session, sequence hay production `command_id`.

## 2. Ranh giới kiến trúc

```text
Backend / MQTT
    -> semantic command, command_id, admission ACK
ESP32-S3 Gateway
    -> semantic translation, timeout/retry, correlation ở gateway
    -> AguLegacyCodec: Length/Opcode/Params/CRC16-Modbus
ATmega8 / AGU legacy node
    -> relay or MOSFET output, legacy response
    -> gateway feedback and flow evidence
```

Gateway là chủ sở hữu FSM runtime, lease, timeout/retry, correlation và safe-off. Node legacy chỉ xử lý transaction AGU, relay và EEPROM/configuration. Raw RF/AGU bytes là transport data tạm thời; không được persist vào MQTT, database hay audit business record.

## 3. Modern gateway/model RF wire (v2)

Phần này là modern ESP32 gateway/model contract; không phải bằng chứng rằng preloaded ATmega8 đang phát hoặc xác minh frame này. Southbound production vẫn là AGU legacy SCI ở §4.

**NORMATIVE:** protocol version `0x02`, `HMAC_TAG_SIZE = 16`, và layout:

```text
[SOF 2B][Version 1B][Message/Header + Payload][HMAC_TAG 16B][CRC_LO 1B][CRC_HI 1B]
```

CRC16-Modbus dùng init `0xFFFF`, polynomial `0xA001`, reflected LSB-first, `XorOut=0x0000`. CRC bao phủ toàn bộ byte serialize từ `SOF`/header qua payload và 16-byte HMAC tag; không bao phủ hai byte CRC trailer. Trailer luôn little-endian `[CRC_LO][CRC_HI]`. Vector chuẩn ASCII `123456789` là `0x4B37`. CRC chỉ phát hiện lỗi truyền dẫn; HMAC mới cung cấp authenticity.

CCITT-FALSE (`0x1021`, vector `0x29B1`) chỉ được giữ cho treatment-storage checksum và legacy history; không dùng cho modern RF wire.

## 4. AGU-Aeroponics Legacy RF 433 MHz

### 4.1 Frame layout

**NORMATIVE / IMPLEMENTED:** AGU frame dùng `[Length][Opcode][Params...][CRC_LO][CRC_HI]`; CRC là CRC16-Modbus trên `[Length][Opcode][Params...]`. Không có `SOF`, HMAC, session hoặc sequence trong AGU frame.

```text
[Length 1B][Opcode 1B][Params...][CRC_LO 1B][CRC_HI 1B]
```

| Offset tương đối | Trường | Kích thước | Giá trị / quy tắc |
|---:|---|---:|---|
| 0 | `length` | 1 | `payload_len + 2`; includes both CRC bytes |
| 1 | `opcode` | 1 | AGU opcode đã xác minh, ví dụ `0x05`, `0x06`, `0x07` |
| 2..N | `params` | variable | Node ID và tham số theo opcode |
| cuối | `crc` | 2 | CRC16-Modbus, little-endian `[crc_lo][crc_hi]`; covers length and payload |

AGU commands: `0x05 PING`, `0x06 PUMP_ON`, `0x07 PUMP_OFF`; legacy ACK là `0x5A`. Các opcode khác chỉ dùng sau khi firmware đã nạp được xác minh.

### 4.2 AGU checksum

**NORMATIVE / IMPLEMENTED:** CRC16-Modbus with init `0xFFFF`, reflected
polynomial `0xA001`, no XOR-out, and little-endian output.

```text
crc = CRC16_MODBUS([Length][Opcode][Params])
wire = [Length][Opcode][Params][crc_lo][crc_hi]
```

Receiver phải kiểm tra length, opcode, params và CRC16-Modbus remainder bằng `0`. CRC chỉ phát hiện lỗi truyền dẫn; không cung cấp authenticity hoặc confidentiality.

**Legacy implementation boundary:** `AguLegacyCodec` là codec duy nhất được dùng trên southbound. Không đưa `RfFrameCodec`, HMAC, session hoặc sequence vào frame AGU.

AGU CRC16-Modbus không phải cơ chế xác thực. Quyền điều khiển phải được bảo vệ ở MQTT/backend/gateway và bằng biện pháp vật lý phù hợp; không tuyên bố RF legacy có confidentiality hoặc authenticity.

### 4.3 Payload production

`SET_PUMP` có đúng 9 byte:

```text
offset 0: desired_state       uint8 (OFF=0, ON=1)
offset 1: run_lease_ms        uint32 LE
offset 5: max_on_duration_ms  uint32 LE
```

`run_lease_ms` là gateway policy trong khoảng `1000..300000`; không phải node-side lease. AGU ACK không phải bằng chứng flow.

`command_id` chỉ tồn tại ở northbound MQTT/audit. Gateway giữ mapping
correlation trong RAM; không encode `command_id` vào AGU frame.

TELEMETRY binary gồm `reported_pump_state`, `driver_feedback`, `flow_lpm_x100`, `delivered_volume_ml`, `pulse_count`, `fault_flags`, `last_command_id`. Flow hợp lệ `0..600` (`0.00..6.00 L/min`); trên 600 là over-range/invalid.

## 5. AGU Transaction, Retry và Liveness

AGU không có `boot_session_id`, sequence hoặc anti-replay. Gateway chỉ kiểm
tra length/opcode/params/CRC16-Modbus, serialize một transaction trên bus và retry
theo policy của `AguLegacyRfHost`:

```text
same `[Length][Opcode][Params][CRC_LO][CRC_HI]` khi retry
```

`0x5A` chỉ là legacy transaction ACK. Node duplicate/idempotency behavior,
lease enforcement và actuator state sau retry là UNKNOWN; gateway không được
suy diễn từ ACK.

Khi gateway reboot hoặc AGU liveness mất, correlation cũ bị hủy và remote
actuator được đánh dấu `UNKNOWN`; chỉ response AGU hợp lệ hoặc interlock vật lý
mới cung cấp bằng chứng trạng thái.

## 6. AGU Legacy / Delphi frame

### 6.1 Wire shape

AGU Legacy **không có** production header, HMAC, boot session hoặc sequence. Codec triển khai envelope Delphi `TSCI.SendCom`:

```text
[Length][Opcode][Params...][CRC_LO][CRC_HI]
```

`Length = payloadLen + 2`, trong đó payload là `[Opcode][Params...]`. CRC là
CRC16-Modbus trên `[Length][Opcode][Params...]`, ghi little-endian sao cho:

```text
CRC16_MODBUS([Length][Opcode][Params][crc_lo][crc_hi]) == 0
```

Độ rộng/byte order của Params phải lấy từ `AguLegacyCodec`; không được tự viết frame bằng tay.

### 6.2 Opcode và encoding đã implement

| Opcode | Codec | Payload cụ thể |
|---:|---|---|
| `0x05` | `encodePing(value, nodeId)` | `[0x05, value, nodeId]` |
| `0x06` | `encodePumpOn(nodeId)` | `[0x06, nodeId]` |
| `0x07` | `encodePumpOff(nodeId)` | `[0x07, nodeId]` |
| `0x08` | `encodeReadEeprom(addr)` | `[0x08, addr_hi, addr_lo]` |
| `0x09` | `encodeWriteEeprom(addr, value)` | `[0x09, addr_hi, addr_lo, value]` |
| `0x0A` | `encodeGetId()` / `encodeSetId(newId)` | GET `[0x0A, 0x00]`; SET `[0x0A, 0x01, newId]` |
| `0x0E` | `encodeReadRamBurst(nodeId, addr, count=8)` | `[0x0E, addr_lo, addr_hi, count, nodeId]` | `Length=0x07` for the current five-byte payload plus two CRC bytes; address little-endian; deployed telemetry uses `count=0x08`. Response is `[length=0x0A][8 RAM bytes][crc_lo][crc_hi]`. |
| `0x01`, `0x04` | additional codec | read word, write RAM | Use the exact function signatures and byte order in `AguLegacyCodec`; do not infer fields not present in the codec. |

Lưu ý: EEPROM address trong codec là **big-endian**; `READ_RAM_BURST` address là **little-endian**. Đây là khác biệt legacy đã được implementation chứng minh. `MAX_CMD_SIZE=16`, burst data tối đa 8 byte. `0x5A` chỉ là legacy ACK transaction, không có nghĩa pump chạy hoặc flow đã xác nhận. Discovery response có sync `FF 5A`; không dùng sync này làm production RF SOF.

### 6.3 Production-to-legacy translation

| Semantic event | Legacy opcode | Gateway interpretation |
|---|---:|---|
| PING | `0x05` | Echo đúng = online; không suy ra pump state |
| ON / schedule ON | `0x06` | ACK transaction, tiếp tục evidence pipeline |
| OFF / lease expiry / safe-off | `0x07` | ACK transaction, vẫn phải verify OFF/current/flow |
| Discovery / GET_ID | `0x0A` | DISCOVERED candidate, chưa active |
| Schedule read | `0x08` | Đọc EEPROM, parse và đối soát |
| Schedule write | `0x09` | ACK + bắt buộc read-after-write verification |

Gateway không được map `0x5A` trực tiếp thành `COMPLETED` hay `FLOW_CONFIRMED`.

## 7. Discovery, claim và provisioning

Allow-list production là gateway `0`, node `1..4`; address space `5..12` chưa được production acceptance. Vòng đời chuẩn:

```text
DISCOVERED -> CLAIMED -> PROVISIONED -> ACTIVE
```

`GET_ID` thành công chỉ tạo discovery result. Claim cần unique ID, allow-list, identity check nếu có, provision PSK/config, ping/health check, flow/lease policy hợp lệ và hoàn tất read-after-write/config verification. Backend hiện chưa có enum `CLAIMED` thống nhất toàn hệ thống; đây là **TBD** cần chuẩn hóa trước khi dùng làm API enum chính thức.

Repository đang có dấu hiệu physical legacy topology `4..7` trong firmware host trong khi production RF contract là `1..4`. Đây là discrepancy tích hợp và **PRODUCTION BLOCKER** cho acceptance: không được tự đổi ID trong tài liệu; phải có quyết định topology/adapter được ký.

## 8. MQTT v1 contract

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

## 9. JSON payload schemas

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

## 10. ACK lifecycle và safety evidence

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

## 11. Test vectors và acceptance checklist

Test tối thiểu phải bao gồm:

- Modern v2 CRC16-Modbus ASCII `123456789` = `0x4B37`; treatment-storage CCITT is a separate historical checksum.
- Canonical AGU `PUMP_ON`/`PUMP_OFF`/`PING` frame từ `AguLegacyCodec`.
- CRC16-Modbus, length/opcode/parameter validation và legacy ACK `0x5A`.
- Retry giữ nguyên toàn bộ AGU frame; không có sequence hoặc HMAC.
- Old boot session, invalid length, invalid node ID, malformed payload.
- Legacy CRC16-Modbus frame cho mọi encoder; address order EEPROM/burst RAM.
- Valid/invalid telemetry, flow over-range, fault flags và dual timestamps.
- Admission ACK deduplication và toàn bộ lifecycle event.
- Heartbeat interval/stale threshold, reset reason và safe-off sau reboot.

Các invariant bắt buộc: không raw frame trong persistence; retry giữ nguyên wire frame; `0x5A` không suy ra flow; node không production-accepted ngoài `1..4`; không claim security provisioning hoàn chỉnh khi thiếu evidence độc lập.

## 12. Open items và production blockers

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
| MQTT string `command_id` ↔ AGU frame | IMPLEMENTED pattern | `command_id` chỉ ở northbound/audit; không encode vào AGU frame |
| `encodeReadRamBurst(nodeId, addr, count)` multi-node support | IMPLEMENTED | Codec nhận node ID và giới hạn `count=0x08` vì decoder response hiện cố định 8 byte; giữ vector node `4`/`7` |
