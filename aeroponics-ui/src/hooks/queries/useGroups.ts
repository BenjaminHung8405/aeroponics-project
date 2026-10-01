import { useEffect } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { apiFetch } from '../../lib/api';
import { QUERY_KEYS, DEFAULT_STALE_TIME_MS } from '../../lib/constants';
import { useGroupStore } from '../../store/useGroupStore';
import type { GroupStatusResponse, AssignGroupDto } from '../../lib/types';

/**
 * Hook to fetch status of all 4 Timer Groups and sync into useGroupStore.
 */
export function useGroups() {
  const query = useQuery({
    queryKey: QUERY_KEYS.GROUPS,
    queryFn: () => apiFetch<GroupStatusResponse[]>('/group'),
    staleTime: 3000,
    refetchInterval: 5000, // Background poll every 5s
  });

  // Sync query state into Zustand store on every fetch
  useEffect(() => {
    if (query.data && Array.isArray(query.data)) {
      useGroupStore.getState().initGroups(query.data);
    }
  }, [query.data]);

  return query;
}

/**
 * Mutation to assign a treatment version and nodes to a timer group.
 */
export function useAssignGroup() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: ({ groupId, deviceId, dto }: { groupId: number; deviceId: string; dto: AssignGroupDto }) =>
      apiFetch<GroupStatusResponse>(`/group/${groupId}/assign?deviceId=${encodeURIComponent(deviceId)}`, {
        method: 'PUT',
        body: JSON.stringify(dto),
      }),
    onSuccess: (data) => {
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.GROUPS });
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.NODES });
      if (data) {
        useGroupStore.getState().initGroups([data]);
      }
    },
  });
}

/**
 * Mutation to unassign a timer group.
 */
export function useUnassignGroup() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: ({ groupId, deviceId }: { groupId: number; deviceId: string }) =>
      apiFetch<GroupStatusResponse>(`/group/${groupId}/assign?deviceId=${encodeURIComponent(deviceId)}`, {
        method: 'DELETE',
      }),
    onSuccess: (data) => {
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.GROUPS });
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.NODES });
      if (data) {
        useGroupStore.getState().initGroups([data]);
      }
    },
  });
}
