'use client';

import React, {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useState,
  type ReactNode,
} from 'react';
import { apiFetch } from './api';
import { useDeviceStore } from '../store/useDeviceStore';
import type { PublicDevice, DeviceStatusResponse } from './types';

interface SelectedDeviceContextValue {
  devices: PublicDevice[];
  selectedDevice: PublicDevice | null;
  selectedDeviceId: string | null;
  isLoadingDevices: boolean;
  reloadDevices: () => Promise<void>;
  selectDevice: (deviceId: string) => void;
}

const SelectedDeviceContext = createContext<SelectedDeviceContextValue | undefined>(
  undefined,
);

const STORAGE_KEY = 'aeroponics-ui:selected-device-id';

export function SelectedDeviceProvider({ children }: { children: ReactNode }) {
  const storeDevices = useDeviceStore((s) => s.devices);
  const setSelectedInStore = useDeviceStore((s) => s.setSelectedDeviceId);
  const setDevicesListInStore = useDeviceStore((s) => s.setDevicesList);

  const [selectedDeviceId, setSelectedDeviceIdState] = useState<string | null>(null);
  const [isLoadingDevices, setIsLoadingDevices] = useState(true);

  // Convert dictionary into array of PublicDevice sorted by deviceId
  const devices = useMemo<PublicDevice[]>(() => {
    return Object.values(storeDevices)
      .map((d) => ({
        deviceId: d.deviceId,
        displayName: d.displayName,
        status: d.status,
        uptime_s: d.uptime_s,
        rssi_dbm: d.rssi_dbm,
        free_heap_b: d.free_heap_b,
        ntpSynced: d.ntpSynced,
        rtcValid: d.rtcValid,
        timeSource: d.timeSource,
        lastSyncUnixTimeUtc: d.lastSyncUnixTimeUtc,
        lastSeenAt: d.lastSeenAt,
        enabled: d.enabled,
      }))
      .sort((a, b) => a.deviceId.localeCompare(b.deviceId));
  }, [storeDevices]);

  const reloadDevices = useCallback(async () => {
    setIsLoadingDevices(true);
    try {
      const res = await apiFetch<DeviceStatusResponse[]>('/device/status');
      setDevicesListInStore(Array.isArray(res) ? res : []);
    } catch {
      // Fallback to existing store devices
    } finally {
      setIsLoadingDevices(false);
    }
  }, [setDevicesListInStore]);

  useEffect(() => {
    void reloadDevices();
  }, [reloadDevices]);

  // Determine active selected ID with localStorage preference
  useEffect(() => {
    if (devices.length === 0) return;

    setSelectedDeviceIdState((currentId) => {
      let storedId: string | null = null;
      if (typeof window !== 'undefined') {
        try {
          storedId = window.localStorage.getItem(STORAGE_KEY);
        } catch {
          storedId = null;
        }
      }

      if (storedId === 'esp32_device') {
        storedId = null;
        try {
          window.localStorage.removeItem(STORAGE_KEY);
        } catch {
          // Ignore
        }
      }

      const preferredId = currentId || storedId;
      const match = devices.find((d) => d.deviceId === preferredId);
      const chosenId = match ? match.deviceId : devices[0].deviceId;

      setSelectedInStore(chosenId);
      return chosenId;
    });
  }, [devices, setSelectedInStore]);


  const selectDevice = useCallback(
    (deviceId: string) => {
      setSelectedDeviceIdState(deviceId);
      setSelectedInStore(deviceId);
      if (typeof window !== 'undefined') {
        try {
          window.localStorage.setItem(STORAGE_KEY, deviceId);
        } catch {
          // Ignore localStorage errors
        }
      }
    },
    [setSelectedInStore],
  );

  const selectedDevice = useMemo(
    () => devices.find((d) => d.deviceId === selectedDeviceId) ?? null,
    [devices, selectedDeviceId],
  );

  const value = useMemo(
    () => ({
      devices,
      selectedDevice,
      selectedDeviceId,
      isLoadingDevices,
      reloadDevices,
      selectDevice,
    }),
    [devices, selectedDevice, selectedDeviceId, isLoadingDevices, reloadDevices, selectDevice],
  );

  return (
    <SelectedDeviceContext.Provider value={value}>
      {children}
    </SelectedDeviceContext.Provider>
  );
}

export function useSelectedDevice(): SelectedDeviceContextValue {
  const context = useContext(SelectedDeviceContext);
  if (!context) {
    throw new Error('useSelectedDevice must be used within a SelectedDeviceProvider');
  }
  return context;
}
