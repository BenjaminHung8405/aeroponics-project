# Sprint 6: Field Rollout, Observability & Post-Deploy Verification

> **Phụ thuộc:** Sprint 5 approved (docs + release gate green).
> **Mục tiêu:** An toàn triển khai production, theo dõi post-deploy, và sẵn sàng rollback.
> **Đối tượng:** DevOps lead + Firmware lead + QA lead cùng review.

---

## 1. BỐI CẢNH & RỦI RO TRIỂN KHAI

CRC-16/MODBUS migration thay đổi **hành vi wire trên every RF frame**. Khi `RF_PROTOCOL_VERSION` bump lên `0x02`:
- Gateway mới (v0x02 + CRC Modbus) **từ chối** frame từ node cũ (v0x01 + CRC CCITT) — trả `UNSUPPORTED_VERSION`.
- Node cũ (v0x01 + CRC CCITT) **từ chối** frame từ gateway mới — `UNSUPPORTED_VERSION`.
- Kết quả: **không lệnh nào thực thi được** nếu hai đầu lệch phiên bản.

Đây là **fail-closed design** — đúng ý đồ an toàn (lệnh không chạy thay vì chạy sai), nhưng cần đảm bảo deployment đồng bộ.

### 1.1 Worst-case scenario

```
Gateway flash v0x02 ──► Node chưa flash ──► EVERY command timeout
                                               ├── PUMP_ON → TIMED_OUT
                                               ├── Stale threshold 15s → SAFE_OFF
                                               └── Nozzle dry → crop damage
```

**Mitigation:** Gateway + nodes flash **trong cùng maintenance window**, hoặc rollback gateway về v0x01.

---

## 2. DEPLOYMENT STRATEGY

### 2.1 Deployment Order (Recommended)

```
Phase A: Maintenance window (manual, controlled)
  1. Flash gateway ESP32-S3 ──► v0x02 + CRC Modbus
  2. Flash ATmega8 nodes 4,5,6,7 ──► (nếu có firmware mới)
  3. Verify: gateway log shows ACK from all 4 nodes
  4. If ANY node fails → rollback gateway to v0x01

Phase B: Monitoring window (24-48 hours)
  5. Watch CRC error rate, stale alerts, pump on/off events
  6. Compare telemetry volume pre/post migration
  7. No anomalies → migration COMPLETE
```

### 2.2 Dual-Version Window

| Scenario | Duration Allowed | Risk | Action |
|---|---|---|---|
| Gateway v0x02, Nodes v0x01 | **0 seconds** (fail-closed) | Commands timeout | Immediate rollback gateway |
| Gateway v0x01, Nodes v0x02 | **0 seconds** (fail-closed) | Nodes reject all | Flash nodes back to v0x01 |
| Gateway v0x02, Nodes v0x02 | **∞ (normal)** | None | Target state |
| Gateway v0x01, Nodes v0x01 | **∞ (rollback)** | Legacy CRC | Pre-migration baseline |

**Critical:** The version gate ensures there is **never** a state where mismatched firmware silently executes commands. This is by design (S2-HARD-01).

### 2.3 Rollback Procedure (Field)

```
Rollback Steps (target: < 5 minutes):
  1. Power-cycle gateway (NVS preserves WiFi config)
  2. Flash gateway with `git checkout pre-crc16-modbus` build
  3. Verify: `RF_PROTOCOL_VERSION == 0x01` in boot log
  4. Verify: nodes respond to legacy CRC frames
  5. Resume normal operation
  6. File incident report: why rollback, timeline, impact
```

**Prerequisite:** A pre-built v0x01 binary must be available offline (not built from HEAD which is v0x02).

---

## 3. TASKS

### TRACK A — Pre-Deploy Verification

**TASK S6-T1 Pre-build rollback binary**
- Build firmware from `git tag pre-crc16-modbus` (v0x01 + CRC CCITT).
- Save binary: `firmware/pre-crc16-modbus-gateway.bin`, `firmware/pre-crc16-modbus-node-4.bin`, etc.
- Verify binary on bench: flash, boot, verify `RF_PROTOCOL_VERSION == 0x01`.
- Store in physically accessible USB drive or offline storage (not just git).

**TASK S6-T2 Maintenance window checklist**
- Checklist document:
  ```
  [ ] Backup current node config (NVS dump)
  [ ] Verify pre-built rollback binary exists and boots
  [ ] Flash gateway v0x02
  [ ] Flash node 4 (ATmega8)
  [ ] Flash node 5
  [ ] Flash node 6
  [ ] Flash node 7
  [ ] Verify: all 4 nodes ACK within 10s
  [ ] Verify: PUMP_ON/OFF roundtrip functional
  [ ] Verify: telemetry flowing to MQTT
  [ ] Mark migration COMPLETE
  ```

**TASK S6-T3 Pre-deploy bench test**
- On bench with all 4 ATmega8 nodes:
  1. Flash gateway v0x02 + nodes v0x02
  2. Execute: `SET_PUMP(OFF)` → `SET_PUMP(ON, 10s lease)` → verify flow sensor
  3. Execute: `PING` → `HEARTBEAT` → verify RSSI/uptime
  4. Inject 1000 random corrupted frames → verify 0 false-positive actuations
  5. Reboot gateway mid-command → verify safe recovery

### TRACK B — Post-Deploy Observability

**TASK S6-T4 CRC error rate monitoring**
- Gateway firmware already increments `UartTransportStats::crc_errors` on every `CRC_MISMATCH`.
- **Metric:** CRC error rate = `crc_errors / total_frames_received` per 5-minute window.
- **Threshold:** < 0.1% = normal; 0.1-1.0% = warning (investigate); > 1.0% = alert (possible firmware mismatch).
- **Dashboard:** Grafana/MQTT → `aeroponics/device/{device_id}/stats/crc_error_rate`.

