# Legacy Wire Captures (S3A-GATE-01)

Raw physical evidence for the AGU legacy southbound wire contract. The directory
is intentionally empty until real hardware is attached: a capture file that was
not produced by a connected node is not evidence.

**Gate status: PENDING HARDWARE.** No CSV files exist yet, therefore
`S3A-GATE-01` is not met.

## How to produce a capture

Attach a CP2102 to the gateway UART and a powered ATmega8 node on the RF path,
then from the repository root:

```bash
python3 tools/test_agu_rf_e2e.py capture \
    --nodes 4,5,6,7 \
    --out docs/legacy_wire_captures
```

Each scenario writes one append-only CSV per node, named
`<scenario>_node<id>.csv`, where scenario is one of:

| Scenario | Payload sent | Expected reply |
|---|---|---|
| `pump_off` | `04 07 <id>` + CRC16 | single `5A` ACK |
| `pump_on` | `04 06 <id>` + CRC16 | single `5A` ACK |
| `ping` | `05 A5 <id>` + CRC16 | single `5A` ACK |
| `read_burst` | `0E .. 08 <id>` + CRC16 | 11-byte burst, length `0x0A` |

## CSV column contract

| Column | Meaning |
|---|---|
| `timestamp_iso` | wall-clock time of the byte transaction |
| `elapsed_ms` | milliseconds since capture start |
| `scenario`, `node_id`, `direction` | capture context (`TX`/`RX`) |
| `byte_count`, `hex_data`, `declared_length` | the raw bytes on the wire |
| `crc_ok`, `framing_ok` | independent verdicts computed from the bytes alone |
| `note` | transaction label |

`crc_ok` is false for a bare `5A` ACK because a single byte carries no CRC
trailer; ACK recognition is decided by `framing_ok`. A burst reply must be
exactly 11 bytes with `declared_length == 10`.

## What closes the gate

Capture CSVs must exist for `pump_on`, `ping`, and `read_burst` against at least
one deployed node, and every `TX` row must show `crc_ok=1` with the frame bytes
matching `04 06 <id> <crc_lo> <crc_hi>`. Record the hardware revision and the
node firmware revision alongside the files before review.

## Re-verification without hardware

The classification logic is covered by the tool's offline self-test:

```bash
python3 tools/test_agu_rf_e2e.py test-crc16
```
