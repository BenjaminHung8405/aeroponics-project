import { useEffect } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { apiFetch } from '../../lib/api';
import { QUERY_KEYS, DEFAULT_STALE_TIME_MS } from '../../lib/constants';
import { useNodeStore } from '../../store/useNodeStore';
import { useDeviceStore } from '../../store/useDeviceStore';
import type {
  NodeStatusResponse,
  UpdateNodeCalibrationDto,
  RfScanResponse,
  ClaimNodeDto,
  SendPumpCommandDto,
} from '../../lib/types';

/**
 * Hook to fetch all Actuator Nodes status and sync into useNodeStore.
 */
export function useNodes() {
  const query = useQuery({
    queryKey: QUERY_KEYS.NODES,
    queryFn: () => apiFetch<NodeStatusResponse[]>('/node'),
    staleTime: 2000,
    refetchInterval: 3000, // Real-time REST polling every 3s
  });

  // Sync query state into Zustand store on every fetch
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

/**
 * Mutation to send a manual pump override (ON or OFF) to an actuator node.
 * Accepts the backend SendPumpCommandDto directly — no field mapping needed.
 * Routes through the assigned timer group endpoint if group_id is present,
 * or direct node override endpoint otherwise.
 */
export function useSendPumpOverride(deviceId?: string | null) {
  const queryClient = useQueryClient();
  const selectedDeviceId = useDeviceStore((s) => s.selectedDeviceId);
  const activeDeviceId = deviceId || selectedDeviceId || null;

  return useMutation({
    mutationFn: (dto: SendPumpCommandDto & { deviceId?: string }) => {
      const targetDeviceId = dto.deviceId || activeDeviceId;
      const queryParam = targetDeviceId ? `?deviceId=${encodeURIComponent(targetDeviceId)}` : '';
      const endpoint = dto.target_type === 'GROUP'
        ? `/group/${dto.group_id}/command${queryParam}`
        : `/node/${dto.node_id}/override${queryParam}`;
      const { group_id: _groupId, ...nodeCommand } = dto;
      delete (nodeCommand as any).deviceId;
      const command = dto.target_type === 'GROUP'
        ? { ...nodeCommand, group_id: dto.group_id }
        : nodeCommand;
      return apiFetch<any>(endpoint, {
        method: 'POST',
        body: JSON.stringify({ source: 'MANUAL_OVERRIDE', ...command }),
      });
    },
    onSuccess: (_, dto) => {
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.NODES });
      if (dto.node_id) {
        useNodeStore.getState().updateNode(dto.node_id, {
          overrideState: dto.action === 'ON' ? 'OVERRIDE_ON' : 'OVERRIDE_OFF',
        });
      }
    },
  });
}

/**
 * Mutation to trigger RF probe sweep on Gateway.
 */
export function useScanRfNodes(deviceId?: string | null) {
  const queryClient = useQueryClient();
  const selectedDeviceId = useDeviceStore((s) => s.selectedDeviceId);
  const activeDeviceId = deviceId || selectedDeviceId || null;
  const queryParam = activeDeviceId ? `?deviceId=${encodeURIComponent(activeDeviceId)}` : '';
  return useMutation({
    mutationFn: async () => {
      const controller = new AbortController();
      const timeout = window.setTimeout(() => controller.abort(), 15000);
      try {
        return await apiFetch<RfScanResponse>(`/node/scan${queryParam}`, {
          method: 'POST',
          signal: controller.signal,
        });
      } finally {
        window.clearTimeout(timeout);
      }
    },
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.NODES });
    },
  });
}

/**
 * Mutation to claim an RF node and assign it to an actuator node slot (1..4).
 */
export function useClaimNode(deviceId?: string | null) {
  const queryClient = useQueryClient();
  const selectedDeviceId = useDeviceStore((s) => s.selectedDeviceId);
  const activeDeviceId = deviceId || selectedDeviceId || null;

  return useMutation({
    mutationFn: (dto: ClaimNodeDto & { deviceId?: string }) => {
      const targetDeviceId = dto.deviceId || activeDeviceId;
      const queryParam = targetDeviceId ? `?deviceId=${encodeURIComponent(targetDeviceId)}` : '';
      const { deviceId: _ignored, ...claimDto } = dto;
      return apiFetch<NodeStatusResponse>(`/node/claim${queryParam}`, {
        method: 'POST',
        body: JSON.stringify(claimDto),
      });
    },
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.NODES });
    },
  });
}
