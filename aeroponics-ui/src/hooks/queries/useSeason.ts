import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { apiFetch } from '../../lib/api';
import { QUERY_KEYS, DEFAULT_STALE_TIME_MS } from '../../lib/constants';
import type {
  Season,
  CreateSeasonDto,
  EndSeasonDto,
  SeasonListResponse,
} from '../../lib/types';

/**
 * Hook to fetch the currently active season.
 * Returns null gracefully if no active season exists (Hard Rule S4-SEASON-10 & S4-NULL-06).
 */
export function useActiveSeason() {
  return useQuery({
    queryKey: QUERY_KEYS.SEASON_ACTIVE,
    queryFn: () => apiFetch<Season | null>('/season/active'),
    staleTime: DEFAULT_STALE_TIME_MS,
  });
}

/**
 * Hook to fetch list of historical seasons.
 */
export function useSeasonHistory() {
  return useQuery({
    queryKey: QUERY_KEYS.SEASON_LIST,
    queryFn: () => apiFetch<SeasonListResponse>('/season'),
    staleTime: DEFAULT_STALE_TIME_MS,
  });
}

/**
 * Mutation to create a new agricultural season.
 */
export function useCreateSeason() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: (dto: CreateSeasonDto) =>
      apiFetch<Season>('/season', {
        method: 'POST',
        body: JSON.stringify(dto),
      }),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.SEASON_ACTIVE });
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.SEASON_LIST });
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.GROUPS });
    },
  });
}

/**
 * Mutation to end an active season.
 */
export function useEndSeason() {
  const queryClient = useQueryClient();

  return useMutation({
    mutationFn: ({ id, dto }: { id: number; dto: EndSeasonDto }) =>
      apiFetch<Season>(`/season/${id}/end`, {
        method: 'PUT',
        body: JSON.stringify(dto),
      }),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.SEASON_ACTIVE });
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.SEASON_LIST });
      queryClient.invalidateQueries({ queryKey: QUERY_KEYS.GROUPS });
    },
  });
}
