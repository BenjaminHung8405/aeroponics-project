# Sprint 3: NestJS Backend — Season + Group + Node + Flow + RF Command + On-demand Measurement

## Prerequisites & Dependencies
- **Sprint 2 (ESP32 RF Gateway & Node Firmware)** is PASS.
- RF gateway is running, and node telemetry is coming in via MQTT.

## Output
A complete NestJS backend that features:
1. **MQTT Integration:** Subscribes to node telemetry, flow, and fault data, writing into TimescaleDB.
2. **Tuya Integration:** Supports on-demand Tuya device measurement.
3. **REST API:** Full API for managing seasons, treatments, groups, nodes, and commands.
4. **Real-time Push:** WebSocket implementation for pushing real-time updates to the dashboard.

## Strategy
We will reuse the established `mushroom-cp` boilerplate (database, config, MQTT, auth), updating the module names and core logic to fit the new aeroponics domain.

---

## Section 1: Scope & Objectives

The backend completely transitions away from direct GPIO relays and 10s sensor polling. We introduce new production modules:
- **SeasonModule:** Manage agricultural seasons (start/end, up to 120 days).
- **TreatmentModule:** Manage treatments and versioning.
- **GroupModule:** Dynamic management of 4 timer groups.
- **NodeModule:** Manage 12 RF nodes, health, and telemetry.
- **FlowModule:** Handle flow telemetry and calibration.
- **PumpCommandModule:** Manage command lifecycle, outcomes, and RF sequencing.
- **TuyaBridgeModule:** Handle on-demand sensor measurements.
- **EventsModule:** Native WebSocket broadcasting.

*Affected Files (New Modules Only):*
- `src/season/*`
- `src/treatment/*`
- `src/group/*`
- `src/node/*`
- `src/flow/*`
- `src/pump-command/*`
- `src/tuya-bridge/*`
- *No old `relay` or `sensor` modules.*

---

## Section 2: Architecture & Data Flows

### Module Dependency Graph
`AppModule` depends on:
- `AppConfigModule`
- `DatabaseModule`
- `MqttModule`
- `EventsModule`
- `SeasonModule`
- `TreatmentModule`
- `GroupModule`
- `NodeModule`
- `FlowModule`
- `PumpCommandModule`
- `TuyaBridgeModule`

### Data Flows

**1. Node Telemetry Flow:**
`ESP32 RF node telemetry` → `MQTT` → `MqttService` → `NodeService.handleTelemetry()` → `TimescaleDB` + `EventsGateway.emit('node_telemetry')`

**2. Command Lifecycle Flow:**
`Backend (PumpCommandService.sendCommand())` → `MQTT` → `ESP32 Gateway` → `RF` → `Node` → `RF ACK` → `MQTT` → `PumpCommandService.handleAck()` → `flow evaluation` → `FLOW_CONFIRMED` or `FAULT_*`

**3. Tuya On-Demand Flow:**
`REST POST /api/measurement/trigger` → `TuyaBridgeService.measureOnDemand()` → parse DPs → save to TimescaleDB → return data to client.

---

## Section 3: Detailed Task Breakdown

### TRACK A - Reuse from mushroom-cp
Set up the base project structure using the proven boilerplate for:
- `/database`
- `/config`
- `/mqtt`
- `/auth`

### TRACK B - TypeORM Entities
Write entity skeletons for the production domain:
- **`season.entity.ts`**: `id`, `name`, `started_at`, `ended_at` (nullable), `status` ('ACTIVE'|'ENDED'), `notes`
- **`treatment.entity.ts`**: `id`, `name`, `created_at`, `is_archived`
- **`treatment_version.entity.ts`**: `id`, `treatment_id`, `version_num`, `spray_day_s`, `cooldown_day_s`, `spray_night_s`, `cooldown_night_s`, `created_at`, `published_at` (nullable)
- **`group_assignment.entity.ts`**: `id`, `group_id` (1-4), `treatment_version_id` (FK), `node_ids` (int[], or separate join table), `season_id` (FK), `assigned_at`, `active`
- **`node_registry.entity.ts`**: `node_id` (1-12 PK), `display_name`, `group_id` (nullable), `calibration_pulses_per_litre` (numeric), `last_seen_at`, `health_status` ('OK'|'STALE'|'FAULT')
- **`pump_command.entity.ts` (hypertable)**: `time`, `command_id` (uuid), `node_id`, `group_id`, `action` ('ON'|'OFF'), `rf_seq`, `outcome` (enum), `acked_at`, `flow_confirmed_at`, `fault_reason` (nullable)
- **`flow_event.entity.ts` (hypertable)**: `time`, `node_id`, `litres_total` (numeric), `pulse_count`, `flow_rate_lpm` (numeric), `is_fault`
- **`measurement_reading.entity.ts` (hypertable)**: `time`, `trigger_type` ('ON_DEMAND'|'END_OF_SEASON'), `ph_value`, `ec_value`, `tds_value`, `temperature`, `salinity`, `orp_value`, `turbidity`, `triggered_by_user_id`

### TRACK C - Season Module
- **`SeasonService`**: CRUD operations (`create`, `getActive`, `endSeason`, `list`).
- **`SeasonController`**: REST endpoints for season management.

### TRACK D - Treatment Module
- **`TreatmentService`**: Functions for `create`, `addVersion`, `publishVersion`, `clone`, `archive`.
- **`TreatmentController`**: REST endpoints for treatment management.

