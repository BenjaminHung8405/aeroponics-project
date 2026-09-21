import { useEffect } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { apiFetch } from '../../lib/api';
import { QUERY_KEYS, DEFAULT_STALE_TIME_MS } from '../../lib/constants';
import { useNodeStore } from '../../store/useNodeStore';
import type {
  NodeStatusResponse,
  UpdateNodeCalibrationDto,
  RfScanResponse,
  ClaimNodeDto,
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

export interface SendPumpOverrideParams {
  nodeId: number;
  groupId?: number | null;
  action: 'ON' | 'OFF';
  runLeaseMs?: number;
  overrideDurationMs?: number;
}

/**
 * Mutation to send a manual pump override (ON or OFF) to an actuator node.
 * Routes through the assigned timer group endpoint if assigned, or direct node override endpoint.
 */
export function useSendPumpOverride() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: ({
      nodeId,
      groupId,
      action,
      runLeaseMs = 30000,
      overrideDurationMs,
    }: SendPumpOverrideParams) => {
      const payload: Record<string, unknown> = {
        node_id: nodeId,
        action,
        run_lease_ms: runLeaseMs,
        source: 'MANUAL_OVERRIDE',
      };
      if (action === 'OFF' && overrideDurationMs) {
        payload.override_duration_ms = overrideDurationMs;
      }
      const endpoint = groupId ? `/group/${groupId}/command` : `/node/${nodeId}/override`;
      return apiFetch<any>(endpoint, {
        method: 'POST',
        body: JSON.stringify(payload),
      });
    },
    onSuccess: (_, variables) => {
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.NODES });
      useNodeStore.getState().updateNode(variables.nodeId, {
        overrideState: variables.action === 'ON' ? 'OVERRIDE_ON' : 'OVERRIDE_OFF',
      });
    },
  });
}

/**
 * Mutation to trigger RF probe sweep on Gateway.
 */
export function useScanRfNodes() {
  const queryClient = useQueryClient();
  return useMutation({
    mutationFn: () =>
      apiFetch<RfScanResponse>('/node/scan', {
        method: 'POST',
      }),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.NODES });
    },
  });
}

/**
 * Mutation to claim an RF node and assign it to an actuator node slot (1..4).
 */
export function useClaimNode() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: (dto: ClaimNodeDto) =>
      apiFetch<NodeStatusResponse>('/node/claim', {
        method: 'POST',
        body: JSON.stringify(dto),
      }),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.NODES });
    },
  });
}


