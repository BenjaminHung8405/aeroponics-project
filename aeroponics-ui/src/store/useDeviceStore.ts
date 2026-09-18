/**
 * Zustand Store for ESP32 Gateway Device Status
 * Follows:
 *  - Type-safe immutable updates.
 *  - Real-time updates from WebSocket 'device_status'.
 *  - Fallback offline state on disconnect or timeout.
 */

import { create } from 'zustand';
import type {
  DeviceConnectionStatus,
  DeviceStatusResponse,
  DeviceStatusWsData,
} from '../lib/types';

export interface DeviceStoreState {
  deviceId: string;
  status: DeviceConnectionStatus;
  uptime_s: number;
  rssi_dbm: number | null;
  free_heap_b: number | null;
  ntpSynced: boolean;
  rtcValid: boolean;
  lastSeenAt: string | null;
  reason: string | null;

  setDeviceStatus: (
    data: Partial<DeviceStatusResponse> | DeviceStatusWsData,
  ) => void;
  markOffline: (reason?: string) => void;
  reset: () => void;
}

const initialState = {
  deviceId: 'esp32_device',
  status: 'unknown' as DeviceConnectionStatus,
  uptime_s: 0,
  rssi_dbm: null,
  free_heap_b: null,
  ntpSynced: false,
  rtcValid: false,
  lastSeenAt: null,
  reason: null,
};

export const useDeviceStore = create<DeviceStoreState>((set) => ({
  ...initialState,

  setDeviceStatus: (data) =>
    set((state) => {
      // Normalize property names between REST response and WS data
      const deviceId =
        ('deviceId' in data && data.deviceId) ||
        ('device_id' in data && data.device_id) ||
        state.deviceId;

      const status =
        (data.status as DeviceConnectionStatus) || state.status;

      const uptime_s =
        data.uptime_s !== undefined ? Number(data.uptime_s) : state.uptime_s;

      const rssi_dbm =
        data.rssi_dbm !== undefined ? data.rssi_dbm : state.rssi_dbm;

      const free_heap_b =
        data.free_heap_b !== undefined ? data.free_heap_b : state.free_heap_b;

      const ntpSynced =
        'ntpSynced' in data && data.ntpSynced !== undefined
          ? Boolean(data.ntpSynced)
          : 'ntp_synced' in data && data.ntp_synced !== undefined
            ? Boolean(data.ntp_synced)
            : state.ntpSynced;

      const rtcValid =
        'rtcValid' in data && data.rtcValid !== undefined
          ? Boolean(data.rtcValid)
          : 'rtc_valid' in data && data.rtc_valid !== undefined
            ? Boolean(data.rtc_valid)
            : state.rtcValid;

      const lastSeenAt =
        'lastSeenAt' in data && data.lastSeenAt !== undefined
          ? data.lastSeenAt
          : 'last_seen_at' in data && data.last_seen_at !== undefined
            ? data.last_seen_at
            : state.lastSeenAt;

      const reason = 'reason' in data ? (data.reason ?? null) : null;

      return {
        deviceId,
        status,
        uptime_s,
        rssi_dbm,
        free_heap_b,
        ntpSynced,
        rtcValid,
        lastSeenAt,
        reason,
      };
    }),

  markOffline: (reason?: string) =>
    set({
      status: 'offline',
      reason: reason || 'HEARTBEAT_TIMEOUT',
    }),

  reset: () => set(initialState),
}));
