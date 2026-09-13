import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { apiFetch } from '../../lib/api';
import { QUERY_KEYS, DEFAULT_STALE_TIME_MS } from '../../lib/constants';
import type {
  Treatment,
  TreatmentVersion,
  TreatmentListResponse,
  CreateTreatmentDto,
  CreateTreatmentVersionDto,
} from '../../lib/types';

/**
 * Hook to fetch paginated list of all treatments.
 */
export function useTreatments() {
  return useQuery({
    queryKey: QUERY_KEYS.TREATMENTS,
    queryFn: () => apiFetch<TreatmentListResponse>('/treatment'),
    staleTime: DEFAULT_STALE_TIME_MS,
  });
}

/**
 * Hook to fetch a single treatment with its version tree.
 */
export function useTreatment(id: number) {
  return useQuery({
    queryKey: QUERY_KEYS.TREATMENT(id),
    queryFn: () => apiFetch<Treatment>(`/treatment/${id}`),
    enabled: Boolean(id && id > 0),
    staleTime: DEFAULT_STALE_TIME_MS,
  });
}

/**
 * Mutation to create a new treatment profile.
 */
export function useCreateTreatment() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: (dto: CreateTreatmentDto) =>
      apiFetch<Treatment>('/treatment', {
        method: 'POST',
        body: JSON.stringify(dto),
      }),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.TREATMENTS });
    },
  });
}

/**
 * Mutation to add a new version to an existing treatment.
 */
export function useCreateTreatmentVersion() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: ({
      treatmentId,
      dto,
    }: {
      treatmentId: number;
      dto: CreateTreatmentVersionDto;
    }) =>
      apiFetch<TreatmentVersion>(`/treatment/${treatmentId}/version`, {
        method: 'POST',
        body: JSON.stringify(dto),
      }),
    onSuccess: (_data, variables) => {
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.TREATMENTS });
      queryClient.invalidateQueries({
        queryKey: QUERY_KEYS.TREATMENT(variables.treatmentId),
      });
    },
  });
}

/**
 * Mutation to publish a treatment version (locking it into immutable state).
 */
export function usePublishTreatmentVersion() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: ({
      treatmentId,
      versionId,
    }: {
      treatmentId: number;
      versionId: number;
    }) =>
      apiFetch<TreatmentVersion>(
        `/treatment/${treatmentId}/version/${versionId}/publish`,
        {
          method: 'PUT',
        },
      ),
    onSuccess: (_data, variables) => {
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.TREATMENTS });
      queryClient.invalidateQueries({
        queryKey: QUERY_KEYS.TREATMENT(variables.treatmentId),
      });
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.GROUPS });
    },
  });
}
