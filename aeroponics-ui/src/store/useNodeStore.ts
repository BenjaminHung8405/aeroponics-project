/**
 * Zustand Store for physical AGU RF nodes (4..7)
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
  rfProtocol: string | null;
  lastScanId: string | null;
  lastRfRttMs: number | null;
  lastDiscoveredAt: string | null;
  discoveryStatus: string | null;
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
  rfProtocol: null,
  lastScanId: null,
  lastRfRttMs: null,
  lastDiscoveredAt: null,
  discoveryStatus: null,
});

export interface NodeStoreState {
  nodes: Record<number, NodeState>;
  initNodes: (nodeResponses: NodeStatusResponse[]) => void;
  updateNode: (id: number, partial: Partial<NodeState>) => void;
  resetAll: () => void;
  applyFlowConfirmed: (
    id: number,
    flowConfirmed: boolean,
    flowRateLpm?: number,
    confirmedAt?: string | null,
  ) => void;
  updateOutcome: (id: number, outcome: string) => void;
}

export const AGU_NODE_IDS = [4, 5, 6, 7] as const;
const initialNodes: Record<number, NodeState> = Object.fromEntries(
  AGU_NODE_IDS.map((id) => [id, createDefaultNode(id)]),
);

export const useNodeStore = create<NodeStoreState>((set) => ({
  nodes: initialNodes,

  initNodes: (nodeResponses) => {
    set((state) => {
      const updatedNodes = { ...state.nodes };
      for (const res of nodeResponses) {
        if (AGU_NODE_IDS.includes(res.node_id as (typeof AGU_NODE_IDS)[number])) {
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
            rfProtocol: res.rf_protocol ?? null,
            lastScanId: res.last_scan_id ?? null,
            lastRfRttMs: res.last_rf_rtt_ms ?? null,
            lastDiscoveredAt: res.last_discovered_at ?? null,
            discoveryStatus: res.discovery_status ?? null,
          };
        }
      }
      return { nodes: updatedNodes };
    });
  },

  updateNode: (id, partial) => {
    if (!AGU_NODE_IDS.includes(id as (typeof AGU_NODE_IDS)[number])) return;
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

  /**
   * S4-WS-02: The only action that may set `flowConfirmed` (server-authoritative).
   * Called exclusively from the WebSocket dispatcher on real `node_flow` events.
   */
  applyFlowConfirmed: (id, flowConfirmed, flowRateLpm, confirmedAt) => {
    if (!AGU_NODE_IDS.includes(id as (typeof AGU_NODE_IDS)[number])) return;
    set((state) => {
      const current = state.nodes[id] || createDefaultNode(id);
      return {
        nodes: {
          ...state.nodes,
          [id]: {
            ...current,
            flowConfirmed,
            ...(flowRateLpm !== undefined ? { flowLpm: flowRateLpm } : {}),
            ...(confirmedAt !== undefined
              ? { flowConfirmedAt: confirmedAt }
              : {}),
          },
        },
      };
    });
  },

  /**
   * Outcome-only update from `pump_command_update` WS events.
   * Never infers RUNNING — RUNNING is derived from `isNodeRunning()`.
   */
  updateOutcome: (id, outcome) => {
    if (!AGU_NODE_IDS.includes(id as (typeof AGU_NODE_IDS)[number])) return;
    set((state) => {
      const current = state.nodes[id] || createDefaultNode(id);
      return {
        nodes: {
          ...state.nodes,
          [id]: {
            ...current,
            outcome,
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
      state.nodes[4] ?? createDefaultNode(4),
      state.nodes[5] ?? createDefaultNode(5),
      state.nodes[6] ?? createDefaultNode(6),
      state.nodes[7] ?? createDefaultNode(7),
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
