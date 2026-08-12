# Sprint 4: HTML Dashboard — Season + Group + Node + Flow + Command Lifecycle + On-demand Measurement

## Prerequisites & Dependencies
- **Sprint 3 (NestJS Backend)** is complete.
- WebSocket `/ws` is working.
- TimescaleDB is populated with real data.

## Output
A single-file `index.html` UI serving:
- 4 Group/Treatment cards
- 12 Node/Pump cards
- Flow & Calibration views
- Command lifecycle logs
- Season & Treatment management
- Tuya on-demand measurement interface

---

## Section 1: Scope & Objectives

- **4 Group Cards:** Display treatment name, version, current phase (DAY/NIGHT), phase timer countdown, assigned node count, and status badge (ACTIVE/UNASSIGNED).
- **12 Node Cards:** Display node ID, display name, pump state (ON/OFF/FAULT), last command outcome badge (RF_ACKED/FLOW_CONFIRMED/FAULT_NO_FLOW/TIMEOUT), flow rate lpm, and last seen indicator (green/amber/red based on staleness).
- **Season Panel:** Show active season name, start date, days elapsed, and an 'End Season' button with confirmation.
- **Treatment Panel:** List treatments and versions. Provide actions to Clone, Publish Version, and Assign to Group.
- **Command Lifecycle View:** Per-node command log showing outcome progression (PENDING → RF_ACKED → FLOW_CONFIRMED or FAULT).
- **On-demand Measurement:** A 'Đo ngay' button triggering `POST /api/measurement/trigger`, displaying results (pH, EC, TDS, Temp, Salinity, ORP, Turbidity).
- **WebSocket Resiliency:** Auto-reconnect with exponential backoff and a connection banner when disconnected.

---

## Section 2: Architecture & WebSocket Schema

### Architecture
We will use the same `ServeStatic` approach from `main.ts` in the NestJS backend to serve the static frontend assets.

### WebSocket Message Schema
- `node_telemetry`
- `node_flow`
- `pump_command_update`
- `group_status`
- `staleness_alert`

---

## Section 3: Detailed Task Breakdown

### TRACK A - HTML Structure
Create a single-file approach with semantic sections:
- `header`: Connection status, season badge, device gateway status.
- `section#season-panel`: Season info + end button.
- `section#group-section`: 4 group cards in a 2x2 grid.
- `section#node-section`: 12 node cards in a 4x3 grid.
- `section#treatment-panel`: Treatment management.
- `section#measurement-panel`: Tuya on-demand + history table.
- `modals`: assign-group modal, command-log modal, measurement-result modal.

### TRACK B - CSS Design System
Maintain the dark theme from previous iterations, updating component names:
- **Keep Existing Variables:** `--bg-base`, `--c-spraying`, etc.
- **New Components:**
  - `.group-card`: Treatment badge, phase indicator, node count, phase countdown.
  - `.node-card`: Staleness indicator (green/amber/red dot), outcome badge, flow display.
  - `.outcome-badge`: Color styling per outcome (`RF_ACKED`=blue, `FLOW_CONFIRMED`=green, `FAULT_*`=red, `TIMEOUT`=orange, `PENDING`=gray).
  - `.staleness-dot`: Pulse animation if `STALE`.
  - Layout panels: `.season-panel`, `.treatment-list`, `.measurement-panel`.

### TRACK C - JavaScript Modules
- **C-1: Constants:** `GROUP_LABELS` (1-4), `NODE_LABELS` (1-12 display names), `OUTCOME_CONFIG` per outcome value, `STALE_THRESHOLD_MS=120000`.
- **C-2: WebSocketManager:** Class implementing exponential backoff.
- **C-3: Group Card Renderer:** `renderGroupCard`, `updateGroupCard` triggered by `group_status` WS event.
- **C-4: Node Card Renderer:** `renderNodeCard`, `updateNodeCard` triggered by `node_telemetry` + `pump_command_update` + `staleness_alert` events.
- **C-5: REST API Helpers:** Reusable `api.get/put/post/delete` pattern.
- **C-6: Season Panel:** `loadSeason`, `renderSeason`, `confirmEndSeason`.
- **C-7: Treatment Panel:** `loadTreatments`, `renderTreatmentList`, `openAssignModal`, `submitAssign`.
- **C-8: Command Log:** `openCommandLogModal` for a node, render command rows with outcome badges.
- **C-9: On-demand Measurement:** `triggerMeasurement` → POST, `showMeasurementResult` modal displaying all 7 values.
- **C-10: Initialization:** Load all data (season, groups x4, nodes x12, treatments, latest measurement) → render → connect WebSocket → set 5min chart refresh interval (if applicable).

### TRACK D - NestJS Integration
- Configure `ServeStatic` in NestJS to serve the `index.html` and assets.

### TRACK E - Hard QA Rules
- **S4-WS-01**: Native WebSocket ✓
- **S4-CASE-02**: camelCase from NestJS ✓
- **S4-CDN-03**: Chart.js from CDN ✓ (only if charts are used)
- **S4-API-04**: `API_BASE` dynamic (`window.location.origin`) ✓
- **S4-NULL-05**: Null-safe rendering ✓
- **S4-BANNER-06**: WebSocket reconnect banner ✓
- **S4-DESIGN-07**: Premium UI (dark theme, Inter font, animations) ✓
- **S4-NO-RELAY-08**: NO `/api/relay`, `relay_update`, relay cards, hardcoded relays 1-4 anywhere in the UI code.
- **S4-OUTCOME-09**: Node cards MUST show command outcome badge per outcome enum value, not just ON/OFF.
- **S4-STALENESS-10**: Node cards MUST show staleness dot (green < 60s, amber < 120s, red >= 120s from `last_seen`).
- **S4-SEASON-11**: Dashboard MUST show active season; if no active season, show 'Tạo mùa vụ mới' CTA.
- **S4-ON-DEMAND-12**: 'Đo ngay' button MUST call `POST /api/measurement/trigger`, NOT auto-poll on a timer.

---
*Sprint 4 Planning (HTML Dashboard — Season + Group + Node + Flow + Command Lifecycle + On-demand Measurement) — Updated 2026-08-12*
