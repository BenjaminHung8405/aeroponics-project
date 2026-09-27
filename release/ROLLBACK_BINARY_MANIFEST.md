# Rollback Binary Manifest (H3)

Offline rollback artifacts for the CRC16-Modbus migration. The release is
**blocked** until the binaries below are built from the `pre-crc16-modbus`
rollback anchor, checksummed, and verified bootable on the bench.

**Status: NOT BUILT.** The firmware directory contains no `.bin` artifacts, so
this gate is not met.

## Rollback anchor

| Item | Value |
|---|---|
| Tag | `pre-crc16-modbus` |
| Resolves to | `796248b^` (pre-`RF_PROTOCOL_VERSION` bump) |
| Protocol version | `0x01` (CRC16/CCITT-FALSE) |

The rollback pair must be built from the anchor, not from `HEAD`. A binary built
from current `HEAD` carries `RF_PROTOCOL_VERSION = 0x02` and is not a rollback
artifact.

## Build procedure

```bash
git checkout pre-crc16-modbus
cd aeroponics-firmware
pio run -e esp32-s3-devkitc-1      # gateway
pio run -e atmega8-node-4          # node
pio run -e atmega8-node-5
pio run -e atmega8-node-6
pio run -e atmega8-node-7
git checkout -
```

Copy the results to offline media and record checksums:

```bash
mkdir -p release/rollback
cp .pio/build/esp32-s3-devkitc-1/firmware.bin release/rollback/pre-crc16-modbus-gateway.bin
for n in 4 5 6 7; do
    cp ".pio/build/atmega8-node-$n/firmware.bin" "release/rollback/pre-crc16-modbus-node-$n.bin"
done
shasum -a 256 release/rollback/*.bin > release/rollback/SHA256SUMS
```

## Artifact checklist

| Artifact | Env | Version | Verified on bench |
|---|---|---|---|
| `pre-crc16-modbus-gateway.bin` | `esp32-s3-devkitc-1` | `0x01` | ☐ |
| `pre-crc16-modbus-node-4.bin` | `atmega8-node-4` | `0x01` | ☐ |
| `pre-crc16-modbus-node-5.bin` | `atmega8-node-5` | `0x01` | ☐ |
| `pre-crc16-modbus-node-6.bin` | `atmega8-node-6` | `0x01` | ☐ |
| `pre-crc16-modbus-node-7.bin` | `atmega8-node-7` | `0x01` | ☐ |
| `SHA256SUMS` | — | — | ☐ |

Fill the version column from the build log, not from expectation. A mismatch
means the wrong source tree was built and the artifacts are invalid.

## Bench verification

1. Flash the rollback gateway, confirm the boot log reports
   `RF_PROTOCOL_VERSION == 0x01`.
2. Flash one rollback node, confirm a `PUMP_ON` command receives a `0x5A` ACK
   under the legacy CCITT-FALSE contract.
3. Confirm the storage checksum boundary is unaffected: treatment NVS snapshots
   written by the `0x02` build remain readable. Storage CRC was never migrated,
   so no NVS restore is required for rollback.

## Rollback procedure

```bash
git checkout pre-crc16-modbus
cd aeroponics-firmware
pio run -t upload -e esp32-s3-devkitc-1
pio run -t upload -e atmega8-node-4
```

Flash the gateway and all nodes in the same window. A version mismatch is
fail-closed by design: a `0x01` gateway rejects `0x02` nodes and vice versa, so
a partial rollback leaves every command timing out and the stale watchdog forces
Safe-OFF within 15 s.

Full field procedure: `docs/deployment/ROLLBACK_RUNBOOK.md`.