### TRACK E - Group & Node Module
- **`GroupService`**: `assignTreatmentVersion`, `unassign`, `getGroupStatus` (with node list, treatment, current phase).
- **`NodeService`**: `register`, `updateHealth`, `handleTelemetry` (upsert last_seen, emit staleness warning), `getNodeStatus`.
- **Controllers**: `NodeController` and `GroupController` REST APIs.

### TRACK F - PumpCommand Module
- **`PumpCommandService`**:
  - `sendCommand(nodeId, groupId, action, treatmentVersionId)`: Publish MQTT with `command_id` + `rf_seq` + `deadman_lease`. Save `pump_command` row with outcome 'PENDING'.
  - `handleRfAck(commandId, acked)`: Update outcome to `RF_ACKED` or `FAULT_NO_ACK`.
  - `handleFlowConfirmed(commandId, flowData)`: Update to `FLOW_CONFIRMED`, save `flow_event`.
  - `handleFault(commandId, reason)`: Update to `FAULT_*`.
  - **Deadman Timer**: If backend disconnects, cancel active leases.
- **`PumpCommandController`**: POST `/api/group/:groupId/command`, GET `/api/node/:nodeId/commands?limit=50`

### TRACK G - Flow Module
- **`FlowService`**: `getHistory(nodeId, hours)`, `getCalibration(nodeId)`, `updateCalibration(nodeId, pulsesPerLitre)`.
- **`FlowController`**: REST endpoints.

### TRACK H - Tuya Bridge Module
- **`TuyaBridgeService`**: *NO polling loop*. Expose `measureOnDemand()` → `device.get()` → `parseDps()` → save `measurement_reading` → return DTO.
- **`TuyaBridgeController`**: POST `/api/measurement/trigger` (auth required), GET `/api/measurement/latest`, GET `/api/measurement/history?limit=20`.

### TRACK I - MQTT Topics
Adapt from sprint_2 RF domain.
- **Subscribe**: `aeroponics/gateway/+/heartbeat`, `aeroponics/node/+/telemetry`, `aeroponics/node/+/flow`, `aeroponics/node/+/ack`, `aeroponics/node/+/fault`
- **Publish**: `aeroponics/gateway/+/command/node/+/pump`, `aeroponics/gateway/+/command/group/+/config`

### TRACK J - WebSocket Events
- `node_telemetry`: `{ nodeId, health, lastSeenAt, ... }`
- `node_flow`: `{ nodeId, litresTotal, flowRateLpm, isFault }`
- `pump_command_update`: `{ commandId, nodeId, outcome, ackedAt, flowConfirmedAt }`
- `group_status`: `{ groupId, treatmentVersionId, phase, nextTransitionAt }`
- `staleness_alert`: `{ nodeId, lastSeenAt, staleForMs }`

### TRACK K - REST API Table

| Method | Path | Description |
|---|---|---|
| GET | `/health` | Health check |
| GET | `/` | Serve index.html |
| POST | `/api/season` | Create season |
| GET | `/api/season/active` | Get active season |
| PUT | `/api/season/:id/end` | End season |
| GET | `/api/season` | List seasons |
| POST | `/api/treatment` | Create treatment |
| POST | `/api/treatment/:id/version` | Add version |
| PUT | `/api/treatment/:id/version/:versionId/publish` | Publish version |
| POST | `/api/treatment/:id/clone` | Clone treatment |
| GET | `/api/treatment` | List treatments |
| GET | `/api/group` | All 4 groups status |
| PUT | `/api/group/:id/assign` | Assign treatment version + nodes |
| DELETE | `/api/group/:id/assign` | Unassign group |
| GET | `/api/node` | All 12 nodes status |
| GET | `/api/node/:id` | Node detail + last telemetry |
| PUT | `/api/node/:id/calibration` | Update calibration |
| POST | `/api/group/:id/command` | Send pump command to group |
| GET | `/api/node/:id/commands?limit=50` | Command history |
| GET | `/api/node/:id/flow?hours=24` | Flow history |
| POST | `/api/measurement/trigger` | Tuya on-demand measure |
| GET | `/api/measurement/latest` | Latest measurement |
| GET | `/api/measurement/history?limit=20` | Measurement history |
| GET | `/api/device/:id/status` | Gateway online/offline status |

### TRACK L - Hard QA Rules
- **S3-REUSE-01**: Reuse mushroom-cp database/config/mqtt patterns ✓
- **S3-NO-INFLUX-02**: No InfluxDB ✓
- **S3-DB-03**: `synchronize: false` ✓
- **S3-TUYA-ON-DEMAND-04**: TuyaBridgeService MUST NOT have setInterval polling loop; must expose `measureOnDemand()` only
- **S3-MQTT-05**: `onMessage` catches all exceptions ✓
- **S3-WS-NATIVE-06**: Native WebSocket, no Socket.IO ✓
- **S3-DTO-07**: class-validator for all request bodies ✓
- **S3-DEADMAN-08**: PumpCommandService MUST implement deadman/lease cancel on module destroy
- **S3-ANTIREPLAY-09**: MQTT command handler MUST reject duplicate `rf_seq` within 60s window
- **S3-STALENESS-10**: NodeService MUST emit `staleness_alert` if `last_seen_at` > `STALE_THRESHOLD_MS` (configurable, default 120s)
- **S3-NO-RELAY-11**: NO RelayModule, relay_events, relay_profiles, /api/relay/* anywhere in production code

---
*Sprint 3 Planning (NestJS Backend — Season + Group + Node + Flow + RF Command + On-demand Measurement) — Updated 2026-08-12*
