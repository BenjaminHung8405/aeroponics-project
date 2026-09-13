'use client';

import React, { useEffect, useState } from 'react';
import { useWebSocket, calculateBackoffDelay } from '../../hooks/useWebSocket';
import { AlertTriangle, RefreshCw } from 'lucide-react';

/**
 * WsBanner Component
 * Hard Rule S4-BANNER-19:
 *  - WS disconnect banner with bg-danger styling.
 *  - Auto-hides when isConnected is true.
 *  - Displays exponential backoff countdown / retry info (capped at 30s).
 *  - Provides manual "Kết nối lại ngay" action button (min-h-[44px], active:scale-95).
 * Hard Rule S4-DS-ICON-14: Zero emoji, uses Lucide SVG icons.
 */
export function WsBanner() {
  const { isConnected, connectionState, retryCount, reconnectNow } = useWebSocket();
  const [secondsRemaining, setSecondsRemaining] = useState<number>(0);

  useEffect(() => {
    if (isConnected) {
      setSecondsRemaining(0);
      return;
    }

    const backoffMs = calculateBackoffDelay(retryCount);
    let remaining = Math.max(1, Math.ceil(backoffMs / 1000));
    setSecondsRemaining(remaining);

    const timer = setInterval(() => {
      remaining -= 1;
      setSecondsRemaining((prev) => Math.max(0, prev - 1));
      if (remaining <= 0) {
        clearInterval(timer);
      }
    }, 1000);

    return () => clearInterval(timer);
  }, [isConnected, retryCount]);

  // Auto-hide when fully connected
  if (isConnected) {
    return null;
  }

  const isReconnecting = connectionState === 'reconnecting';

  return (
    <div
      role="alert"
      aria-live="assertive"
      className="sticky top-0 z-50 w-full bg-danger text-text px-4 py-2.5 shadow-lg border-b border-danger/40 backdrop-blur-md"
    >
      <div className="max-w-7xl mx-auto flex flex-col sm:flex-row items-center justify-between gap-2 text-sm font-medium">
        <div className="flex items-center gap-2 text-center sm:text-left">
          <AlertTriangle size={18} className="shrink-0 animate-bounce" aria-hidden="true" />
          <span>
            {isReconnecting ? (
              <>
                Mất kết nối máy chủ viễn thám. Đang thử kết nối lại (lần {retryCount + 1})
                {secondsRemaining > 0 ? ` sau ${secondsRemaining}s...` : '...'}
              </>
            ) : (
              <>Đang ngắt kết nối với máy chủ viễn thám.</>
            )}
          </span>
        </div>

        <button
          type="button"
          onClick={reconnectNow}
          className="inline-flex items-center justify-center gap-1.5 px-3 py-1.5 rounded-lg bg-background/40 hover:bg-background/60 active:scale-95 text-text border border-border/40 text-xs font-semibold cursor-pointer transition-all duration-150 min-h-[44px]"
          aria-label="Thử kết nối lại WebSocket ngay lập tức"
        >
          <RefreshCw
            size={14}
            className={isReconnecting ? 'animate-spin' : ''}
            aria-hidden="true"
          />
          <span>Kết nối lại ngay</span>
        </button>
      </div>
    </div>
  );
}
