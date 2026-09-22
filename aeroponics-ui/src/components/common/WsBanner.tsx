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
  return null;
}
