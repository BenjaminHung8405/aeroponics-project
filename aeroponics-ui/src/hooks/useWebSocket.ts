'use client';

/**
 * Aeroponics Architecture Refactor:
 * WebSocket has been replaced by MQTT + REST Polling (matching mushroom-cp architecture).
 * This hook is retained as a zero-overhead compatibility stub.
 */

export type WsConnectionState = 'connecting' | 'connected' | 'disconnected' | 'reconnecting';

export interface UseWebSocketReturn {
  isConnected: boolean;
  connectionState: WsConnectionState;
  retryCount: number;
  reconnectNow: () => void;
}

export function calculateBackoffDelay(_retryCount: number): number {
  return 0;
}

export function resolveWsUrl(): string {
  return '';
}

export function useWebSocket(): UseWebSocketReturn {
  return {
    isConnected: true,
    connectionState: 'connected',
    retryCount: 0,
    reconnectNow: () => {},
  };
}
