'use client';

import React from 'react';
import { useDeviceStore } from '../../store/useDeviceStore';
import { useDeviceStatus } from '../../hooks/queries/useDeviceStatus';
import { WifiOff, AlertTriangle, RefreshCw } from 'lucide-react';

/**
 * GatewayBanner Component
 * Displays a prominent alert when the ESP32 Gateway is offline/disconnected.
 *
 * Rules:
 *  - High visibility alert banner with danger styling.
 *  - Auto-hides when status === 'online'.
 *  - Explains operational impact: local RTC autonomous schedule runs, remote commands paused.
 *  - Provides manual recheck action button.
 */
export function GatewayBanner() {
  // Ensure query is mounted so it polls / refetches status
  const { refetch, isFetching } = useDeviceStatus();
  const status = useDeviceStore((s) => s.status);
  const deviceId = useDeviceStore((s) => s.deviceId);
  const lastSeenAt = useDeviceStore((s) => s.lastSeenAt);
  const reason = useDeviceStore((s) => s.reason);

  // Auto-hide when online or still initializing
  if (status !== 'offline') {
    return null;
  }

  const formatLastSeen = (isoString?: string | null) => {
    if (!isoString) return 'Chưa có dữ liệu gần đây';
    try {
      const d = new Date(isoString);
      return new Intl.DateTimeFormat('vi-VN', {
        timeZone: 'Asia/Ho_Chi_Minh',
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
        day: '2-digit',
        month: '2-digit',
      }).format(d);
    } catch {
      return isoString;
    }
  };

  return (
    <div
      role="alert"
      aria-live="assertive"
      className="w-full bg-danger/15 border border-danger/40 text-danger rounded-xl p-3.5 mb-4 shadow-md backdrop-blur-md transition-all duration-200"
    >
      <div className="flex flex-col sm:flex-row items-start sm:items-center justify-between gap-3">
        <div className="flex items-start gap-2.5">
          <div className="p-1.5 rounded-lg bg-danger/20 border border-danger/30 shrink-0 mt-0.5 sm:mt-0">
            <WifiOff size={18} className="text-danger animate-pulse" aria-hidden="true" />
          </div>
          <div className="text-xs sm:text-sm">
            <div className="flex items-center gap-2 font-bold tracking-tight">
              <span>CẢNH BÁO: Gateway ESP32 ({deviceId}) đang MẤT KẾT NỐI</span>
            </div>
            <p className="text-text-muted mt-0.5 text-xs leading-relaxed">
              Tín hiệu nhận lần cuối: <strong className="text-text font-mono">{formatLastSeen(lastSeenAt)}</strong>.
              {reason === 'HEARTBEAT_TIMEOUT' && ' (Quá thời gian chờ heartbeat 30s)'}
            </p>
            <p className="text-text-subtle text-[11px] mt-1">
              Hệ thống tưới tự động cục bộ vẫn hoạt động theo lịch RTC phần cứng, nhưng lệnh điều khiển từ xa và telemetry mới tạm ngưng.
            </p>
          </div>
        </div>

        <button
          type="button"
          onClick={() => refetch()}
          disabled={isFetching}
          className="inline-flex items-center justify-center gap-1.5 px-3 py-1.5 rounded-lg bg-danger/20 hover:bg-danger/30 active:scale-95 text-danger border border-danger/40 text-xs font-semibold cursor-pointer transition-all duration-150 shrink-0 min-h-[36px] self-end sm:self-center disabled:opacity-50"
          aria-label="Kiểm tra lại trạng thái kết nối ESP32 Gateway"
        >
          <RefreshCw
            size={13}
            className={isFetching ? 'animate-spin' : ''}
            aria-hidden="true"
          />
          <span>{isFetching ? 'Đang kiểm tra...' : 'Kiểm tra lại'}</span>
        </button>
      </div>
    </div>
  );
}
