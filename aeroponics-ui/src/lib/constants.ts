/**
 * Constants & Frozen Configuration for Aeroponics Smart Farm
 * Follows MASTER.md design system tokens and sprint_4 specifications.
 */

export interface OutcomeStyle {
  label: string;
  color: string;
  bgClass: string;
  textClass: string;
  borderClass: string;
  glowClass?: string;
  isFault?: boolean;
}

/**
 * Pre-defined outcome configurations for command lifecycle.
 * Frozen with Object.freeze to guarantee runtime immutability (Hard Rule S4-OUTCOME-08).
 */
export const OUTCOME_CONFIG: Readonly<Record<string, OutcomeStyle>> = Object.freeze({
  FLOW_CONFIRMED: Object.freeze({
    label: 'Xác nhận dòng chảy',
    color: 'primary',
    bgClass: 'bg-primary/15',
    textClass: 'text-primary',
    borderClass: 'border-primary/40',
    glowClass: 'relay-glow-active',
    isFault: false,
  }),
  RF_ACKED: Object.freeze({
    label: 'Đã nhận lệnh (RF)',
    color: 'accent-indigo',
    bgClass: 'bg-accent-indigo/15',
    textClass: 'text-accent-indigo',
    borderClass: 'border-accent-indigo/40',
    isFault: false,
  }),
  PENDING: Object.freeze({
    label: 'Đang gửi lệnh',
    color: 'text-subtle',
    bgClass: 'bg-surface/50',
    textClass: 'text-text-subtle',
    borderClass: 'border-border/30',
    isFault: false,
  }),
  TIMEOUT: Object.freeze({
    label: 'Hết thời gian phản hồi',
    color: 'accent-amber',
    bgClass: 'bg-accent-amber/15',
    textClass: 'text-accent-amber',
    borderClass: 'border-accent-amber/40',
    isFault: false,
  }),
});

const DEFAULT_FAULT_STYLE: OutcomeStyle = Object.freeze({
  label: 'Lỗi phần cứng / Mất kết nối',
  color: 'danger',
  bgClass: 'bg-danger/15',
  textClass: 'text-danger',
  borderClass: 'border-danger/40',
  isFault: true,
});

const DEFAULT_NEUTRAL_STYLE: OutcomeStyle = Object.freeze({
  label: 'Chờ lệnh',
  color: 'text-subtle',
  bgClass: 'bg-surface/50',
  textClass: 'text-text-subtle',
  borderClass: 'border-border/30',
  isFault: false,
});

/**
 * Helper to retrieve outcome style safely with FAULT_* prefix handling.
 */
export function getOutcomeConfig(outcome?: string | null): OutcomeStyle {
  if (!outcome) {
    return DEFAULT_NEUTRAL_STYLE;
  }

  // Handle all FAULT_* variations automatically (Hard Rule S4-OUTCOME-08)
  if (outcome.startsWith('FAULT_')) {
    let faultLabel = 'Lỗi vận hành';
    switch (outcome) {
      case 'FAULT_NO_ACK':
        faultLabel = 'Lỗi không nhận ACK';
        break;
      case 'FAULT_OVER_RANGE':
        faultLabel = 'Lỗi lưu lượng vượt ngưỡng';
        break;
      case 'FAULT_HARDWARE':
        faultLabel = 'Lỗi phần cứng cảm biến';
        break;
      case 'FAULT_BACKEND_DISCONNECT':
        faultLabel = 'Mất kết nối máy chủ';
        break;
      default:
        faultLabel = `Lỗi (${outcome.replace('FAULT_', '')})`;
        break;
    }
    return {
      ...DEFAULT_FAULT_STYLE,
      label: faultLabel,
    };
  }

  const matched = OUTCOME_CONFIG[outcome];
  if (matched) {
    return matched;
  }

  return {
    ...DEFAULT_NEUTRAL_STYLE,
    label: outcome,
  };
}

