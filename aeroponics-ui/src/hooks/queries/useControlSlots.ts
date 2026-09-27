import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { apiFetch } from '../../lib/api';
import { QUERY_KEYS } from '../../lib/constants';
import type { ControlSlot, UpdateControlSlotDto } from '../../lib/types';

export function useControlSlots() {
  return useQuery({
    queryKey: QUERY_KEYS.CONTROL_SLOTS,
    queryFn: () => apiFetch<ControlSlot[]>('/control-slot'),
    staleTime: 3000,
    refetchInterval: 5000,
  });
}

export function useUpdateControlSlot() {
  const queryClient = useQueryClient();
  return useMutation({
    mutationFn: ({ slotIndex, dto }: { slotIndex: number; dto: UpdateControlSlotDto }) =>
      apiFetch<ControlSlot>(`/control-slot/${slotIndex}`, {
        method: 'PUT',
        body: JSON.stringify(dto),
      }),
    onSuccess: () => queryClient.invalidateQueries({ queryKey: QUERY_KEYS.CONTROL_SLOTS }),
  });
}
