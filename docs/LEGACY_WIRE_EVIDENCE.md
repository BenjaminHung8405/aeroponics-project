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

## Field Observation: `PING (0x05)` is unanswered on deployed nodes

**Status:** observed on hardware, gateway-side format ruled out.

Bench log (2026-09-27) shows a live node 8 that answers actuator commands but
never answers PING:

```text
[CLI] PUMP ON node 8 for 15 s
TX node=8 command=PUMP_ON  frame=04 06 08 32 67
RX node=8 response=0x5A expected=0x5A rtt=47 ms   -> PUMP_ON accepted
...
TX node=8 command=PUMP_OFF frame=04 07 08 33 F7
RX node=8 response=0x5A expected=0x5A rtt=47 ms   -> PUMP_OFF accepted

TX node=8 command=PING attempt=1/3 frame=05 05 A5 08 6A 7F
TX node=8 command=PING attempt=2/3 frame=05 05 A5 08 6A 7F
TX node=8 command=PING attempt=3/3 frame=05 05 A5 08 6A 7F
PING node=8 response=0x00 result=1 (TIMEOUT) rtt=300 ms attempt=3/3
```

Facts derived from the bytes above:

- Every transmitted frame is a valid CRC16-Modbus frame; the whole-frame
  remainder of both `04 06 08 32 67` and `05 05 A5 08 6A 7F` is `0x0000`.
- The node parses and ACKs `0x06`/`0x07`, so the CRC, length byte, UART
  framing and addressing are all correct for that firmware.
- PING returns **zero** bytes for the full 300 ms window on all three attempts
  (`rtt` equals the timeout exactly, no `RX` line is emitted). The node is not
  sending a wrong echo; it is not replying at all.
- Therefore the gateway PING frame encoding is not the defect. The deployed
  node 8 firmware accepts the `0x05` frame as CRC-valid but exposes no `0x05`
  response path.

**Consequence:** the "verified AGU transaction set" claim in
`ATMEGA8_INTEGRATION_BOUNDARY.md` and `interface-wire-contract.md` is not met
for `PING (0x05)` on this firmware revision. Liveness must not rely on `0x05`
until a node firmware revision is confirmed to echo it. Use `0x0E`
READ_RAM_BURST (once verified on the same nodes) or a successful `0x5A` ACK as
the liveness signal, and treat PING as UNVERIFIED for the deployed units.

### Node 08 golden frames (verified 2026-09-27)

The node-8 frames were recomputed independently with the CRC16-Modbus
algorithm and match both the deployed log and the codec:

| Transaction | Wire bytes | Expected reply |
|---|---|---|
| `PUMP_ON` node 8 | `04 06 08 32 67` | `0x5A` ACK |
| `PUMP_OFF` node 8 | `04 07 08 33 F7` | `0x5A` ACK |
| `PING` node 8 | `05 05 A5 08 6A 7F` | no reply on deployed firmware |

The same recomputation exposed a defect in the `rfdiag` CLI path: the
hard-coded node-7 PING frame used trailer `2B B8`, whose whole-frame CRC
remainder is non-zero, so a real node rejects it before parsing. It now carries
the correct trailer `2A 7B` (`05 05 A5 07 2A 7B`).

## Implementation and Regression Coverage

- `aeroponics-firmware/src/agu_legacy_codec.cpp` formats every SendCom frame
  through `core/Crc16Modbus.h` and validates burst responses fail-closed.
- `aeroponics-firmware/src/agu_legacy_rf_host.cpp` waits for the 11-byte burst
  envelope while preserving ACK transaction behavior.
- Production tests assert the exact pump vectors, command CRCs, burst length and
  CRC, rejection of old zero-sum frames, ACK recognition, and framed-ID parsing.
- Node-08 tests (`test_agu_node08_*` in `test_production.cpp`) pin the three
  golden frames above and the response policy: an exact echo is the only PING
  pass, while `0x5A`, `0x00`, and silence are each surfaced as a distinct
  non-ACKED result with the frame retransmitted byte-for-byte.
- The hardware tool `tools/test_agu_rf_e2e.py` gained `link-test`, which
  reproduces the `on <node> <secs>` CLI flow and judges pass/fail on the
  `0x06`/`0x07` ACK path while classifying each `0x05` probe
  (`ECHO_OK`/`ACK_NOT_ECHO`/`NACK`/`TIMEOUT`). `--require-ping` opts in to
  failing on a missing echo for firmware that implements it. Its offline
  vectors run with `python3 -m unittest test_agu_rf_e2e_test`.
- The modern `RfFrameCodec` and treatment/NVS checksum implementations are not
  changed by this migration.
