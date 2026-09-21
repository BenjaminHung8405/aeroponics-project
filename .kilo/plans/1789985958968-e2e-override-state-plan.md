# E2E Override, State, and Liveness Plan

## Findings

- The AGU wire path is working: node 7 receives `03 06 07 F0` and returns `0x5A`; OFF receives `03 07 07 EF` and returns `0x5A`.
- The immediate OFF is not caused by an RF timeout. In the log, `PUMP_OFF` occurs ~517 ms after `PUMP_ON` without an MQTT `Applying node override ... state=OFF` line.
- `serviceScheduleTick()` runs every second, calls `stepGroupSchedule()`, then sends a legacy pump command whenever `desired_state != reported_state` (`aeroponics-firmware/src/main.cpp:625-645`). The manual override path updates registry state directly (`main.cpp:1038-1042`), but does not establish a durable override lease in the scheduler. The scheduler can therefore overwrite the manual state with its schedule/safe state.
- `NodeRegistry::evaluateStaleNodes()` also forces `desired_state=OFF` and latches a fault after the liveness threshold (`src/node_registry.cpp:331-357`). Liveness must be separated from a one-shot pump command.
- The ESP32 also rebooted with `rst:0x8 (TG1WDT_SYS_RST)`. This is a separate reliability issue that must be captured and correlated with node disconnects and command state.

## Implementation Plan

1. **Define one authoritative command state machine for legacy nodes.**
   - Add an explicit per-node manual override state: `NONE`, `ON_LEASE`, `OFF_PAUSE`, with command id, start time, expiry, desired state, and last RF result.
   - Make `onGatewayCommand()` route `ON/OFF` through this state machine instead of directly mutating `NodeRegistry` only.
   - For ON, preserve the requested `run_lease_ms` (30 seconds) and block schedule writes until lease expiry.
   - For OFF, preserve `override_duration_ms` and block schedule writes until pause expiry.
   - Return/publish `ACCEPTED` only after the command is queued; publish `RF_ACKED`/`REJECTED` after the actual AGU transaction.

2. **Prevent scheduler clobbering of manual override.**
   - Update `serviceScheduleTick()` so it never issues a schedule-derived ON/OFF while a node has an active manual override.
   - Add a dedicated expiry tick: when an ON lease expires, perform one explicit OFF transaction, clear the override only after ACK, and publish the resulting state. When an OFF pause expires, resume the schedule through the same state machine.
   - Ensure `stepGroupSchedule()` cannot silently write over an active override; return an audit/result when a schedule transition is suppressed.
   - Remove the current implicit behavior where `desired_state != reported_state` alone triggers an uncorrelated pump transaction.

3. **Separate AGU liveness from pump actuation.**
   - Add a periodic node-7 PING probe with configurable interval, timeout, retry count, and backoff; do not probe during an active pump transaction on the same UART.
   - Persist a bounded liveness record per node: last ping sent, last valid `0xA5` response, RTT, consecutive failures, last success/failure reason, boot/session marker, and health transition timestamp.
   - Treat a successful pump ACK (`0x5A`) as command acknowledgment, not as a replacement for periodic PING liveness.
   - On liveness failure, publish a node snapshot/audit event and enter safe-off only according to an explicit policy; avoid immediately issuing a second OFF command that masks the original failure.

4. **Persist and correlate state end-to-end.**
   - Extend gateway telemetry/snapshot payloads with `desired_state`, `reported_state`, `override_state`, `override_expiry_ms`, `last_command_id`, `last_command_result`, `last_ping_at`, `last_ping_ok`, `ping_rtt_ms`, `consecutive_ping_failures`, `boot_session_id`, and `reset_reason`.
   - Ensure backend maps gateway ACK/event/snapshot data to the same UUID command row and rejects/ignores non-UUID retained test ACKs without database exceptions.
   - Record every transition with source (`MANUAL_OVERRIDE`, `SCHEDULE`, `SAFE_OFF`, `LIVENESS`) and reason, so an unexpected OFF can be attributed to MQTT, scheduler, expiry, stale handling, watchdog, or hardware.

5. **Harden watchdog and reboot diagnostics.**
   - Capture `esp_reset_reason()` at boot and publish it in the first heartbeat/snapshot.
   - Audit all synchronous AGU transactions for watchdog servicing and maximum blocking duration; the current RF retries and scheduler interaction must not starve the main task.
   - Add a core-dump partition or explicitly document its absence; the current `No core dump partition found` prevents post-reset crash analysis.
   - Verify MQTT task and main task WDT registration/reset paths after each long RF transaction.

## E2E Test Matrix

1. **Baseline discovery/liveness**
   - Boot gateway; record reset reason, Wi-Fi/MQTT connection, UART config, node 7 initial state.
   - Run discovery for nodes 4–7; require node 7 PING `0xA5`, RTT within configured bound, and `ONLINE` snapshot.
   - Continue PING node 7 for at least several intervals; assert no false STALE transition.

2. **Manual ON lease**
   - Click UI override ON once with 30s lease.
   - Verify backend publish topic is `aeroponics/device/esp32_device/command/node/7/override` and exactly one command UUID is created.
   - Verify gateway receives one command, sends `03 06 07 F0`, receives `0x5A`, and emits `RF_ACKED`.
   - Verify no `PUMP_OFF` appears before the 30s lease expiry, even while scheduler ticks run.
   - Verify snapshots continuously report `desired=ON`, `reported=ON`, `override=ON`, and matching command UUID.

3. **Lease expiry**
   - At expiry, verify exactly one OFF frame `03 07 07 EF`, valid `0x5A`, transition to `NONE/IDLE`, and no duplicate OFF retries.
   - Verify backend command outcome and timestamps are complete.

4. **Manual OFF pause and resume**
   - Click OFF; verify explicit duration is honored and schedule cannot re-enable the pump early.
   - After pause expiry, verify schedule resumes only if node is still liveness-healthy.

5. **Repeated clicks/idempotency**
   - Double-click ON, ON then OFF, and OFF then ON quickly.
   - Assert command serialization per node, no overlapping AGU transactions, deterministic final state, and no stale pending command rows.

6. **Liveness loss**
   - Disconnect RF/AGU node 7 after a known-good PING.
   - Verify consecutive PING failures, health transition, safe-off policy, audit reason, and backend state.
   - Reconnect node 7; verify recovery requires a valid PING before accepting ON.

7. **Gateway reboot/WDT**
   - Trigger a controlled reboot and reproduce the long-running scenario.
   - Verify reset reason, boot session increment, state reset to safe OFF, retained MQTT commands do not replay, and liveness re-establishes before actuation.

8. **Negative contract tests**
   - Invalid/missing command envelope, stale duplicate UUID, invalid node id, expired lease, and non-UUID retained ACK.
   - Confirm no relay actuation, bounded rejection ACK, and no DB UUID parsing error.

## Acceptance Criteria

- A manual ON remains physically ON for the requested lease unless an explicit safety fault or user OFF command occurs.
- No scheduler-generated OFF may occur during an active manual override.
- Every ON/OFF frame has a matching ACK, command UUID, source, state transition, and backend outcome.
- Node 7 liveness is continuously visible and does not rely on pump ACKs.
- RF disconnects and ESP32 watchdog resets are distinguishable in logs and persisted telemetry.
- Repeated UI clicks cannot create overlapping UART transactions or ambiguous final state.
