/**
 * Domain Types for Aeroponics Smart Farm
 * Aligns 100% with NestJS backend entities, DTOs, and WebSocket contracts.
 */

// ==========================================
// 1. Season Types
// ==========================================

export type SeasonStatus = 'ACTIVE' | 'ENDED';

export interface Season {
  id: number;
  name: string;
  start_date?: string;
  end_date?: string | null;
  started_at?: string;
  ended_at?: string | null;
  status: SeasonStatus;
  created_at: string;
  updated_at: string;
  target_ec?: number | null;
  target_ph?: number | null;
  notes?: string | null;
}

export interface CreateSeasonDto {
  name: string;
  start_date?: string;
  target_ec?: number;
  target_ph?: number;
  notes?: string;
}

export interface EndSeasonDto {
  end_date?: string;
  notes?: string;
}

export interface SeasonListResponse {
  items: Season[];
  total: number;
}

// ==========================================
// 2. Timer Group Types
// ==========================================

export type TimerGroupStatus = 'ACTIVE' | 'UNASSIGNED';
export type CyclePhase = 'DAY' | 'NIGHT';

export interface GroupNodeSummary {
  node_id: number;
  display_name: string;
  health_status: NodeHealthStatus;
  schedule_state: string;
  last_seen_at: string | null;
  is_stale: boolean;
}

export interface GroupTreatmentSummary {
  treatment_id: number;
  treatment_name: string;
  treatment_version_id: number;
  version_num: number;
  spray_day_s: number;
  cooldown_day_s: number;
  spray_night_s: number;
  cooldown_night_s: number;
}

export interface GroupStatusResponse {
  group_id: number;
  name: string;
  status: TimerGroupStatus;
  current_phase: CyclePhase | null;
  next_transition_at: string | null;
  treatment: GroupTreatmentSummary | null;
  nodes: GroupNodeSummary[];
}

export interface AssignGroupDto {
  treatment_version_id: number;
  node_ids: number[];
}

// ==========================================
// 3. Actuator Node Types
// ==========================================

export type NodeHealthStatus = 'OK' | 'STALE' | 'FAULT';
export type CalibrationStatus = 'CALIBRATED' | 'UNCALIBRATED';
export type ScheduleState = 'SPRAYING' | 'COOLDOWN' | 'IDLE' | 'UNKNOWN';
export type OverrideState = 'NONE' | 'OVERRIDE_ON' | 'OVERRIDE_OFF';

export interface NodeActiveCalibration {
  id: number;
  version_num: number;
  pulses_per_litre: string;
  reference_volume_ml: number;
  calibrated_at: string;
  calibrated_by: string | null;
}

export interface NodeStatusResponse {
  node_id: number;
  display_name: string;
  cached_group_id: number | null;
  sensor_serial: string | null;
  calibration_status: CalibrationStatus;
  schedule_state: string;
  override_state: string;
  last_boot_session_id: number | null;
  last_seen_at: string | null;
  health_status: NodeHealthStatus;
  is_stale: boolean;
  stale_for_ms: number;
  rf_protocol?: string | null;
  last_scan_id?: string | null;
  last_rf_rtt_ms?: number | null;
  last_discovered_at?: string | null;
  discovery_status?: string | null;
  active_calibration: NodeActiveCalibration | null;
}

export interface UpdateNodeCalibrationDto {
  pulses_per_litre: number;
  reference_volume_ml: number;
  sensor_serial?: string;
  calibrated_by?: string;
}

export interface SendPumpCommandDto {
  node_id?: number;
  group_id?: number;
  action: 'ON' | 'OFF';
  run_lease_ms?: number;
  override_duration_ms?: number;
  source?: string;
}

export interface DiscoveredRfNode {
  node_id: number;
  online: boolean;
  rtt_ms: number | null;
  protocol: string;
  is_assigned: boolean;
  current_slot?: number;
  boot_session_id?: number;
  failure_reason?: string;
}

export interface RfScanResponse {
  scan_id: string;
  duration_ms: number;
  status?: 'COMPLETED' | 'FAILED' | 'TIMEOUT';
  error?: string;
  nodes: DiscoveredRfNode[];
}

export interface ClaimNodeDto {
  fromNodeId: number;
  toNodeId: number;
}

// ==========================================
// 4. Treatment & Version Types
// ==========================================

export type TreatmentStatus = 'DRAFT' | 'PUBLISHED' | 'ARCHIVED';

export interface TreatmentVersion {
  id: number;
  treatment_id: number;
  version_num: number;
  status: TreatmentStatus;
  spray_day_s: number;
  cooldown_day_s: number;
  spray_night_s: number;
  cooldown_night_s: number;
  created_at: string;
  published_at: string | null;
}

export interface Treatment {
  id: number;
  name: string;
  description: string | null;
  is_archived: boolean;
  created_at: string;
  updated_at: string;
  versions?: TreatmentVersion[];
  active_version?: TreatmentVersion | null;
}

