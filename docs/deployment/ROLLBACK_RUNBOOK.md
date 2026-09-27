# Rollback Runbook — CRC16-Modbus Migration (H3/H6)

Field rollback from `RF_PROTOCOL_VERSION = 0x02` (CRC16-Modbus) to `0x01`
(CRC16/CCITT-FALSE). Target completion: **under 5 minutes**.

**Status: PRE-DEPLOY.** Runbook exists and is reviewed; binaries not yet built
(see `release/ROLLBACK_BINARY_MANIFEST.md`). Do not deploy until H3 passes.

## Prerequisites (blocking)

| # | Prerequisite | Evidence |
|---|---|---|
| 1 | Rollback binaries exist on offline media with recorded checksums | `release/rollback/SHA256SUMS` |
| 2 | Rollback gateway binary verified to boot and report `0x01` | bench boot log |
| 3 | Bench test passed with the exact binaries to be deployed | `S2_E_4NODE_BENCH_REPORT.md` |
| 4 | NVS snapshot backup taken in the last 24 h | backup path + timestamp |

If any prerequisite is missing, **do not roll back** — investigate first. A
partial rollback is worse than no rollback, because the version gate is
fail-closed.

## Why rollback must be atomic

The version gate rejects frames from a mismatched peer:

- `0x01` gateway + `0x02` nodes → gateway rejects node frames
- `0x02` gateway + `0x01` nodes → nodes reject gateway frames

Either way every command times out. The stale watchdog then forces Safe-OFF
after 15 s (`RF_STALE_THRESHOLD_MS`), so pumps stop, but telemetry, scheduling
and manual control are unavailable for the whole window. Flash the gateway and
all nodes in the **same** maintenance window.

## Procedure

### Step 1 — Quarantine and safe-off (≤ 1 min)

1. Stop new commands at the control plane (pause manual override and schedules).
2. Confirm all nodes are in a known-safe state: pumps OFF.
3. If any pump is ON, send OFF and confirm telemetry reports OFF before
   continuing. Do not proceed with a pump in an unknown state.

### Step 2 — Flash the rollback pair (≤ 3 min)

```bash
cd aeroponics-firmware
pio run -t upload -e esp32-s3-devkitc-1   # gateway -> v0x01
pio run -t upload -e atmega8-node-4
pio run -t upload -e atmega8-node-5
pio run -t upload -e atmega8-node-6
pio run -t upload -e atmega8-node-7
```

Flash from the offline rollback artifacts, not from a fresh `HEAD` build.
Compare checksums before flashing:

```bash
shasum -a 256 -c release/rollback/SHA256SUMS
```

### Step 3 — Verify (≤ 1 min)

| Check | Expected | Fail action |
|---|---|---|
| Gateway boot log | `RF_PROTOCOL_VERSION == 0x01` | Stop. Binary mismatch — reflash correct artifact |
| Node ACK per ID `4..7` | `0x5A` within 300 ms | Stop. Identify the silent node; reflash or replace |
| PING round-trip | ACK + valid response | Investigate RF link before proceeding |
| Telemetry to MQTT | messages arriving per node | Check broker and gateway WiFi |
| Stale events | none after settle (15 s) | A silent node means partial rollback — continue flashing |

Every check must pass. One non-ACKing node means the fleet is split across
protocol versions, which is the fail-closed state described above.

### Step 4 — Settle (≤ 5 min)

1. Send `PUMP ON` → bounded lease → `PUMP OFF` on one node and confirm the
   round trip.
2. Confirm no unexpected auto-ON after the settle period.
3. Resume schedules only after the round trip is confirmed.

## Recovery if rollback fails

If Step 3 fails after gateway flash: **stop flashing**, revert the gateway to
the `0x02` artifact and leave the fleet on `0x02`. A known-bad `0x02` fleet that
is failing observably is safer than a half-rolled `0x01`/`0x02` split with no
command path.

```bash
pio run -t upload -e esp32-s3-devkitc-1   # restore the 0x02 gateway artifact
```

Do not attempt to "fix" the split by editing `AGU_LEGACY_MIN/MAX_NODE_ID` or
relaxing the version gate to accept mixed versions. Both relaxations remove the
fail-closed property and must never be used as a production workaround.

## NVS recovery

Storage checksum is frozen to CCITT-FALSE in `treatment_manager` and was never
migrated, so no NVS restore is required for a protocol rollback. Treatment
snapshots written by `0x02` remain readable by `0x01`.

If a snapshot is nevertheless corrupt, restore from backup:

```bash
# 1. Restore the verified NVS snapshot
# 2. Re-verify treatment integrity
# 3. Confirm gateway logs no storage verification failure
```

## Sign-off

Rollback is not complete until the deployment report records: binary checksums
actually flashed, boot-log version, per-node ACK list, the settle-window
observation, and signatures from Firmware, QA and DevOps leads.
