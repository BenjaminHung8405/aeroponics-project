# Release Gate Report

**Captured:** 2026-09-26
**Updated:** 2026-09-27 (sequential gate re-run on the current worktree)
**Status:** Software gates pass; hardware and field gates remain blocked.
**Rollback anchor:** `pre-crc16-modbus` (`796248b^`)

> **Correction (DEF-01 resolved):** `platformio.ini` `test_filter` is newline-separated (commit `434c80c`), so bare `pio test -e native` is the authoritative gate. The CLI comma form `-f test_crc16,test_fsm,test_production` parses as one glob and silently reports **0 test cases with exit 0** (false green) — never use it as a gate.

## Firmware gates

Run sequentially in `aeroponics-firmware/` to avoid PlatformIO build-directory races:

| Gate | Result |
|---|---|
| `pio test -e native -f test_crc16` | PASS, 9/9 |
| `pio test -e native -f test_fsm` | PASS, 21/21 |
| `pio test -e native -f test_production` | PASS, 277/277 |
| `pio test -e native -f test_production` (post legacy CRC16) | PASS, 278/278 |
| `pio test -e native -f test_rf_address` | PASS, 7/7; includes 2,500 deterministic mutations |
| `pio test -e native` (bare, all 4 suites) | PASS, 315/315 |
| `pio test -e native-prototype` | PASS, 23/23 |
| `pio run -e native-integration` | SUCCESS (build-only Mosquitto harness; not a test suite) |
| `pio run -e esp32-s3-devkitc-1` | PASS |
| `pio run -e atmega8-node-4` | PASS; flash 6358/7000, RAM 301/900 |
| `python3 tools/test_agu_rf_e2e.py test-crc16` | PASS (offline CRC16-Modbus + capture-classifier self-test) |

## Control-plane/UI gates

| Gate | Result |
|---|---|
| `aeroponics-backend`: `npm test -- --runInBand` | PASS, 42 suites / 364 tests |
| `aeroponics-backend`: `npm run test:e2e -- --runInBand` | PASS, 82 tests |
| `aeroponics-backend`: `npm run build` | PASS |
| `aeroponics-ui`: `npm run type-check` | PASS |
| `aeroponics-ui`: `npm test` | PASS, 48 tests |
| `git diff --check` | PASS |

## Legacy AGU CRC16 migration

- `aeroponics-firmware/src/agu_legacy_codec.cpp` now frames every AGU SCI
  command through `core/Crc16Modbus.h` (`[length=payloadLen+2][payload][crc_lo][crc_hi]`)
  and validates burst responses fail-closed against an 11-byte CRC16 envelope.
- `agu_legacy_rf_host.cpp` waits for the 11-byte burst envelope.
- Golden vectors: PUMP_ON node 9 `04 06 09 F3 A7`, PUMP_OFF node 9 `04 07 09 F2 37`;
  read-burst sample `0A 0A 14 1E 28 32 3C 46 50 3F 7E`.
- Old one-byte zero-sum frames are rejected; no compatibility fallback exists.
- `tools/test_agu_rf_e2e.py` TX/verify/mock-node paths were migrated to CRC16-Modbus;
  offline self-test `test-crc16` passes.
- Modern `RfFrameCodec`, treatment-storage CCITT, calibration CRC32, and EEPROM XOR
  checksums are unchanged and remain distinct from the AGU wire CRC.

## Database migration status

- `1726200009000-CreateControlSlots.ts` creates device-scoped four-slot persistence with target-type/range checks and a per-device unique target index.
- `1726200010000-ExpandModernNodeTopology.ts` expands node constraints to `1..15` and blocks rollback when non-legacy rows exist.
- `database/schema.sql` mirrors both the `control_slots` table and modern node range.
- The migrations have not been executed against a live PostgreSQL/TimescaleDB instance in this environment. Execute `typeorm migration:run` in a controlled database window and verify constraints before deployment.

## Blocking gates

The release is not green until these evidence-producing gates are completed:

1. The supplied Delphi `TSCI.SendComCRC16` implementation and legacy AVR routine establish the AGU CRC algorithm as CRC16-Modbus: init `0xFFFF`, reflected polynomial `0xA001`, length includes two CRC bytes, and wire order is `[CRC_LO][CRC_HI]`. Raw logic-analyzer/UART captures for PUMP, PING, and READ burst are still required to close the physical-wire evidence gate.
2. Group-address (`$14`) ACK behavior and per-command `expectedResponseSize` remain unverified against real deployed nodes.
3. Build and bench-verify matching v0x01 rollback binaries and v0x02 gateway/node binaries.
4. Flash a complete modern fleet in one maintenance window and record per-node ACK/safe-off evidence.
5. Observe CRC errors, stale events, retries, and pump outcomes for 24–48 hours; create the deployment report before tagging completion.
