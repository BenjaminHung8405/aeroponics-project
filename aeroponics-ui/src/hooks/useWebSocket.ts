'use client';

/**
 * Native WebSocket Hook for Aeroponics Smart Farm
 * Follows:
 *  - S4-WS-04: Native WebSocket only. Zero full page reloads.
 *  - S4-C2: Exponential backoff max 30s. Dispatch events directly to Zustand stores.
 *  - S4-API-05: Dynamic WS URL derivation from NEXT_PUBLIC_WS_URL or window.location.
 */

import { useState, useEffect, useCallback, useRef } from 'react';
import { useNodeStore } from '../store/useNodeStore';
import { useGroupStore } from '../store/useGroupStore';
import {
  WS_RECONNECT_MAX_DELAY_MS,
  WS_RECONNECT_BASE_DELAY_MS,
  WS_RECONNECT_FACTOR,
  WS_EVENTS,
} from '../lib/constants';
import type {
  WebSocketBroadcastMessage,
  NodeTelemetryWsData,
  NodeFlowWsData,
  PumpCommandUpdateWsData,
  GroupStatusWsData,
  StalenessAlertWsData,
} from '../lib/types';

export type WsConnectionState = 'connecting' | 'connected' | 'disconnected' | 'reconnecting';

export interface UseWebSocketReturn {
  isConnected: boolean;
  connectionState: WsConnectionState;
  retryCount: number;
  reconnectNow: () => void;
}

/**
 * Calculates exponential backoff delay capped at WS_RECONNECT_MAX_DELAY_MS (30s).
 */
export function calculateBackoffDelay(retryCount: number): number {
  return Math.min(
    WS_RECONNECT_MAX_DELAY_MS,
    WS_RECONNECT_BASE_DELAY_MS * Math.pow(WS_RECONNECT_FACTOR, retryCount),
  );
}

/**
 * Derives the WebSocket connection URL.
 */
export function resolveWsUrl(): string {
  if (process.env.NEXT_PUBLIC_WS_URL) {
    return process.env.NEXT_PUBLIC_WS_URL;
  }
  if (typeof window === 'undefined') {
    return 'ws://localhost:3000/ws';
  }
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${protocol}//${window.location.host}/ws`;
}

// Global connection state manager (Singleton pattern across client components)
class WebSocketManager {
  private static instance: WebSocketManager | null = null;
  private ws: WebSocket | null = null;
  private retryCount = 0;
  private reconnectTimer: NodeJS.Timeout | null = null;
  private listeners = new Set<(state: WsConnectionState, retry: number) => void>();
  private connectionState: WsConnectionState = 'disconnected';
  private refCount = 0;

  private constructor() {}

  public static getInstance(): WebSocketManager {
    if (!WebSocketManager.instance) {
      WebSocketManager.instance = new WebSocketManager();
    }
    return WebSocketManager.instance;
  }

  public subscribe(listener: (state: WsConnectionState, retry: number) => void): () => void {
    this.listeners.add(listener);
    this.refCount++;

    // Notify current state immediately
    listener(this.connectionState, this.retryCount);

    if (this.refCount === 1 && !this.ws) {
      this.connect();
    }

    return () => {
      this.listeners.delete(listener);
      this.refCount--;
      if (this.refCount <= 0) {
        this.disconnect();
      }
    };
  }

  private setState(newState: WsConnectionState) {
    this.connectionState = newState;
    for (const listener of this.listeners) {
      listener(this.connectionState, this.retryCount);
    }
  }

  public connect() {
    if (typeof window === 'undefined') return;

    if (this.ws && (this.ws.readyState === WebSocket.CONNECTING || this.ws.readyState === WebSocket.OPEN)) {
      return;
    }

    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }

    const url = resolveWsUrl();
    this.setState(this.retryCount > 0 ? 'reconnecting' : 'connecting');

