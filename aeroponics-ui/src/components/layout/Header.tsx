'use client';

import React, { useEffect, useState } from 'react';
import { useAuth } from '../../hooks/useAuth';
import { useWebSocket } from '../../hooks/useWebSocket';
import { useDeviceStore } from '../../store/useDeviceStore';
import { useDeviceStatus } from '../../hooks/queries/useDeviceStatus';
import { Droplets, Clock, Activity, LogOut, Wifi, WifiOff, Radio } from 'lucide-react';

/**
 * Header Component
 * Follows:
 *  - MASTER.md: Bio-glassmorphic header, high contrast text, Outfit font.
 *  - S4-DS-FONT-12: Real-time ICT clock with font-mono tabular-nums.
 *  - S4-DS-TOUCH-15: Logout button with min-h-[44px], active:scale-95.
 *  - S4-DS-ICON-14: 100% Lucide SVG components (Zero emoji).
 */
export function Header() {
  const { logout } = useAuth();
  const { isConnected } = useWebSocket();
  useDeviceStatus(); // trigger query & background sync
  const gatewayStatus = useDeviceStore((s) => s.status);
  const gatewayDeviceId = useDeviceStore((s) => s.deviceId);
  const gatewayUptime = useDeviceStore((s) => s.uptime_s);
  const gatewayRssi = useDeviceStore((s) => s.rssi_dbm);
  const [timeString, setTimeString] = useState<string>('--:--:-- ICT');

  useEffect(() => {
    const updateTime = () => {
      try {
        const now = new Date();
        const formatter = new Intl.DateTimeFormat('vi-VN', {
          timeZone: 'Asia/Ho_Chi_Minh',
          hour: '2-digit',
          minute: '2-digit',
          second: '2-digit',
          hour12: false,
        });
        setTimeString(`${formatter.format(now)} ICT`);
      } catch {
        const now = new Date();
        setTimeString(`${now.toTimeString().split(' ')[0]} ICT`);
      }
    };

    updateTime();
    const interval = setInterval(updateTime, 1000);
    return () => clearInterval(interval);
  }, []);

  return (
    <header className="glass-card mb-6 px-4 py-3 sm:px-6">
      <div className="flex flex-col md:flex-row items-center justify-between gap-4">
        {/* Brand identity */}
        <div className="flex items-center gap-3 w-full md:w-auto">
          <div className="w-11 h-11 rounded-xl bg-primary/15 border border-primary/30 flex items-center justify-center shrink-0">
            <Droplets className="w-6 h-6 text-primary" aria-hidden="true" />
          </div>
          <div>
            <h1 className="text-xl sm:text-2xl font-bold tracking-tight text-text leading-tight">
              Aeroponics Smart Farm
            </h1>
            <p className="text-xs text-text-muted">
              Hệ thống viễn thám &amp; điều khiển khí canh đa trạm
            </p>
          </div>
        </div>

        {/* Telemetry badges & actions */}
        <div className="flex flex-wrap items-center justify-end gap-2 sm:gap-3 w-full md:w-auto">
          {/* Gateway Status Badge */}
          <div
            className={`inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg border text-xs font-medium transition-colors ${
              gatewayStatus === 'online'
                ? 'bg-primary/15 text-primary border-primary/40'
                : gatewayStatus === 'offline'
                  ? 'bg-danger/15 text-danger border-danger/40'
                  : 'bg-surface/80 text-text-muted border-border/40'
            }`}
            title={`Gateway ${gatewayDeviceId}: ${
              gatewayStatus === 'online'
                ? `Online (Uptime: ${gatewayUptime}s, RSSI: ${gatewayRssi !== null ? `${gatewayRssi} dBm` : 'N/A'})`
                : gatewayStatus === 'offline'
                  ? 'Offline (Mất kết nối với Gateway ESP32)'
                  : 'Đang kiểm tra kết nối...'
            }`}
          >
            {gatewayStatus === 'online' ? (
              <Wifi size={14} className="shrink-0 text-primary" aria-hidden="true" />
            ) : gatewayStatus === 'offline' ? (
              <WifiOff size={14} className="shrink-0 text-danger" aria-hidden="true" />
            ) : (
              <Radio size={14} className="shrink-0 text-text-muted animate-pulse" aria-hidden="true" />
            )}
            <span className="w-2 h-2 rounded-full shrink-0">
              <span
                className={`block w-2 h-2 rounded-full ${
                  gatewayStatus === 'online'
                    ? 'bg-primary animate-pulse'
                    : gatewayStatus === 'offline'
                      ? 'bg-danger'
                      : 'bg-text-muted'
                }`}
              />
            </span>
            <span>
              Gateway:{' '}
              <strong
                className={`font-semibold ${
                  gatewayStatus === 'online'
                    ? 'text-primary'
                    : gatewayStatus === 'offline'
                      ? 'text-danger'
                      : 'text-text-muted'
                }`}
              >
                {gatewayStatus === 'online'
                  ? 'Online'
                  : gatewayStatus === 'offline'
                    ? 'Offline'
                    : 'Đang kết nối...'}
              </strong>
            </span>
          </div>

          {/* Live ICT Clock */}
          <div
            className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-surface/80 border border-border/40 text-xs font-medium text-text"
            title="Giờ thực tế trạm nông nghiệp (Asia/Ho_Chi_Minh)"
          >
            <Clock size={14} className="text-text-muted shrink-0" aria-hidden="true" />
            <span className="font-mono tabular-nums text-text-muted">{timeString}</span>
          </div>

          {/* WebSocket Status Badge */}
          <div
            className={`inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg border text-xs font-medium transition-colors ${
              isConnected
                ? 'bg-primary/15 text-primary border-primary/40'
                : 'bg-danger/15 text-danger border-danger/40'
            }`}
          >
            <Activity size={14} className="shrink-0" aria-hidden="true" />
            <span className="w-2 h-2 rounded-full shrink-0">
              <span
                className={`block w-2 h-2 rounded-full ${
                  isConnected ? 'bg-primary animate-pulse' : 'bg-danger'
                }`}
              />
            </span>
            <span>{isConnected ? 'WS Đã kết nối' : 'WS Ngắt kết nối'}</span>
          </div>

          {/* Logout button */}
          <button
            type="button"
            onClick={logout}
            className="inline-flex items-center justify-center gap-1.5 px-3 py-2 rounded-lg bg-surface/50 hover:bg-danger/20 hover:text-danger hover:border-danger/40 active:scale-95 border border-border/30 text-xs font-semibold text-text-muted cursor-pointer transition-all duration-150 min-h-[44px]"
            title="Đăng xuất khỏi hệ thống"
            aria-label="Đăng xuất khỏi hệ thống"
          >
            <LogOut size={16} aria-hidden="true" />
            <span className="hidden sm:inline">Đăng xuất</span>
          </button>
        </div>
      </div>
    </header>
  );
}
