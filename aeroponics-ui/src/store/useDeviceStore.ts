/**
 * Zustand Store for ESP32 Gateway Device Status (Multi-Device Supported)
 * Follows:
 *  - Type-safe immutable updates.
 *  - Real-time updates from WebSocket 'device_status' keyed by deviceId.
 *  - Multi-device dictionary (Record<string, SingleDeviceState>) with selectedDeviceId.
 *  - 100% backward-compatible top-level properties reflecting active selected device.
 */

import { create } from 'zustand';
import type {
  DeviceConnectionStatus,
  DeviceStatusResponse,
  DeviceStatusWsData,
  ScheduleSyncState,
} from '../lib/types';

export interface SingleDeviceState {
  deviceId: string;
  displayName: string | null;
  status: DeviceConnectionStatus;
  uptime_s: number;
  rssi_dbm: number | null;
  free_heap_b: number | null;
  ntpSynced: boolean;
  rtcValid: boolean;
  timeSource: string | null;
  lastSyncUnixTimeUtc: number | null;
  lastSeenAt: string | null;
  reason: string | null;
  enabled: boolean;
  syncState: ScheduleSyncState;
  reportedScheduleState: Record<string, unknown> | null;
  scheduleSyncUpdatedAt: string | null;
  scheduleSyncDetails: Record<string, unknown> | null;
}

export interface DeviceStoreState {
  // Top-level properties (reflecting active/selected device for backward compatibility)
  deviceId: string;
  status: DeviceConnectionStatus;
  uptime_s: number;
  rssi_dbm: number | null;
  free_heap_b: number | null;
  ntpSynced: boolean;
  rtcValid: boolean;
  timeSource: string | null;
  lastSyncUnixTimeUtc: number | null;
  lastSeenAt: string | null;
  reason: string | null;
  syncState: ScheduleSyncState;
  reportedScheduleState: Record<string, unknown> | null;
  scheduleSyncUpdatedAt: string | null;
  scheduleSyncDetails: Record<string, unknown> | null;

  // Multi-device state
  selectedDeviceId: string;
  devices: Record<string, SingleDeviceState>;

  setSelectedDeviceId: (deviceId: string) => void;
  getDevice: (deviceId: string) => SingleDeviceState | undefined;
  setDeviceStatus: (
    data: Partial<DeviceStatusResponse> | DeviceStatusWsData,
  ) => void;
  setDevicesList: (
    list: (Partial<DeviceStatusResponse> | DeviceStatusWsData)[],
  ) => void;
  markOffline: (deviceIdOrReason?: string, reason?: string) => void;
  reset: () => void;
}

const initialSingleDevice: SingleDeviceState = {
  deviceId: '',
  displayName: null,
  status: 'unknown' as DeviceConnectionStatus,
  uptime_s: 0,
  rssi_dbm: null,
  free_heap_b: null,
  ntpSynced: false,
  rtcValid: false,
  timeSource: null,
  lastSyncUnixTimeUtc: null,
  lastSeenAt: null,
  reason: null,
  enabled: true,
  syncState: 'UNCONFIRMED',
  reportedScheduleState: null,
  scheduleSyncUpdatedAt: null,
  scheduleSyncDetails: null,
};

const initialState = {
  ...initialSingleDevice,
  selectedDeviceId: '',
  devices: {} as Record<string, SingleDeviceState>,
};