    try {
      this.ws = new WebSocket(url);

      this.ws.onopen = () => {
        this.retryCount = 0;
        this.setState('connected');
      };

      this.ws.onmessage = (event) => {
        this.handleMessage(event.data);
      };

      this.ws.onerror = () => {
        // Handled in onclose
      };

      this.ws.onclose = () => {
        this.ws = null;
        if (this.refCount > 0) {
          this.scheduleReconnect();
        } else {
          this.setState('disconnected');
        }
      };
    } catch {
      this.scheduleReconnect();
    }
  }

  private scheduleReconnect() {
    const delay = calculateBackoffDelay(this.retryCount);
    this.retryCount++;
    this.setState('reconnecting');

    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
    }

    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this.connect();
    }, delay);
  }

  public reconnectNow() {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    if (this.ws) {
      try {
        this.ws.close();
      } catch {}
      this.ws = null;
    }
    this.retryCount = 0;
    this.connect();
  }

  public disconnect() {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    if (this.ws) {
      try {
        this.ws.close();
      } catch {}
      this.ws = null;
    }
    this.retryCount = 0;
    this.setState('disconnected');
  }

  private handleMessage(rawData: any) {
    if (typeof rawData !== 'string') return;

    try {
      const parsed: WebSocketBroadcastMessage = JSON.parse(rawData);
      const { event, data } = parsed;

      switch (event) {
        case WS_EVENTS.CONNECTED:
          this.setState('connected');
          break;

        case WS_EVENTS.NODE_TELEMETRY: {
          const telemetry = data as NodeTelemetryWsData;
          if (telemetry && telemetry.nodeId >= 1 && telemetry.nodeId <= 4) {
            useNodeStore.getState().updateNode(telemetry.nodeId, {
              healthStatus: telemetry.health || 'OK',
              lastSeenAt: telemetry.lastSeenAt,
              scheduleState: telemetry.scheduleState || 'IDLE',
              overrideState: telemetry.overrideState || 'NONE',
              sensorSerial: telemetry.sensorSerial,
              isStale: telemetry.health === 'STALE',
            });
          }
          break;
        }

        case WS_EVENTS.NODE_FLOW: {
          const flow = data as NodeFlowWsData;
          if (flow && flow.nodeId >= 1 && flow.nodeId <= 4) {
            useNodeStore.getState().updateNode(flow.nodeId, {
              flowLpm: flow.flowRateLpm || 0,
              litresTotal: flow.litresTotal || 0,
              flowConfirmed: Boolean(flow.flowConfirmed),
              lastSeenAt: flow.time,
            });
          }
          break;
        }

        case WS_EVENTS.PUMP_COMMAND_UPDATE: {
          const cmd = data as PumpCommandUpdateWsData;
          if (cmd && cmd.nodeId >= 1 && cmd.nodeId <= 4) {
            useNodeStore.getState().updateNode(cmd.nodeId, {
              outcome: cmd.outcome,
              lastCommandId: cmd.commandId,
              flowConfirmedAt: cmd.flowConfirmedAt ?? null,
              flowConfirmed: cmd.outcome === 'FLOW_CONFIRMED',
            });
          }
          break;
        }

        case WS_EVENTS.GROUP_STATUS: {
          const grp = data as GroupStatusWsData;
          if (grp && grp.groupId >= 1 && grp.groupId <= 4) {
            useGroupStore.getState().updateGroup(grp.groupId, {
              phase: grp.phase === 'UNASSIGNED' ? null : grp.phase,
              status: grp.phase === 'UNASSIGNED' ? 'UNASSIGNED' : 'ACTIVE',
              nextTransitionAt: grp.nextTransitionAt,
              treatmentVersionId: grp.treatmentVersionId,
              nodeIds: grp.nodeIds || [],
            });
          }
          break;
        }

        case WS_EVENTS.STALENESS_ALERT: {
          const stale = data as StalenessAlertWsData;
          if (stale && stale.nodeId >= 1 && stale.nodeId <= 4) {
            useNodeStore.getState().updateNode(stale.nodeId, {
              isStale: true,
              staleForMs: stale.staleForMs || 0,
              healthStatus: 'STALE',
              lastSeenAt: stale.lastSeenAt,
            });
          }
          break;
        }

        default:
          break;
      }
    } catch {
      // Ignore malformed JSON messages gracefully
    }
  }
}

/**
 * useWebSocket Hook — React hook providing reactive connection state
 * and manual reconnection trigger.
 */
export function useWebSocket(): UseWebSocketReturn {
  const [connectionState, setConnectionState] = useState<WsConnectionState>('disconnected');
  const [retryCount, setRetryCount] = useState<number>(0);

  useEffect(() => {
    const manager = WebSocketManager.getInstance();
    const unsubscribe = manager.subscribe((state, retries) => {
      setConnectionState(state);
      setRetryCount(retries);
    });

    return () => {
      unsubscribe();
    };
  }, []);

  const reconnectNow = useCallback(() => {
    WebSocketManager.getInstance().reconnectNow();
  }, []);

  return {
    isConnected: connectionState === 'connected',
    connectionState,
    retryCount,
    reconnectNow,
  };
}