**TASK S6-T5 Stale alert integration**
- Gateway already marks nodes `STALE` after `RF_STALE_THRESHOLD_MS = 15000ms`.
- **Post-migration:** If stale rate increases compared to pre-migration baseline → investigate.
- **Comparison metric:** Average `last_seen_ms` gap per node per hour.

**TASK S6-T6 Telemetry volume comparison**
- Before migration: record baseline telemetry volume (messages/hour per node).
- After migration: compare — significant drop indicates command failure or frame rejection.
- **Threshold:** < 5% variation = normal; > 5% = investigate.

### TRACK C — EMC & Regulatory

**TASK S6-T7 RF link budget verification**
- Measure RSSI for each node at production mounting distance.
- Compare with pre-migration values (should be identical — CRC change does not affect PHY).
- **Acceptance:** RSSI variation < 3 dB (normal environmental variation).

**TASK S6-T8 Regulatory compliance confirmation**
- CRC migration does **NOT** change RF physical parameters (frequency, power, modulation).
- Regulatory reference: Thông tư 08/2021/TT-BTTTT (433 MHz, 25mW max).
- **Action:** Confirm in deployment report that no re-certification required.

### TRACK D — Safety & FMEA

**TASK S6-T9 FMEA impact analysis**
- CRC change affects **integrity check** only, not safety-critical paths:
  - Stale safe-off: **unchanged** (gateway-initiated, independent of node CRC).
  - Lease expiry: **unchanged** (node timer, independent of CRC algorithm).
  - Emergency stop: **unchanged** (GPIO-level or MQTT-initiated).
- **Conclusion:** CRC migration has **no impact** on safety-critical FMEA paths (FMEA-01..16 in `RF_FLOW_POC_FMEA.md`).

**TASK S6-T10 Post-deploy regression verification**
- 24 hours after deployment, run full test suite (if bench available):
  ```bash
  pio test -e native
  pio run -e esp32-s3-devkitc-1
  pio run -e atmega8-node-4
  ```
- Compare results with Sprint 5 release gate — must be identical.

### TRACK E — Documentation & Sign-off

**TASK S6-T11 Deployment report**
- Template:
  ```
  # Deployment Report: CRC16-MODBUS Migration
  Date: [date]
  Operator: [name]
  
  ## Firmware Versions
  - Gateway: v0x02 + CRC Modbus (commit [hash])
  - Node 4: v0x02 + CRC Modbus (commit [hash])
  - ...
  
  ## Pre-Deploy Bench Test: PASS/FAIL
  ## Deployment Window: [start] - [end] ([duration])
  ## Post-Deploy Verification: PASS/FAIL
  
  ## Metrics
  - CRC error rate: [X%]
  - Telemetry volume: [baseline → current]
  - Stale events: [count]
  
  ## Incident: [none / description]
  ## Sign-off: [Firmware Lead] [QA Lead] [DevOps Lead]
  ```

**TASK S6-T12 Archive planning artifacts**
- Tag: `rf-crc16-migration-complete` at final commit.
- Archive: `EXECUTION_MASTER_PLAN.md`, `WALKTHROUGH_LOG.md`, `BASELINE_REPORT.md`, `LEGACY_WIRE_EVIDENCE.md`.
- Ensure `docs/RF_PROTOCOL.md` and `docs/interface-wire-contract.md` are in final state.

---

## 4. TIÊU CHUẨN RÀ SOÁT CỨNG

1. **Bench test mandatory (S6-HARD-01):** Không flash production nếu bench test (S6-T3) chưa PASS. Không ngoại lệ.
2. **Rollback binary available (S6-HARD-02):** Pre-built v0x01 binary must exist offline AND verified bootable. Nếu binary missing → BLOCK deployment.
3. **All 4 nodes ACK (S6-HARD-03):** Trong maintenance window, nếu BẤT KỲ node nào không ACK trong 10s → rollback. Không "thử lại sau" trong production.
4. **CRC error rate baseline (S6-HARD-04):** Post-deploy CRC error rate must be < 0.1%. Nếu ≥ 0.1% trong 24h → investigate, possibly rollback.
5. **No safety regression (S6-HARD-05):** Stale safe-off and lease expiry behavior must be verified identical to pre-migration. Run `test_fsm` → 21/21 PASS.
6. **Deployment report signed (S6-HARD-06):** Migration không coi là hoàn tất cho tới khi deployment report có chữ ký 3 lead (Firmware, QA, DevOps).

---

## 5. INCIDENT RESPONSE

| Signal | Threshold | Response |
|---|---|---|
| Node no ACK after flash | Any node | Immediate rollback gateway to v0x01 |
| CRC error rate > 1% | 5-min window | Investigate; prepare rollback |
| Stale rate > 2x baseline | 1-hour window | Investigate RF link or firmware |
| Pump unexpected ON/OFF | Any | **EMERGENCY STOP** → rollback → root cause |
| Telemetry volume drop > 5% | 24-hour comparison | Investigate command path |

---

## 6. KẾT QUẢ DỰ KIẾN

- Pre-built rollback binary available offline.
- All 4 nodes ACK within maintenance window.
- Post-deploy metrics within thresholds for 48 hours.
- Deployment report signed by 3 leads.
- Migration tag `rf-crc16-migration-complete` created.
- `WALKTHROUGH_LOG.md` updated with deployment evidence.
