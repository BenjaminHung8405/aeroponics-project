import { useEffect } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { apiFetch } from '../../lib/api';
import { QUERY_KEYS, DEFAULT_STALE_TIME_MS } from '../../lib/constants';
import { useNodeStore } from '../../store/useNodeStore';
import type {
  NodeStatusResponse,
  UpdateNodeCalibrationDto,
} from '../../lib/types';

/**
 * Hook to fetch all Actuator Nodes status and sync into useNodeStore.
 */
export function useNodes() {
  const query = useQuery({
    queryKey: QUERY_KEYS.NODES,
    queryFn: () => apiFetch<NodeStatusResponse[]>('/node'),
    staleTime: DEFAULT_STALE_TIME_MS,
  });

  // Sync initial query state into Zustand store
  useEffect(() => {
    if (query.data && Array.isArray(query.data)) {
      useNodeStore.getState().initNodes(query.data);
    }
  }, [query.data]);

  return query;
}

/**
 * Mutation to update flow sensor calibration pulses and reference volume.
 */
export function useUpdateCalibration() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: ({
      nodeId,
      dto,
    }: {
      nodeId: number;
      dto: UpdateNodeCalibrationDto;
    }) =>
      apiFetch<NodeStatusResponse>(`/node/${nodeId}/calibration`, {
        method: 'PUT',
        body: JSON.stringify(dto),
      }),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.NODES });
    },
  });
}

/**
 * Mutation to explicitly reset a latched hardware fault on a node.
 */
export function useResetNodeFault() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: (nodeId: number) =>
      apiFetch<NodeStatusResponse>(`/node/${nodeId}/fault-reset`, {
        method: 'POST',
      }),
    onSuccess: (data) => {
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.NODES });
      if (data) {
        useNodeStore.getState().updateNode(data.node_id, {
          healthStatus: 'OK',
          isStale: false,
        });
      }
    },
  });
}
