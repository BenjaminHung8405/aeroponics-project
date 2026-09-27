# Firmware Baseline Report

**Captured:** 2026-09-26
**Working tree:** `3eedb84` plus the pre-existing uncommitted planning and PlatformIO changes

## Sequential test results

The PlatformIO test environments were run sequentially to avoid sharing a native build directory between concurrent jobs.

| Command | Collected | Result | Notes |
|---|---:|---|---|
| `pio test -e native -f test_crc16` | 9 | PASS | All CRC16-Modbus vectors and bounds tests passed |
| `pio test -e native -f test_fsm` | 21 | PASS | No FSM regressions observed |
| `pio test -e native -f test_production` | 204 | ERROR | 106 passed, 97 failed, then SIGSEGV |
| `pio test -e native` | 234 | ERROR | 136 passed, 97 failed, then SIGSEGV |

The multiline `test_filter` currently collects all three suites. The total is 234 tests in the current checkout, not the older planning estimate of 300+.

## Failure classification

The 97 production failures are retained as a baseline limitation and are not reported as a CRC migration regression:

1. **Topology/address mismatch:** production tests still exercise assumptions tied to the old `4..7` node boundary, while the execution plan now requires modern `1..15` support and a separate AGU legacy `4..7` boundary.
2. **Fixture/setup dependency failures:** many command, registry, scheduler, telemetry, calibration, and closed-loop failures occur after setup rejects a node or frame; these need re-triage after address predicates are migrated.
3. **Pre-existing crash:** the production suite terminates with SIGSEGV in the C4 configurable per-node/treatment provenance region after `test_c4_configurable_per_node_and_treatment_provenance_isolation` reports a failure. The crash is not hidden or skipped.
4. **Other:** any failure that remains after topology and fixture updates must receive an individual reproduction and owner before a release gate can be green.

## Release limitations

- `test_crc16` and `test_fsm` are green.
- `test_production` is not a release gate yet because its 97 failures and SIGSEGV are not fully classified.
- No legacy AGU wire behavior was changed; real-device capture is still required before any Sprint 3B codec migration.
- Gateway and ATmega8 build evidence must be refreshed after the address expansion changes.