function normalizeIncomingData(
  data: Partial<DeviceStatusResponse> | DeviceStatusWsData,
  currentState?: SingleDeviceState,
): SingleDeviceState {
  const fallback = currentState || initialSingleDevice;

  const deviceId =
    ('deviceId' in data && data.deviceId) ||
    ('device_id' in data && data.device_id) ||
    fallback.deviceId;

  const displayName =
    ('displayName' in data && data.displayName !== undefined)
      ? data.displayName
      : ('display_name' in data && data.display_name !== undefined)
        ? data.display_name
        : fallback.displayName;

  const status = (data.status as DeviceConnectionStatus) || fallback.status;

  const uptime_s =
    data.uptime_s !== undefined ? Number(data.uptime_s) : fallback.uptime_s;

  const rssi_dbm =
    data.rssi_dbm !== undefined ? data.rssi_dbm : fallback.rssi_dbm;

  const free_heap_b =
    data.free_heap_b !== undefined ? data.free_heap_b : fallback.free_heap_b;

  const ntpSynced =
    'ntpSynced' in data && data.ntpSynced !== undefined
      ? Boolean(data.ntpSynced)
      : 'ntp_synced' in data && data.ntp_synced !== undefined
        ? Boolean(data.ntp_synced)
        : fallback.ntpSynced;

  const rtcValid =
    'rtcValid' in data && data.rtcValid !== undefined
      ? Boolean(data.rtcValid)
      : 'rtc_valid' in data && data.rtc_valid !== undefined
        ? Boolean(data.rtc_valid)
        : fallback.rtcValid;

  const timeSource =
    'timeSource' in data && data.timeSource !== undefined
      ? data.timeSource
      : 'time_source' in data && data.time_source !== undefined
        ? data.time_source
        : fallback.timeSource;

  const lastSyncUnixTimeUtc =
    'lastSyncUnixTimeUtc' in data && data.lastSyncUnixTimeUtc !== undefined
      ? (data.lastSyncUnixTimeUtc !== null ? Number(data.lastSyncUnixTimeUtc) : null)
      : 'last_sync_unix_time_utc' in data && data.last_sync_unix_time_utc !== undefined
        ? (data.last_sync_unix_time_utc !== null ? Number(data.last_sync_unix_time_utc) : null)
        : fallback.lastSyncUnixTimeUtc;

  const lastSeenAt =
    'lastSeenAt' in data && data.lastSeenAt !== undefined
      ? data.lastSeenAt
      : 'last_seen_at' in data && data.last_seen_at !== undefined
        ? data.last_seen_at
        : fallback.lastSeenAt;

  const reason = 'reason' in data ? (data.reason ?? null) : fallback.reason;

  const enabled =
    'enabled' in data && data.enabled !== undefined
      ? Boolean(data.enabled)
      : fallback.enabled;

  const syncState = ('syncState' in data && data.syncState !== undefined
    ? data.syncState
    : fallback.syncState) as ScheduleSyncState;
  const reportedScheduleState = 'reportedScheduleState' in data && data.reportedScheduleState !== undefined
    ? data.reportedScheduleState
    : fallback.reportedScheduleState;
  const scheduleSyncUpdatedAt = 'scheduleSyncUpdatedAt' in data && data.scheduleSyncUpdatedAt !== undefined
    ? data.scheduleSyncUpdatedAt
    : fallback.scheduleSyncUpdatedAt;
  const scheduleSyncDetails = 'scheduleSyncDetails' in data && data.scheduleSyncDetails !== undefined
    ? data.scheduleSyncDetails
    : fallback.scheduleSyncDetails;

  return {
    deviceId,
    displayName,
    status,
    uptime_s,
    rssi_dbm,
    free_heap_b,
    ntpSynced,
    rtcValid,
    timeSource,
    lastSyncUnixTimeUtc,
    lastSeenAt,
    reason,
    enabled,
    syncState,
    reportedScheduleState,
    scheduleSyncUpdatedAt,
    scheduleSyncDetails,
  };
}