// ==========================================
// Telemetry Staleness Thresholds (Hard Rule S4-STALENESS-09)
// ==========================================
export const STALE_THRESHOLD_MS = 120_000; // ≥ 120s -> Red danger + pulse
export const STALE_AMBER_MS = 60_000;      // 60s - 120s -> Amber warning

export type StalenessLevel = 'fresh' | 'warning' | 'stale';

/**
 * Calculates the staleness level based on lastSeenAt or staleForMs.
 * Boundary tests:
 *  - 0s .. 59s -> 'fresh' (< 60s)
 *  - 60s .. 119s -> 'warning' (60s .. 120s)
 *  - >= 120s -> 'stale' (>= 120s)
 */
export function getStalenessLevel(
  lastSeenAt?: string | null,
  isStale?: boolean,
  staleForMs?: number,
): { level: StalenessLevel; elapsedSeconds: number; label: string } {
  if (isStale) {
    const elapsed = staleForMs ? Math.floor(staleForMs / 1000) : 120;
    return {
      level: 'stale',
      elapsedSeconds: elapsed,
      label: `Mất tín hiệu (${elapsed}s)`,
    };
  }

  let elapsedMs = staleForMs || 0;
  if (!elapsedMs && lastSeenAt) {
    const last = new Date(lastSeenAt).getTime();
    if (!Number.isNaN(last)) {
      elapsedMs = Math.max(0, Date.now() - last);
    }
  }

  const elapsedSeconds = Math.floor(elapsedMs / 1000);

  if (elapsedMs >= STALE_THRESHOLD_MS) {
    return {
      level: 'stale',
      elapsedSeconds,
      label: `Mất tín hiệu (${elapsedSeconds}s)`,
    };
  }

  if (elapsedMs >= STALE_AMBER_MS) {
    return {
      level: 'warning',
      elapsedSeconds,
      label: `Tín hiệu trễ (${elapsedSeconds}s)`,
    };
  }

  return {
    level: 'fresh',
    elapsedSeconds,
    label: lastSeenAt ? `Trực tuyến (${elapsedSeconds}s)` : 'Trực tuyến',
  };
}

// ==========================================
// WebSocket Configuration
// ==========================================
export const WS_RECONNECT_MAX_DELAY_MS = 30_000; // Max backoff ceiling 30s
export const WS_RECONNECT_BASE_DELAY_MS = 1_000; // Initial backoff 1s
export const WS_RECONNECT_FACTOR = 1.5;

export const WS_EVENTS = Object.freeze({
  CONNECTED: 'connected',
  NODE_TELEMETRY: 'node_telemetry',
  NODE_FLOW: 'node_flow',
  PUMP_COMMAND_UPDATE: 'pump_command_update',
  GROUP_STATUS: 'group_status',
  STALENESS_ALERT: 'staleness_alert',
  DEVICE_STATUS: 'device_status',
} as const);

// ==========================================
// TanStack Query Keys & Cache Configuration
// ==========================================
export const DEFAULT_STALE_TIME_MS = 30_000; // 30s cache TTL

export const QUERY_KEYS = Object.freeze({
  SEASON_ACTIVE: ['season', 'active'] as const,
  SEASON_LIST: ['season', 'list'] as const,
  GROUPS: ['groups'] as const,
  GROUP: (id: number) => ['group', id] as const,
  NODES: ['nodes'] as const,
  NODE: (id: number) => ['node', id] as const,
  TREATMENTS: ['treatments'] as const,
  TREATMENT: (id: number) => ['treatment', id] as const,
  MEASUREMENT_LATEST: ['measurement', 'latest'] as const,
  MEASUREMENT_HISTORY: (limit?: number, offset?: number, type?: string) =>
    ['measurement', 'history', limit ?? 50, offset ?? 0, type ?? 'all'] as const,
  DEVICE_STATUS: ['device', 'status'] as const,
} as const);
