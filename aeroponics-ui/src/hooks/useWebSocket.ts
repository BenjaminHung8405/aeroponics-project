'use client';

/**
 * Native WebSocket client hook for the Aeroponics dashboard.
 *
 * Sprint 4 — Track N (Tasks N1 + N2):
 *  - N1: Real WebSocket connection with exponential backoff (base 1s, factor 1.5, max 30s).
 *  - N2: Server-authoritative message dispatcher from WS events into Zustand stores.
 *
 * Rules enforced:
 *  - S4-WS-03: Reconnect uses exponential backoff. `onclose`/`onerror` never call
 *    `connect()` directly; they schedule a single bounded retry timer.
 *  - S4-WS-06: Backoff delay(n) >= delay(n-1) for all n; capped at 30s.
 *  - S4-WS-02: `flowConfirmed` is only set true via `applyFlowConfirmed()` from
 *    `wsMessageHandler()` — never from REST or optimistic UI.
 *  - S4-NOOPT-01: Only real server events dispatch to the store. No optimistic writes.
 */

import { useEffect, useRef, useState, useCallback } from 'react';
import { useNodeStore } from '../store/useNodeStore';
import { useDeviceStore } from '../store/useDeviceStore';
import {
  WS_EVENTS,
  WS_RECONNECT_BASE_DELAY_MS,
  WS_RECONNECT_FACTOR,
  WS_RECONNECT_MAX_DELAY_MS,
} from '../lib/constants';
import type { WebSocketBroadcastMessage } from '../lib/types';

export type WsConnectionState =
  | 'connecting'
  | 'connected'
  | 'disconnected'
  | 'reconnecting';

export interface UseWebSocketReturn {
  isConnected: boolean;
  connectionState: WsConnectionState;
  retryCount: number;
  reconnectNow: () => void;
}

/**
 * Resolve the WebSocket endpoint.
 * Priority: NEXT_PUBLIC_WS_URL env → `ws(s)://${hostname}:${port}/ws` derived
 * from the current browser location (S4-WS-03 / Track N1).
 */
export function resolveWsUrl(): string {
  if (typeof window === 'undefined') {
    return '';
  }

  const configured = process.env.NEXT_PUBLIC_WS_URL;
  if (configured) {
    return configured;
  }

  const isSecure = window.location.protocol === 'https:';
  const protocol = isSecure ? 'wss' : 'ws';
  const hostname = window.location.hostname;
  const port = window.location.port || (isSecure ? '443' : '80');

  return `${protocol}://${hostname}:${port}/ws`;
}

/**
 * Exponential backoff delay in milliseconds.
 * delay(n) = min(base * factor^n, max) with base=1s, factor=1.5, max=30s.
 * Monotonically increasing and capped (S4-WS-03 / S4-WS-06).
 */
export function calculateBackoffDelay(retryCount: number): number {
  const raw = WS_RECONNECT_BASE_DELAY_MS * Math.pow(WS_RECONNECT_FACTOR, retryCount);
  return Math.min(raw, WS_RECONNECT_MAX_DELAY_MS);
}

/**
 * Message dispatcher — the single funnel for every WS event into the stores.
 * Only server-authoritative payloads are applied (S4-NOOPT-01).
 *
 * Event map (Sprint 4 contract, backend events.gateway.ts):
 *  - node_telemetry        → useNodeStore.updateNode()
 *  - node_flow             → useNodeStore.updateNode() (metrics) + applyFlowConfirmed()
 *  - pump_command_update   → useNodeStore.updateOutcome()
 *  - staleness_alert       → useNodeStore.updateNode({ isStale: true })
 *  - device_status         → useDeviceStore.setDeviceStatus()
 */
export function wsMessageHandler(msg: WebSocketBroadcastMessage): void {
  const { event, data } = msg;

  switch (event) {
    case WS_EVENTS.NODE_TELEMETRY: {
      if (!data || typeof data.nodeId !== 'number') {
        console.warn('[WS] Ignoring node_telemetry without a valid nodeId');
        return;
      }
      useNodeStore.getState().updateNode(data.nodeId, {
        healthStatus: data.health || 'OK',
        lastSeenAt: data.lastSeenAt ?? null,
        scheduleState: data.scheduleState || 'IDLE',
        overrideState: data.overrideState || 'NONE',
        sensorSerial: data.sensorSerial ?? null,
        isStale: Boolean(data.isStale),
      });
      break;
    }

    case WS_EVENTS.NODE_FLOW: {
      if (!data || typeof data.nodeId !== 'number') {
        console.warn('[WS] Ignoring node_flow without a valid nodeId');
        return;
      }
      // Flow metrics are always applied.
      useNodeStore.getState().updateNode(data.nodeId, {
        flowLpm: typeof data.flowRateLpm === 'number' ? data.flowRateLpm : 0,
        litresTotal: typeof data.litresTotal === 'number' ? data.litresTotal : 0,
      });

      // S4-WS-02: flowConfirmed only flips true through applyFlowConfirmed()
      // when the server authoritatively confirms flow.
      if (data.flowConfirmed === true) {
        useNodeStore
          .getState()
          .applyFlowConfirmed(data.nodeId, true, data.flowRateLpm, data.time ?? null);
      } else {
        // Server says flow is no longer confirmed (e.g. pump OFF).
        useNodeStore.getState().applyFlowConfirmed(data.nodeId, false);
      }
      break;
    }

    case WS_EVENTS.PUMP_COMMAND_UPDATE: {
      if (!data || typeof data.nodeId !== 'number' || !data.outcome) {
        console.warn('[WS] Ignoring pump_command_update without nodeId/outcome');
        return;
      }
      useNodeStore.getState().updateOutcome(data.nodeId, data.outcome);
      break;
    }

    case WS_EVENTS.STALENESS_ALERT: {
      if (!data || typeof data.nodeId !== 'number') {
        console.warn('[WS] Ignoring staleness_alert without a valid nodeId');
        return;
      }
      useNodeStore.getState().updateNode(data.nodeId, {
        isStale: true,
        lastSeenAt: data.lastSeenAt ?? null,
        staleForMs: typeof data.staleForMs === 'number' ? data.staleForMs : 0,
      });
      break;
    }

    case WS_EVENTS.DEVICE_STATUS: {
      useDeviceStore.getState().setDeviceStatus(data ?? {});
      break;
    }

    case WS_EVENTS.CONNECTED:
    case WS_EVENTS.GROUP_STATUS:
      // Informational events — no store mutation required.
      break;

    default:
      console.warn(`[WS] Unknown event type: ${event}`);
      break;
  }
}

