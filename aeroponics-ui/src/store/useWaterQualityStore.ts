'use client';

/**
 * Zustand Store for Realtime Water Quality Telemetry (Tuya PH-W218)
 * Decoupled from REST polling, receives instant push notifications via native WebSocket.
 */

import { create } from 'zustand';

export interface WaterQualityTelemetryData {
  sensor_id: string;
  time: string;
  ph: number | null;
  ec: number | null;
  tds: number | null;
  temperature_c: number | null;
  orp: number | null;
  salinity: number | null;
  specific_gravity: number | null;
  conductivity_factor: number | null;
  humidity: number | null;
  season_id: number | null;
  target_ph: number | null;
  target_ec: number | null;
  is_ph_out_of_range: boolean;
  is_ec_out_of_range: boolean;
  status: 'online' | 'offline';
}

export interface WaterQualityStoreState {
  telemetry: WaterQualityTelemetryData | null;
  status: 'online' | 'offline';
  lastSeenAt: string | null;

  setTelemetry: (data: WaterQualityTelemetryData) => void;
  setStatus: (status: 'online' | 'offline', timestamp?: string) => void;
  reset: () => void;
}

const initialState = {
  telemetry: null,
  status: 'online' as const,
  lastSeenAt: null,
};

export const useWaterQualityStore = create<WaterQualityStoreState>((set) => ({
  ...initialState,

  setTelemetry: (data) =>
    set({
      telemetry: data,
      status: data.status || 'online',
      lastSeenAt: data.time || new Date().toISOString(),
    }),

  setStatus: (status, timestamp) =>
    set((state) => ({
      status,
      lastSeenAt: timestamp || new Date().toISOString(),
      telemetry: state.telemetry
        ? { ...state.telemetry, status }
        : null,
    })),

  reset: () => set(initialState),
}));
