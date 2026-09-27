# AGU Legacy Wire Evidence

**Status:** Authoritative implementation evidence, updated 2026-09-26

## Scope

The ESP32 gateway southbound protocol is AGU-Aeroponics legacy SCI. It is
separate from the modern ESP32 RF codec and from treatment/NVS integrity
checksums. The legacy codec must use the wire behavior shown by the Delphi
`TSCI.SendComCRC16`/`CalCRC16` implementation and the corresponding AVR
assembly.

## Chosen Wire Contract

```text
[length][payload][crc_lo][crc_hi]
```

- CRC: CRC16-Modbus, initial value `0xFFFF`, reflected polynomial `0xA001`,
  no XOR-out.
- CRC coverage: the complete prefix `[length][payload]`.
- CRC byte order: little-endian (`crc_lo`, then `crc_hi`).
- Length: `payload_len + 2`, including both CRC bytes and excluding the length
  byte itself.
- Verification: calculate CRC over the complete frame and require remainder
  `0x0000`, matching `CheckCRC16`.
- ACK and framing: command ACK remains `0x5A`; framed ID remains `FF 5A ID`.

## Exact Vectors

| Transaction | Wire bytes |
|---|---|
| Pump ON, node 9 | `04 06 09 F3 A7` |
| Pump OFF, node 9 | `04 07 09 F2 37` |
| Read burst response, sample data `0A 14 1E 28 32 3C 46 50` | `0A 0A 14 1E 28 32 3C 46 50 3F 7E` |

The read burst response is 11 bytes: length `0x0A`, eight data bytes, and two
CRC bytes. The corresponding `decodeBurstRam` output contains only the eight
data bytes after validation.

## Rejected Legacy Assumption

The former one-byte two's-complement zero-sum model is not the deployed AGU
legacy wire contract. For example, the nine-byte frame
`0A 14 1E 28 32 3C 46 50 98` is rejected by `decodeBurstRam`; it cannot be
treated as a valid burst response. No compatibility fallback is permitted.

## Implementation and Regression Coverage

- `aeroponics-firmware/src/agu_legacy_codec.cpp` formats every SendCom frame
  through `core/Crc16Modbus.h` and validates burst responses fail-closed.
- `aeroponics-firmware/src/agu_legacy_rf_host.cpp` waits for the 11-byte burst
  envelope while preserving ACK transaction behavior.
- Production tests assert the exact pump vectors, command CRCs, burst length and
  CRC, rejection of old zero-sum frames, ACK recognition, and framed-ID parsing.
- The modern `RfFrameCodec` and treatment/NVS checksum implementations are not
  changed by this migration.
