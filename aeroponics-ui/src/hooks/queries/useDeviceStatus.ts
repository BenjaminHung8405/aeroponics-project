import { useEffect } from 'react';
import { useQuery } from '@tanstack/react-query';
import { apiFetch } from '../../lib/api';
import { QUERY_KEYS, DEFAULT_STALE_TIME_MS } from '../../lib/constants';
import { useDeviceStore } from '../../store/useDeviceStore';
import type { DeviceStatusResponse } from '../../lib/types';

/**
 * Hook to fetch ESP32 Gateway device status and sync into useDeviceStore.
 */
export function useDeviceStatus() {
  const query = useQuery({
    queryKey: QUERY_KEYS.DEVICE_STATUS,
    queryFn: async () => {
      try {
        const res = await apiFetch<DeviceStatusResponse[]>('/device/status');
        if (Array.isArray(res) && res.length > 0) {
          return res[0];
        }
        return null;
      } catch {
        return null;
      }
    },
    staleTime: 3000,
    refetchInterval: 5000, // Background poll every 5s for gateway status
  });

  useEffect(() => {
    if (query.data) {
      useDeviceStore.getState().setDeviceStatus(query.data);
    }
  }, [query.data]);

  return query;
}
