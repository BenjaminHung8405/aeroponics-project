import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { apiFetch } from '../../lib/api';
import { QUERY_KEYS } from '../../lib/constants';
import type { ControlSlot, UpdateControlSlotDto } from '../../lib/types';

export function useControlSlots(deviceId?: string | null) {
  return useQuery({
    queryKey: [QUERY_KEYS.CONTROL_SLOTS, deviceId ?? 'none'],
    queryFn: () => {
      // Keep the unscoped endpoint impossible even if React Query invokes the
      // function unexpectedly during a transition between selected devices.
      if (!deviceId) {
        throw new Error('Cannot load control slots without a selected device');
      }
      return apiFetch<ControlSlot[]>(
        `/control-slot?deviceId=${encodeURIComponent(deviceId)}`,
      );
    },
    enabled: Boolean(deviceId),
    staleTime: 3000,
    refetchInterval: 5000,
  });
}

export function useUpdateControlSlot(deviceId?: string | null) {
  const queryClient = useQueryClient();
  const queryKey = [QUERY_KEYS.CONTROL_SLOTS, deviceId ?? 'none'];
  return useMutation({
    mutationFn: ({ slotIndex, dto }: { slotIndex: number; dto: UpdateControlSlotDto }) => {
      if (!deviceId) {
        throw new Error('Cannot update a control slot without a selected device');
      }
      return apiFetch<ControlSlot>(
        `/control-slot/${slotIndex}?deviceId=${encodeURIComponent(deviceId)}`,
        {
        method: 'PUT',
        body: JSON.stringify(dto),
        },
      );
    },
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