export const useDeviceStore = create<DeviceStoreState>((set, get) => ({
  ...initialState,

  setSelectedDeviceId: (deviceId: string) =>
    set((state) => {
      const target = state.devices[deviceId];
      if (!target) {
        return {
          selectedDeviceId: deviceId,
          deviceId,
        };
      }
      return {
        selectedDeviceId: deviceId,
        deviceId: target.deviceId,
        status: target.status,
        uptime_s: target.uptime_s,
        rssi_dbm: target.rssi_dbm,
        free_heap_b: target.free_heap_b,
        ntpSynced: target.ntpSynced,
        rtcValid: target.rtcValid,
        timeSource: target.timeSource,
        lastSyncUnixTimeUtc: target.lastSyncUnixTimeUtc,
        lastSeenAt: target.lastSeenAt,
        reason: target.reason,
        syncState: target.syncState,
        reportedScheduleState: target.reportedScheduleState,
        scheduleSyncUpdatedAt: target.scheduleSyncUpdatedAt,
        scheduleSyncDetails: target.scheduleSyncDetails,
      };
    }),

  getDevice: (deviceId: string) => {
    return get().devices[deviceId];
  },

  setDeviceStatus: (data) =>
    set((state) => {
      const deviceId =
        ('deviceId' in data && data.deviceId) ||
        ('device_id' in data && data.device_id) ||
        state.selectedDeviceId ||
        state.deviceId;

      const currentDev = state.devices[deviceId];
      const updatedDev = normalizeIncomingData(data, currentDev);

      const nextDevices = {
        ...state.devices,
        [deviceId]: updatedDev,
      };

      // Determine if we should update top-level properties
      const isSelected =
        deviceId === state.selectedDeviceId ||
        (!state.selectedDeviceId && Object.keys(state.devices).length === 0);

      if (isSelected) {
        return {
          devices: nextDevices,
          deviceId: updatedDev.deviceId,
          selectedDeviceId: updatedDev.deviceId,
          status: updatedDev.status,
          uptime_s: updatedDev.uptime_s,
          rssi_dbm: updatedDev.rssi_dbm,
          free_heap_b: updatedDev.free_heap_b,
          ntpSynced: updatedDev.ntpSynced,
          rtcValid: updatedDev.rtcValid,
          timeSource: updatedDev.timeSource,
          lastSyncUnixTimeUtc: updatedDev.lastSyncUnixTimeUtc,
          lastSeenAt: updatedDev.lastSeenAt,
          reason: updatedDev.reason,
          syncState: updatedDev.syncState,
          reportedScheduleState: updatedDev.reportedScheduleState,
          scheduleSyncUpdatedAt: updatedDev.scheduleSyncUpdatedAt,
          scheduleSyncDetails: updatedDev.scheduleSyncDetails,
        };
      }

      return {
        devices: nextDevices,
      };
    }),

  setDevicesList: (list) =>
    set((state) => {
      // The REST response is authoritative; never synthesize or retain a gateway locally.
      const nextDevices: Record<string, SingleDeviceState> = {};

      for (const item of list) {
        const id =
          ('deviceId' in item && item.deviceId) ||
          ('device_id' in item && item.device_id);
        if (id) {
          nextDevices[id] = normalizeIncomingData(item, nextDevices[id]);
        }
      }

      const deviceKeys = Object.keys(nextDevices);

      // If selected device was 'esp32_device' but real devices exist, auto-switch to first real device
      let targetDeviceId = state.selectedDeviceId;
      if (!nextDevices[targetDeviceId] && deviceKeys.length > 0) {
        targetDeviceId = deviceKeys[0];
      }

      const activeDev = nextDevices[targetDeviceId] || Object.values(nextDevices)[0];
      if (activeDev) {
        return {
          devices: nextDevices,
          selectedDeviceId: activeDev.deviceId,
          deviceId: activeDev.deviceId,
          status: activeDev.status,
          uptime_s: activeDev.uptime_s,
          rssi_dbm: activeDev.rssi_dbm,
          free_heap_b: activeDev.free_heap_b,
          ntpSynced: activeDev.ntpSynced,
          rtcValid: activeDev.rtcValid,
          timeSource: activeDev.timeSource,
          lastSyncUnixTimeUtc: activeDev.lastSyncUnixTimeUtc,
          lastSeenAt: activeDev.lastSeenAt,
          reason: activeDev.reason,
          syncState: activeDev.syncState,
          reportedScheduleState: activeDev.reportedScheduleState,
          scheduleSyncUpdatedAt: activeDev.scheduleSyncUpdatedAt,
          scheduleSyncDetails: activeDev.scheduleSyncDetails,
        };
      }

      return { devices: nextDevices };
    }),


  markOffline: (deviceIdOrReason?: string, reason?: string) =>
    set((state) => {
      let targetDeviceId = state.selectedDeviceId;
      let effectiveReason = reason || 'HEARTBEAT_TIMEOUT';

      if (deviceIdOrReason && state.devices[deviceIdOrReason]) {
        targetDeviceId = deviceIdOrReason;
        effectiveReason = reason || 'HEARTBEAT_TIMEOUT';
      } else if (deviceIdOrReason && !reason) {
        effectiveReason = deviceIdOrReason;
      }

      const currentDev = state.devices[targetDeviceId];
      if (!currentDev) {
        return {
          status: 'offline',
          reason: effectiveReason,
        };
      }

      const updatedDev: SingleDeviceState = {
        ...currentDev,
        status: 'offline',
        reason: effectiveReason,
      };

      const nextDevices = {
        ...state.devices,
        [targetDeviceId]: updatedDev,
      };

      if (targetDeviceId === state.selectedDeviceId) {
        return {
          devices: nextDevices,
          status: 'offline',
          reason: effectiveReason,
        };
      }

      return { devices: nextDevices };
    }),

  reset: () => set(initialState),
}));
