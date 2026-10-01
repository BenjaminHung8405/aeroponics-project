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
  const queryKey = [QUERY_KEYS.CONTROL_SLOTS, deviceId ?? 'default'];
  const queryParam = deviceId ? `?deviceId=${encodeURIComponent(deviceId)}` : '';
  return useMutation({
    mutationFn: ({ slotIndex, dto }: { slotIndex: number; dto: UpdateControlSlotDto }) =>
      apiFetch<ControlSlot>(`/control-slot/${slotIndex}${queryParam}`, {
        method: 'PUT',
        body: JSON.stringify(dto),
      }),
    onMutate: async ({ slotIndex, dto }) => {
      await queryClient.cancelQueries({ queryKey });
      const previousSlots = queryClient.getQueryData<ControlSlot[]>(queryKey);
      if (previousSlots) {
        queryClient.setQueryData<ControlSlot[]>(
          queryKey,
          previousSlots.map((s) =>
            s.slot_index === slotIndex
              ? { ...s, target_type: dto.target_type ?? null, target_id: dto.target_id ?? null }
              : s,
          ),
        );
      }
      return { previousSlots };
    },
    onError: (_err, _vars, context) => {
      if (context?.previousSlots) {
        queryClient.setQueryData(queryKey, context.previousSlots);
      }
    },
    onSettled: () => queryClient.invalidateQueries({ queryKey: [QUERY_KEYS.CONTROL_SLOTS] }),
  });
}