/**
 * useWebSocket — establishes a real Native WebSocket on mount and keeps it
 * alive with bounded exponential-backoff reconnect.
 *
 * Implements Finding #8 fix: the reconnect scheduler always reads the retry
 * counter from a ref (never a stale render closure), so the delay grows
 * monotonically instead of being stuck at 1s.
 */
export function useWebSocket(): UseWebSocketReturn {
  const wsRef = useRef<WebSocket | null>(null);
  const retryCountRef = useRef(0);
  const retryTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const mountedRef = useRef(true);

  const [connectionState, setConnectionState] =
    useState<WsConnectionState>('disconnected');
  const [retryCount, setRetryCount] = useState(0);

  // Refs hold the latest implementations so callbacks never capture stale
  // render closures (S4-WS-03 / Finding #8).
  const connectRef = useRef<() => void>(() => {});
  const scheduleReconnectRef = useRef<() => void>(() => {});
  const cleanupRetryTimerRef = useRef<() => void>(() => {});

  cleanupRetryTimerRef.current = () => {
    if (retryTimerRef.current !== null) {
      clearTimeout(retryTimerRef.current);
      retryTimerRef.current = null;
    }
  };

  scheduleReconnectRef.current = () => {
    if (!mountedRef.current || retryTimerRef.current !== null) {
      // Max 1 pending retry timer at any point in time (S4-WS-03).
      return;
    }

    const delay = calculateBackoffDelay(retryCountRef.current);
    retryCountRef.current += 1;
    setRetryCount(retryCountRef.current);
    setConnectionState('reconnecting');

    retryTimerRef.current = setTimeout(() => {
      retryTimerRef.current = null;
      if (mountedRef.current) {
        connectRef.current();
      }
    }, delay);
  };

  connectRef.current = () => {
    if (!mountedRef.current) {
      return;
    }

    // Clean up any stale socket before opening a new one.
    if (wsRef.current) {
      try {
        wsRef.current.onclose = null;
        wsRef.current.close();
      } catch {
        // Ignore close errors on already-closed sockets.
      }
      wsRef.current = null;
    }

    const url = resolveWsUrl();
    if (!url) {
      setConnectionState('disconnected');
      return;
    }

    try {
      setConnectionState('connecting');
      const ws = new WebSocket(url);
      wsRef.current = ws;

      ws.onopen = () => {
        if (!mountedRef.current) {
          return;
        }
        retryCountRef.current = 0;
        setRetryCount(0);
        setConnectionState('connected');
      };

      ws.onmessage = (event: MessageEvent) => {
        if (!mountedRef.current) {
          return;
        }
        try {
          const raw =
            typeof event.data === 'string'
              ? event.data
              : (event.data as Blob | ArrayBuffer | ArrayBufferView).toString();
          const message = JSON.parse(raw) as WebSocketBroadcastMessage;
          // S4-NOOPT-01: dispatch only server events into the stores.
          wsMessageHandler(message);
        } catch (err) {
          console.warn('[WS] Failed to parse incoming message', err);
        }
      };

      ws.onerror = () => {
        // `onerror` is always followed by `onclose`; reconnection is
        // scheduled there to avoid duplicate timers.
      };

      ws.onclose = () => {
        if (!mountedRef.current) {
          return;
        }
        wsRef.current = null;
        setConnectionState('disconnected');
        // S4-WS-03: never call connect() directly — schedule bounded retry.
        scheduleReconnectRef.current();
      };
    } catch {
      setConnectionState('disconnected');
      scheduleReconnectRef.current();
    }
  };

  useEffect(() => {
    mountedRef.current = true;
    connectRef.current();

    return () => {
      mountedRef.current = false;
      cleanupRetryTimerRef.current();
      if (wsRef.current) {
        try {
          wsRef.current.onclose = null;
          wsRef.current.close();
        } catch {
          // Ignore close errors during unmount.
        }
        wsRef.current = null;
      }
    };
  }, []);

  const reconnectNow = useCallback(() => {
    retryCountRef.current = 0;
    setRetryCount(0);
    cleanupRetryTimerRef.current();

    if (wsRef.current) {
      try {
        wsRef.current.onclose = null;
        wsRef.current.close();
      } catch {
        // Ignore close errors on manual reconnect.
      }
      wsRef.current = null;
    }

    setConnectionState('disconnected');
    connectRef.current();
  }, []);

  return {
    isConnected: connectionState === 'connected',
    connectionState,
    retryCount,
    reconnectNow,
  };
}