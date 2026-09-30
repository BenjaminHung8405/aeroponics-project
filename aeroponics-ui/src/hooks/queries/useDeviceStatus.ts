import { useEffect } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
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
        if (Array.isArray(res)) {
          return res;
        }
        return [];
      } catch {
        return [];
      }
    },
    staleTime: 3000,
    refetchInterval: 5000, // Background poll every 5s for gateway status
  });

  useEffect(() => {
    if (query.data && Array.isArray(query.data)) {
      useDeviceStore.getState().setDevicesList(query.data);
    }
  }, [query.data]);

  return query;
}

/**
 * Mutation hook to manually trigger RTC clock synchronization for an ESP32 Gateway.
 */
export function useSyncDeviceClock() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: async (deviceId: string) => {
      return apiFetch<{ success: boolean; device_id: string; timestamp: number }>(
        `/device/${encodeURIComponent(deviceId)}/sync-clock`,
        {
          method: 'POST',
        },
      );
    },
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.DEVICE_STATUS });
    },
  });
}
