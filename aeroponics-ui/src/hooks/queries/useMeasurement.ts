import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { apiFetch } from '../../lib/api';
import { QUERY_KEYS, DEFAULT_STALE_TIME_MS } from '../../lib/constants';
import type {
  MeasurementReadingResponse,
  MeasurementHistoryResponse,
  MeasurementHistoryQuery,
  TriggerMeasurementDto,
  TuyaBridgeStatusResponse,
  ToggleTuyaBridgeDto,
} from '../../lib/types';

/**
 * Hook to fetch Tuya PH-W218 Bridge status (Probe protection mode, active/disabled state).
 */
export function useTuyaBridgeStatus() {
  return useQuery({
    queryKey: QUERY_KEYS.MEASUREMENT_STATUS,
    queryFn: () =>
      apiFetch<TuyaBridgeStatusResponse>('/measurement/status'),
    staleTime: DEFAULT_STALE_TIME_MS,
  });
}

/**
 * Mutation to toggle Tuya PH-W218 Bridge (Enable for late-season/experiments, Disable for probe preservation).
 */
export function useToggleTuyaBridge() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: (dto: ToggleTuyaBridgeDto) =>
      apiFetch<TuyaBridgeStatusResponse>('/measurement/toggle', {
        method: 'POST',
        body: JSON.stringify(dto),
      }),
    onSuccess: (data) => {
      queryClient.setQueryData(QUERY_KEYS.MEASUREMENT_STATUS, data);
      queryClient.invalidateQueries({ queryKey: ['measurement'] });
    },
  });
}

/**
 * Hook to fetch the most recent water quality reading from Tuya PH-W218.
 * Returns null if no reading has been taken yet.
 */
export function useLatestMeasurement() {
  return useQuery({
    queryKey: QUERY_KEYS.MEASUREMENT_LATEST,
    queryFn: () =>
      apiFetch<MeasurementReadingResponse | null>('/measurement/latest'),
    staleTime: DEFAULT_STALE_TIME_MS,
  });
}

/**
 * Hook to fetch paginated measurement history.
 */
export function useMeasurementHistory(query?: MeasurementHistoryQuery) {
  const limit = query?.limit ?? 50;
  const offset = query?.offset ?? 0;
  const triggerType = query?.trigger_type;

  return useQuery({
    queryKey: QUERY_KEYS.MEASUREMENT_HISTORY(limit, offset, triggerType),
    queryFn: () => {
      const searchParams = new URLSearchParams();
      searchParams.set('limit', String(limit));
      searchParams.set('offset', String(offset));
      if (triggerType) {
        searchParams.set('trigger_type', triggerType);
      }
      return apiFetch<MeasurementHistoryResponse>(
        `/measurement/history?${searchParams.toString()}`,
      );
    },
    staleTime: DEFAULT_STALE_TIME_MS,
  });
}

/**
 * Mutation to trigger an on-demand Tuya PH-W218 measurement session.
 * Adheres to Hard Rule S4-ON-DEMAND-11: Zero setInterval polling.
 * Handles HTTP 429 cooldown window safely without crashing.
 */
export function useTriggerMeasurement() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: (dto?: TriggerMeasurementDto) =>
      apiFetch<MeasurementReadingResponse>('/measurement/trigger', {
        method: 'POST',
        body: JSON.stringify(dto || { trigger_type: 'ON_DEMAND' }),
      }),
    onSuccess: (data) => {
      queryClient.setQueryData(QUERY_KEYS.MEASUREMENT_LATEST, data);
      queryClient.invalidateQueries({ queryKey: ['measurement'] });
    },
  });
}