export interface CreateTreatmentDto {
  name: string;
  spray_day_s?: number;
  cooldown_day_s?: number;
  spray_night_s?: number;
  cooldown_night_s?: number;
  created_by?: string;
}

export interface CreateTreatmentVersionDto {
  spray_day_s: number;
  cooldown_day_s: number;
  spray_night_s: number;
  cooldown_night_s: number;
}

export interface TreatmentListResponse {
  items: Treatment[];
  total: number;
}

// ==========================================
// 5. Water Quality Measurement (Tuya PH-W218)
// ==========================================

export type MeasurementTriggerType = 'ON_DEMAND' | 'END_OF_SEASON';

export interface TriggerMeasurementDto {
  trigger_type?: MeasurementTriggerType;
}

export interface MeasurementReadingResponse {
  id: number | string;
  sensor_id: string;
  time: string;
  ph: number | null;
  ec: number | null;
  tds: number | null;
  temperature_c: number | null;
  salinity: number | null;
  orp: number | null;
  turbidity: number | null;
  battery_pct: number | null;
  trigger_type: MeasurementTriggerType;
  operator: string | null;
  season_id: number | null;
}

export interface MeasurementHistoryResponse {
  items: MeasurementReadingResponse[];
  total: number;
  limit: number;
  offset: number;
}

export interface MeasurementHistoryQuery {
  limit?: number;
  offset?: number;
  trigger_type?: MeasurementTriggerType;
}

export interface ToggleTuyaBridgeDto {
  enabled: boolean;
  reason?: string;
}

export interface TuyaBridgeStatusResponse {
  enabled: boolean;
  static_enabled: boolean;
  runtime_enabled: boolean;
  sensor_id: string;
  device_ip?: string;
  masked_device_id?: string;
  cooldown_remaining_s: number;
  is_measuring: boolean;
  last_measurement_time: string | null;
  reason: string | null;
  updated_at: string | null;
  updated_by: string | null;
}

// ==========================================
// 6. Pump Command & Outcome Types
// ==========================================

export type PumpCommandOutcome =
  | 'PENDING'
  | 'RF_ACKED'
  | 'FLOW_CONFIRMED'
  | 'TIMEOUT'
  | 'FAULT_NO_ACK'
  | 'FAULT_OVER_RANGE'
  | 'FAULT_HARDWARE'
  | 'FAULT_BACKEND_DISCONNECT'
  | string;

export interface SendPumpCommandDto {
  action: 'ON' | 'OFF';
  run_lease_ms?: number;
  node_id?: number;
}

// ==========================================
// 7. WebSocket Event Payloads
// ==========================================

export interface WebSocketBroadcastMessage<T = any> {
  event: string;
  data: T;
  timestamp: string;
}

export interface NodeTelemetryWsData {
  nodeId: number;
  health: NodeHealthStatus;
  previousHealth?: NodeHealthStatus;
  lastSeenAt: string;
  scheduleState?: string;
  overrideState?: string;
  sensorSerial?: string;
  bootSessionId?: number;
  reason?: string;
  resetBy?: string;
}

export interface NodeFlowWsData {
  nodeId: number;
  litresTotal: number;
  flowRateLpm: number;
  isFault: boolean;
  faultCode: string | null;
  flowConfirmed: boolean;
  sampleWindowMs: number;
  time: string;
}

export interface PumpCommandUpdateWsData {
  commandId: string;
  nodeId: number;
  outcome: PumpCommandOutcome;
  action?: 'ON' | 'OFF';
  runLeaseMs?: number;
  sentAt?: string;
  ackedAt?: string;
  latencyMs?: number;
  flowRateLpm?: number;
  flowConfirmedAt?: string | null;
  faultReason?: string;
  faultAt?: string;
}

export interface GroupStatusWsData {
  groupId: number;
  treatmentVersionId: number | null;
  phase: CyclePhase | 'UNASSIGNED';
  nextTransitionAt: string | null;
  nodeIds?: number[];
  unassignedAt?: string;
}

export interface StalenessAlertWsData {
  nodeId: number;
  lastSeenAt: string | null;
  staleForMs: number;
}

// ==========================================
// 8. Device / Gateway Status Types
// ==========================================

export type DeviceConnectionStatus = 'online' | 'offline' | 'unknown';

export interface DeviceStatusResponse {
  device_id: string;
  status: 'online' | 'offline';
  uptime_s: number;
  rssi_dbm: number | null;
  free_heap_b: number | null;
  ntp_synced: boolean;
  rtc_valid: boolean;
  last_seen_at: string | null;
}

export interface DeviceStatusWsData {
  deviceId: string;
  status: 'online' | 'offline';
  uptime_s?: number;
  rssi_dbm?: number | null;
  free_heap_b?: number | null;
  ntpSynced?: boolean;
  rtcValid?: boolean;
  lastSeenAt?: string;
  reason?: string;
}
