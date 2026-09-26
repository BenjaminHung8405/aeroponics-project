# NodeID / GroupID RF Addressing Plan

## 1. Mục đích

Chuẩn hóa địa chỉ RF cho 15 node vật lý và 4 group multicast trong cùng đợt migration CRC16. Tài liệu này là baseline để các sprint CRC không vô tình giới hạn địa chỉ ở topology cũ `node 4..7`.

## 2. Address space chuẩn

- Gateway: `0x00`.
- Node vật lý: `0x01..0x0F` (15 node), biểu diễn theo hex là `[1,2,3,4,5,6,7,8,9,A,B,C,D,E,F]`.
- RF group address: `0x10`, `0x14`, `0x18`, `0x1C`.
- Logical group trong backend/MQTT/database: `1..4`; không đổi schema chỉ vì RF address đổi.

## 3. Mapping group

| Group | RF groupID | Node members | Cardinality |
|---|---:|---|---:|
| 1 | `0x10` | `0x1,0x2,0x3` | 3 |
| 2 | `0x14` | `0x4,0x5,0x6,0x7` | 4 |
| 3 | `0x18` | `0x8,0x9,0xA,0xB` | 4 |
| 4 | `0x1C` | `0xC,0xD,0xE,0xF` | 4 |

Formula đã xác nhận — legacy Delphi (`TestSCI.dpr:340`):

```text
groupID = 0x10 | (nodeID & 0x0C)
```

Membership test cho node nhận được groupID:

```text
belongs = (groupID & 0x0C) == (nodeID & 0x0C)
```

Ví dụ: node `0x06` thuộc group `0x14` vì `(0x14 & 0x0C) = 0x04` và `(0x06 & 0x0C) = 0x04`.

> **Không** phải `nodeID × $F8` — `$F8` không có trong legacy source Delphi/Assembly. Mask đúng là `0x0C`, sau đó OR với prefix `0x10`.

`0x00` is reserved for the gateway. Therefore group `0x10` has only three valid node members (`0x1..0x3`); this avoids assigning the gateway as a controllable pump node.

## 4. Required firmware changes

1. Update the RF address constants in `aeroponics-firmware/include/config.h`:
   - `RF_MAX_NODE_ID` from decimal `12` to hex `0x0F`.
   - Keep `RF_GATEWAY_NODE_ID = 0` and `RF_MIN_NODE_ID = 1`.
   - Add named constants/table for the four RF group addresses; do not scatter `0x10`, `0x14`, `0x18`, `0x1C` as magic numbers.
2. Split validation into explicit predicates:
   - `isValidNodeId`: `0x1..0xF`.
   - `isValidRfGroupId`: exact membership in `{0x10,0x14,0x18,0x1C}`.
   - `isValidSourceAddress`: gateway or node only.
   - `isValidTargetAddress`: gateway, node, or RF group.
3. Update `RfFrameCodec` address validation without changing the wire field width. A group address is legal only as a target.
4. Preserve logical group IDs `1..4` in `NodeRegistry`, scheduler, MQTT, backend, and database. Add a single mapping helper at the RF boundary.
5. Review all production-only guards currently fixed to `4..7`. They must be explicitly classified as either:
   - legacy AGU compatibility (`4..7`, unchanged), or
   - general RF node addressing (`1..F`, expanded).
   Do not widen AGU legacy commands unless the legacy node firmware supports the additional IDs.
6. Update MQTT/control-plane node validation and documentation from maximum `12` to maximum `15` where the command targets the new RF node topology. Keep `group_id` validation at `1..4`.

## 5. CRC16 interaction

- CRC scope remains `[RF header + payload + HMAC tag]`, excluding the two CRC trailer bytes.
- The encoded target/source address bytes are part of the CRC input; changing a unicast target to a group target must produce a different CRC.
- Add golden frames for at least one target in each address class: gateway, node `0x01`, node `0x0F`, group `0x10`, group `0x14`, group `0x18`, and group `0x1C`.
- A one-bit mutation in either address byte must return `ParseError::CRC_MISMATCH` when the CRC trailer is not regenerated.
- HMAC remains the authentication mechanism. Group addressing must not weaken source validation or permit a group address in `source_node_id`.

## 6. Migration order

1. Freeze the mapping above and record the product decision about the phrase `1[first nodeID]`.
2. Add constants, predicates, and mapping unit tests before widening the accepted address range.
3. Update `RfFrameCodec` address validation and group-target routing.
4. Apply CRC16-Modbus migration and regenerate all address-sensitive golden frames.
5. Update node registry/control-plane validation and preserve AGU legacy restrictions.
6. Run native tests, ESP32 build, ATmega8 build, and RF interoperability tests with mixed unicast/group frames.
7. Release gateway and nodes as one protocol-versioned deployment; reject unsupported protocol versions fail-closed.

## 7. Acceptance criteria

- All 15 node IDs `0x1..0xF` pass node-address validation.
- `0x00` passes only as gateway/source or gateway target where the existing protocol allows it; it never passes as a pump node.
- Exactly four RF group IDs pass group validation: `0x10`, `0x14`, `0x18`, `0x1C`.
- Group IDs are accepted only in the target address field, never in the source field.
- Logical groups `1..4` map one-to-one to the four RF group IDs.
- AGU legacy commands remain restricted to node IDs `0x4..0x7` unless a separate compatibility decision is approved.
- CRC16-Modbus vectors and all address-class golden frames pass on native, ESP32, and ATmega8 targets.
- Invalid address, stale protocol version, CRC mismatch, and unauthenticated group command are all rejected fail-closed.
