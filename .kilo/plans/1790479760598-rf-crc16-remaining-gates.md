# ESP32 RF CRC16 — Remaining Hardware & Release Gates

## Conclusion

All software changes called out by `.ai/planning/esp32-rf-crc16/EXECUTION_MASTER_PLAN.md` and `.kilo/plans/1790414625182-esp32-rf-crc16-execution-plan.md` are already in the working tree. The current release gate status is: **software green, hardware/field gates pending**.

Evidence captured in this repo:

- `RELEASE_GATE_REPORT.md`: software gates pass; hardware/field gates listed as blockers.
- `docs/LEGACY_WIRE_EVIDENCE.md`: authoritative Delphi/AVR wire evidence recorded.
- `database/002_expand_modern_node_topology_rollback.md`: rollback note for live restore.

## What Remains (non-software gates)

| ID | Item | Owner | Blocker | Artifact to produce |
|---|---|---|---|---|
| H1 | Physical legacy wire capture | Firmware/QA | **Hardware** | `docs/legacy_wire_captures/pump_on_nodeX.csv`, `ping.csv`, `read_burst.csv` |
| H2 | Legacy group-address behavior (`$14` ACK, expected response size) | Firmware/QA | **Hardware** | `docs/LEGACY_WIRE_EVIDENCE.md` section `S3A-GATE-03/04` |
| H3 | Pre-build rollback v0x01 binary | DevOps/Firmware | **Hardware** | `firmware/pre-crc16-modbus-gateway.bin`, `pre-crc16-modbus-node-4.bin` |
| H4 | Bench test with 4 physical nodes | Firmware/QA | **Hardware** | `S2_E_4NODE_BENCH_REPORT.md` + `WALKTHROUGH_LOG.md` update |
| H5 | Live DB migration execution | DevOps/Backend | **Infrastructure** | `migration_run_evidence_<timestamp>.log` |
| H6 | Maintenance-window field deployment | DevOps/Firmware/QA | **Hardware** | Deployment report with 3-lead sign-off |
| H7 | 24–48h post-deploy observability | DevOps/QA | **Hardware** | Grafana/MQTT metric export + deployment report |

## Verification Checklist (closed in software)

Verified in the current working tree:

- `RF_PROTOCOL_VERSION == 0x02` (no hardcode)
- `RfFrameCodec::calculateCrc16` delegates to `calculateCrc16Modbus`
- `calculateStorageCrc16` uses `0x1021` CCITT-FALSE and is isolated from RF codec
- Modern address space `1..15`, gateway `0x00`, legacy AGU `4..7`
- Group targets rejected on modern wire (`isValidTargetAddress` / `isValidSourceAddress`)
- AGU legacy codec uses CRC16-Modbus, 11-byte burst envelope, old zero-sum rejected
- Golden vectors asserted: PUMP_ON node 9 `04 06 09 F3 A7`, PUMP_OFF node 9 `04 07 09 F2 37`
- Fuzz test: 2,500 deterministic mutations for modern codec
- `test_crc16` 9/9, `test_fsm` 21/21, `test_production` 278/278, `test_rf_address` 7/7, bare gate 315/315
- Backend DTO `SendPumpCommandDto` forbids mixed `NODE + group_id` / `GROUP + node_id`
- `control_slots` unique per `(device_id, target_type, target_id)`, `target_type IS NOT NULL` guard present
- Schema.sql, TypeORM migrations `CreateControlSlots` and `ExpandModernNodeTopology` present
- Docs `RF_PROTOCOL.md`, `interface-wire-contract.md` updated; zero CCITT-as-deployed references remain

## Exact Remaining Execution Path

### 1. Close software QA sign-off (read-only)

Run the sequential gate in `aeroponics-firmware/`:

```bash
pio test -e native
pio test -e native-prototype
pio run -e native-integration
pio run -e esp32-s3-devkitc-1
pio run -e atmega8-node-4
```

Capture pass/fail counts into `docs/QA_ACCEPTANCE_REPORT_4_NODES.md` append.

### 2. Commit plan doc hygiene

Staged docs-only changes are pending in the worktree:

- `.ai/planning/esp32-rf-crc16/sprint_0.md`
- `.ai/planning/esp32-rf-crc16/sprint_2.md`
- `.ai/planning/esp32-rf-crc16/sprint_4.md`
- `.ai/planning/esp32-rf-crc16/sprint_5.md`
- `.kilo/plans/1790414625182-esp32-rf-crc16-execution-plan.md`
- `database/002_expand_modern_node_topology_rollback.md`

**Action:** commit these corrections before any other release work so rollback anchor history is clean.

### 3. Execute DB migrations (manual)

Run in a controlled window after DB backup:

```bash
cd aeroponics-backend
npx typeorm migration:run
```

Verify `control_slots`, `node_registry`, `group_node_assignments` constraints for `node_id 1..15`. Store evidence in `database/migration_run_evidence_<timestamp>.log`.

### 4. Build rollback binaries (H3)

```bash
git checkout pre-crc16-modbus
pio run -e esp32-s3-devkitc-1
pio run -e atmega8-node-4
# Save artifacts and document checksums
```

### 5. Hardware bench (H1–H4, manual)

On bench with 4 ATmega8 nodes:

- Capture raw UART/IO traces for PUMP_ON, PING, READ burst into `docs/legacy_wire_captures/`.
- Measure `$14` group address behavior and actual `expectedResponseSize`.
- Run bench sequence: PUMP OFF → bounded PUMP ON lease → OFF → PING → HEARTBEAT → corrupted frame injection → reboot recovery.
- Store evidence in `S2_E_4NODE_BENCH_REPORT.md` and `WALKTHROUGH_LOG.md`.

### 6. Field rollout (H5–H7, manual)

- Flash gateway + nodes in one maintenance window.
- Verify per-node ACK for `1..15`.
- Monitor CRC error rate, stale rate, telemetry volume, retries, pump outcomes for 24–48h.
- Produce `docs/deployment/DEPLOYMENT_REPORT_CRC16_MODBUS.md` with 3-lead sign-off.
- Tag `rf-crc16-migration-complete` only after report approval.

## Important: Do Not Merge Code Before Hardware Evidence

Do not call release green until H1–H4 artifacts exist. The Delphi/AVR source evidence is strong, but the master plan explicitly gates production release on physical wire capture.
