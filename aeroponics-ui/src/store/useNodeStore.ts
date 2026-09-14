/**
 * Zustand Store for Actuator Nodes (1..4)
 * Follows:
 *  - S4-C3: Type-safe immutable updates, Object.assign / spread pattern.
 *  - ui-ux-pro-max: Granular selectors to isolate re-renders between nodes.
 */

import { create } from 'zustand';
import { useShallow } from 'zustand/react/shallow';
import type {
  NodeHealthStatus,
  CalibrationStatus,
  NodeStatusResponse,
} from '../lib/types';

export interface NodeState {
  id: number;
  displayName: string;
  cachedGroupId: number | null;
  healthStatus: NodeHealthStatus;
  calibrationStatus: CalibrationStatus;
  scheduleState: string;
  overrideState: string;
  flowLpm: number;
  litresTotal: number;
  outcome: string;
  flowConfirmed: boolean;
  isStale: boolean;
  staleForMs: number;
  lastSeenAt: string | null;
  sensorSerial: string | null;
  lastCommandId: string | null;
  flowConfirmedAt: string | null;
}

const createDefaultNode = (id: number): NodeState => ({
  id,
  displayName: `Node #${id}`,
  cachedGroupId: null,
  healthStatus: 'OK',
  calibrationStatus: 'UNCALIBRATED',
  scheduleState: 'IDLE',
  overrideState: 'NONE',
  flowLpm: 0,
  litresTotal: 0,
  outcome: 'PENDING',
  flowConfirmed: false,
  isStale: false,
  staleForMs: 0,
  lastSeenAt: null,
  sensorSerial: null,
  lastCommandId: null,
  flowConfirmedAt: null,
});

export interface NodeStoreState {
  nodes: Record<number, NodeState>;
  initNodes: (nodeResponses: NodeStatusResponse[]) => void;
  updateNode: (id: number, partial: Partial<NodeState>) => void;
  resetAll: () => void;
}

const initialNodes: Record<number, NodeState> = {
  1: createDefaultNode(1),
  2: createDefaultNode(2),
  3: createDefaultNode(3),
  4: createDefaultNode(4),
};

export const useNodeStore = create<NodeStoreState>((set) => ({
  nodes: initialNodes,

  initNodes: (nodeResponses) => {
    set((state) => {
      const updatedNodes = { ...state.nodes };
      for (const res of nodeResponses) {
        if (res.node_id >= 1 && res.node_id <= 4) {
          const current = updatedNodes[res.node_id] || createDefaultNode(res.node_id);
          updatedNodes[res.node_id] = {
            ...current,
            id: res.node_id,
            displayName: res.display_name || `Node #${res.node_id}`,
            cachedGroupId: res.cached_group_id,
            healthStatus: res.health_status || 'OK',
            calibrationStatus: res.calibration_status || 'UNCALIBRATED',
            scheduleState: res.schedule_state || 'IDLE',
            overrideState: res.override_state || 'NONE',
            isStale: Boolean(res.is_stale),
            staleForMs: res.stale_for_ms || 0,
            lastSeenAt: res.last_seen_at,
            sensorSerial: res.sensor_serial,
          };
        }
      }
      return { nodes: updatedNodes };
    });
  },

  updateNode: (id, partial) => {
    if (id < 1 || id > 4) return;
    set((state) => {
      const current = state.nodes[id] || createDefaultNode(id);
      return {
        nodes: {
          ...state.nodes,
          [id]: {
            ...current,
            ...partial,
          },
        },
      };
    });
  },

  resetAll: () => set({ nodes: initialNodes }),
}));

// ==========================================
// Granular Selectors (ui-ux-pro-max performance)
// ==========================================

export const useNode = (id: number): NodeState =>
  useNodeStore((state) => state.nodes[id] ?? createDefaultNode(id));

export const useAllNodes = (): NodeState[] =>
  useNodeStore(
    useShallow((state) => [
      state.nodes[1] ?? createDefaultNode(1),
      state.nodes[2] ?? createDefaultNode(2),
      state.nodes[3] ?? createDefaultNode(3),
      state.nodes[4] ?? createDefaultNode(4),
    ]),
  );

export const useNodeOutcome = (id: number): string =>
  useNodeStore((state) => state.nodes[id]?.outcome ?? 'PENDING');

export const useNodeFlow = (id: number) =>
  useNodeStore(
    useShallow((state) => ({
      flowLpm: state.nodes[id]?.flowLpm ?? 0,
      litresTotal: state.nodes[id]?.litresTotal ?? 0,
      flowConfirmed: state.nodes[id]?.flowConfirmed ?? false,
    })),
  );
