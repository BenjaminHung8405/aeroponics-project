# RF Scan Correlation Fix Plan

## Root Cause

The failure is not an MQTT transport loss. The gateway receives the scan command and publishes a result, but the result uses `scan_cmd` instead of the backend request ID (`scan_<timestamp>`).

Code evidence:

- Backend publishes `{ scan_id, command_id }` but currently omits the required command envelope `version` in `aeroponics-backend/src/node/node.service.ts`.
- Firmware validates the envelope in `_hasValidCommandEnvelope()`, which requires both a valid `command_id` and `version > 0`.
- Firmware `_enqueueGatewayScanCommand()` falls back to `scan_cmd` when validation fails.
- Backend then correctly rejects the result because its `scan_id` does not match the active request.

The primary fix is to make the scan command conform to the MQTT command envelope and preserve the ID end-to-end. The fallback must not silently convert a malformed UI request into a correlated scan.

## Decisions

1. Use one correlation ID: backend `scanId` is copied into both MQTT `command_id` and `scan_id`.
2. Add `version: 1` to the backend scan command payload; this is required by the existing firmware envelope validator.
3. Firmware scan parsing must reject a malformed/missing envelope and publish a rejection/diagnostic event; it must not use `scan_cmd` for production UI requests.
4. Keep `scan_cmd` only for an explicitly documented CLI/bench compatibility path, not for MQTT production commands.
5. Keep backend correlation matching strict on both `deviceId` and `payload.scan_id`.
6. Keep timeout responses semantically correct: `online: null` means unknown; do not convert correlation timeout to offline.

## Implementation Tasks

### 1. Backend command contract

File: `aeroponics-backend/src/node/node.service.ts`

- Change gateway scan publish payload to:
  ```json
  {
    "command_id": "scan_<timestamp>",
    "scan_id": "scan_<timestamp>",
    "version": 1
  }
  ```
- Preserve the existing `scanId` for the response matcher.
- Add structured logs before publish containing device ID, scan ID, topic, and envelope version.
- Keep the 10-second configurable result timeout.
- Keep strict result matching and correlation-mismatch logging.
- Keep `online: null` for gateway-level timeout.

### 2. Firmware MQTT scan parser

Files:

- `aeroponics-firmware/src/mqtt_client.cpp`
- `aeroponics-firmware/include/mqtt_client.h` if a result/rejection API needs a declaration
- `aeroponics-firmware/src/main.cpp` for diagnostics

- Update `_enqueueGatewayScanCommand()` so a production MQTT scan requires a valid command envelope.
- Copy the validated `command_id` into `MqttInboundCommand.command_id` unchanged.
- Do not silently substitute `scan_cmd` when the MQTT message is malformed.
- Route malformed scan commands to the existing rejection path with a specific reason such as `Invalid scan command envelope: command_id/version required`.
- Preserve `executeRfScan(command.command_id)` as the only production scan entry point.
- Add a bounded log at command acceptance showing `command_id`, `version`, and command type; do not log secrets.
- If CLI `scan` remains supported, keep its explicit `cli_scan` ID separate from MQTT handling and make that distinction visible in logs.

### 3. Result publication contract

Files:

- `aeroponics-firmware/src/main.cpp`
- `aeroponics-firmware/src/mqtt_client.cpp`

- Ensure `publishScanResults()` receives the validated command ID unchanged.
- Log `scan_id`, RF duration, node count, and whether the result was queued/published.
- Retain the already-added telemetry payload capacity fix (`MQTT_TELEMETRY_PAYLOAD_SIZE` must remain large enough for four-node scan JSON).
- Treat a failed enqueue/publish as an explicit diagnostic error; do not report a successful scan without a queued result.

### 4. Backend response/API/UI consistency

Files:

- `aeroponics-backend/src/node/node.service.ts`
- `aeroponics-ui/src/lib/types.ts`
- `aeroponics-ui/src/components/dashboard/RfDiscoveryModal.tsx`

- Keep `status` values `COMPLETED`, `PARTIAL`, `FAILED`, and `TIMEOUT`.
- Keep `error_code: SCAN_RESULT_CORRELATION_TIMEOUT` for unmatched/late results.
- Preserve `online: null` and render it as `UNKNOWN` in the UI.
- Keep `MISSING_RESULT` only for a gateway payload that arrives but omits a node; do not use it for a backend-wide timeout.
- Display the active scan ID and correlation error in diagnostic UI/logs where practical so operators can compare UI, MQTT, and firmware evidence.

### 5. Docker rollout

- Rebuild both backend and UI images after source changes.
- Restart `aero-backend`, `aero_ui`, and `aero_proxy` with `docker compose up -d`.
- Confirm the running backend image contains the new envelope and correlation logging; do not rely on a host-side `npm run build` alone.
- Confirm the ESP32 firmware is rebuilt/uploaded separately; Docker changes do not update the gateway binary.

## Validation Matrix

### Firmware/native tests

Add or update tests to verify:

1. Valid scan payload `{command_id, scan_id, version: 1}` becomes an inbound command with the exact ID.
2. Missing `version` is rejected and never becomes `scan_cmd` in production MQTT handling.
3. Missing/invalid `command_id` is rejected.
4. `executeRfScan()` receives and publishes the exact accepted command ID.
5. CLI scan remains explicitly `cli_scan` and cannot affect MQTT correlation.
6. Scan result queue accepts the full four-node payload.

Run:

```bash
cd aeroponics-firmware
pio test -e native
pio run -e esp32-s3-n16r8
```

### Backend tests

In `aeroponics-backend/src/node/node.service.spec.ts`, verify:

1. Published scan command includes `command_id`, `scan_id`, and `version: 1`.
2. A result with the same ID resolves as `COMPLETED`/`PARTIAL`.
3. A result with `scan_cmd` is ignored and logs correlation mismatch.
4. A result with a wrong device ID is ignored.
5. No result within 10 seconds returns `TIMEOUT` with `online: null` nodes.
6. MQTT publish failure returns `MQTT_PUBLISH_FAILED`.
7. A late result does not mutate a completed/expired request unexpectedly.

Run:

```bash
cd aeroponics-backend
npm run build
npm test -- --runInBand src/node/node.service.spec.ts
```

Repair stale `NodeRegistry` test fixtures if they prevent the suite from compiling; include the required discovery fields rather than weakening production types.

### Docker end-to-end test

1. Start/rebuild the stack:
   ```bash
   docker compose build aero-backend aeroponics-ui
   docker compose up -d aero-backend aeroponics-ui proxy
   ```
2. Login at `http://localhost:6003` with the existing local test account.
3. Trigger scan from the dashboard.
4. Verify all three IDs match:
   - backend log: `scan_<timestamp>`;
   - MQTT command and result payload: same ID;
   - API response: same ID, `COMPLETED` or `PARTIAL`.
5. Verify Node 7 reports `online: true` and its RTT; missing nodes remain individual RF failures, not a gateway timeout.
6. Capture one malformed-command test and verify it is rejected rather than published as `scan_cmd`.

## Watchdog Scope

The repeated `TG1WDT_SYS_RST` is a separate reliability issue. Track it in the same validation run, but do not conflate it with correlation. After correlation passes, investigate watchdog resets with reset-reason logs, task-level timestamps, and MQTT/RF transaction boundaries. The correlation fix must not be blocked on a full watchdog redesign, but production sign-off should require no reset during repeated scans.
