/**
 * Zustand Store for Timer Groups (1..4)
 * Follows:
 *  - S4-C3: Type-safe immutable updates, Object.assign / spread pattern.
 *  - ui-ux-pro-max: Granular selectors to prevent unnecessary re-renders.
 */

import { create } from 'zustand';
import { useShallow } from 'zustand/react/shallow';
import type {
  TimerGroupStatus,
  CyclePhase,
  GroupTreatmentSummary,
  GroupStatusResponse,
} from '../lib/types';

export interface GroupState {
  groupId: number;
  name: string;
  status: TimerGroupStatus;
  phase: CyclePhase | null;
  nextTransitionAt: string | null;
  treatment: GroupTreatmentSummary | null;
  treatmentVersionId: number | null;
  nodeIds: number[];
}

const createDefaultGroup = (groupId: number): GroupState => ({
  groupId,
  name: `Group #${groupId}`,
  status: 'UNASSIGNED',
  phase: null,
  nextTransitionAt: null,
  treatment: null,
  treatmentVersionId: null,
  nodeIds: [],
});

/**
 * Stable fallback for selectors. `useSyncExternalStore` requires a
 * referentially stable snapshot, so a missing group must not allocate a new
 * object on every read (that causes an infinite re-render loop, React #185).
 */
const defaultGroupCache = new Map<number, GroupState>();
const getDefaultGroup = (groupId: number): GroupState => {
  let group = defaultGroupCache.get(groupId);
  if (!group) {
    group = createDefaultGroup(groupId);
    defaultGroupCache.set(groupId, group);
  }
  return group;
};

export interface GroupStoreState {
  groups: Record<number, GroupState>;
  initGroups: (groupResponses: GroupStatusResponse[]) => void;
  updateGroup: (groupId: number, partial: Partial<GroupState>) => void;
  resetAll: () => void;
}

const initialGroups: Record<number, GroupState> = {
  1: createDefaultGroup(1),
  2: createDefaultGroup(2),
  3: createDefaultGroup(3),
  4: createDefaultGroup(4),
};

export const useGroupStore = create<GroupStoreState>((set) => ({
  groups: initialGroups,

  initGroups: (groupResponses) => {
    set((state) => {
      const updatedGroups = { ...state.groups };
      for (const res of groupResponses) {
        if (res.group_id >= 1 && res.group_id <= 4) {
          const current = updatedGroups[res.group_id] || createDefaultGroup(res.group_id);
          updatedGroups[res.group_id] = {
            ...current,
            groupId: res.group_id,
            name: res.name || `Group #${res.group_id}`,
            status: res.status || 'UNASSIGNED',
            phase: res.current_phase,
            nextTransitionAt: res.next_transition_at,
            treatment: res.treatment,
            treatmentVersionId: res.treatment?.treatment_version_id ?? null,
            nodeIds: res.nodes ? res.nodes.map((n) => n.node_id) : [],
          };
        }
      }
      return { groups: updatedGroups };
    });
  },

  updateGroup: (groupId, partial) => {
    if (groupId < 1 || groupId > 4) return;
    set((state) => {
      const current = state.groups[groupId] || createDefaultGroup(groupId);
      return {
        groups: {
          ...state.groups,
          [groupId]: {
            ...current,
            ...partial,
          },
        },
      };
    });
  },

  resetAll: () => set({ groups: initialGroups }),
}));

// ==========================================
// Granular Selectors (ui-ux-pro-max performance)
// ==========================================

export const useGroup = (groupId: number): GroupState =>
  useGroupStore((state) => state.groups[groupId] ?? getDefaultGroup(groupId));

export const useAllGroups = (): GroupState[] =>
  useGroupStore(
    useShallow((state) => [
      state.groups[1] ?? getDefaultGroup(1),
      state.groups[2] ?? getDefaultGroup(2),
      state.groups[3] ?? getDefaultGroup(3),
      state.groups[4] ?? getDefaultGroup(4),
    ]),
  );

export const findGroupByNodeId = (
  groups: Record<number, GroupState>,
  nodeId: number,
  cachedGroupId?: number | null,
): GroupState | null => {
  if (cachedGroupId && groups[cachedGroupId]) {
    return groups[cachedGroupId];
  }
  for (let id = 1; id <= 4; id++) {
    const g = groups[id];
    if (g && g.nodeIds && g.nodeIds.includes(nodeId)) {
      return g;
    }
  }
  return null;
};

export const useGroupByNodeId = (
  nodeId: number,
  cachedGroupId?: number | null,
): GroupState | null =>
  useGroupStore((state) => findGroupByNodeId(state.groups, nodeId, cachedGroupId));
