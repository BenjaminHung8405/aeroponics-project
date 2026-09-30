import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { apiFetch } from '../../lib/api';
import { QUERY_KEYS } from '../../lib/constants';
import type { ControlSlot, UpdateControlSlotDto } from '../../lib/types';

export function useControlSlots(deviceId?: string | null) {
  const queryParam = deviceId ? `?deviceId=${encodeURIComponent(deviceId)}` : '';
  return useQuery({
    queryKey: [QUERY_KEYS.CONTROL_SLOTS, deviceId ?? 'default'],
    queryFn: () => apiFetch<ControlSlot[]>(`/control-slot${queryParam}`),
    staleTime: 3000,
    refetchInterval: 5000,
  });
}

export function useUpdateControlSlot(deviceId?: string | null) {
  const queryClient = useQueryClient();
  const queryParam = deviceId ? `?deviceId=${encodeURIComponent(deviceId)}` : '';
  return useMutation({
    mutationFn: ({ slotIndex, dto }: { slotIndex: number; dto: UpdateControlSlotDto }) =>
      apiFetch<ControlSlot>(`/control-slot/${slotIndex}${queryParam}`, {
        method: 'PUT',
        body: JSON.stringify(dto),
      }),
    onSuccess: () => queryClient.invalidateQueries({ queryKey: [QUERY_KEYS.CONTROL_SLOTS] }),
  });
}
