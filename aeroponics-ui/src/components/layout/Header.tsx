'use client';

import React, { useEffect, useState } from 'react';
import { useAuth } from '../../hooks/useAuth';
import { useDeviceStore } from '../../store/useDeviceStore';
import { useDeviceStatus, useSyncDeviceClock } from '../../hooks/queries/useDeviceStatus';
import { Droplets, Clock, Activity, LogOut, Wifi, WifiOff, Radio, RefreshCw, Cpu } from 'lucide-react';
import { DeviceSelector } from '../common/DeviceSelector';
import { useSelectedDevice } from '../../lib/selected-device-context';

import { useToast } from '../common/Toast';

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
  const { toast } = useToast();
  useDeviceStatus(); // trigger query & background sync
  const { selectedDevice, selectedDeviceId } = useSelectedDevice();
  const syncClockMutation = useSyncDeviceClock();
  const storeStatus = useDeviceStore((s) => s.status);
  const storeDeviceId = useDeviceStore((s) => s.deviceId);
  const storeUptime = useDeviceStore((s) => s.uptime_s);
  const storeRssi = useDeviceStore((s) => s.rssi_dbm);
  const storeRtcValid = useDeviceStore((s) => s.rtcValid);
  const storeNtpSynced = useDeviceStore((s) => s.ntpSynced);
  const storeTimeSource = useDeviceStore((s) => s.timeSource);
  const storeLastSync = useDeviceStore((s) => s.lastSyncUnixTimeUtc);

  const gatewayStatus = selectedDevice ? selectedDevice.status : storeStatus;
  const gatewayDeviceId = selectedDeviceId || storeDeviceId;
  const gatewayUptime = selectedDevice?.uptime_s ?? storeUptime;
  const gatewayRssi = selectedDevice?.rssi_dbm !== undefined ? selectedDevice.rssi_dbm : storeRssi;
  const rtcValid = selectedDevice?.rtcValid !== undefined ? selectedDevice.rtcValid : storeRtcValid;
  const ntpSynced = selectedDevice?.ntpSynced !== undefined ? selectedDevice.ntpSynced : storeNtpSynced;
  const timeSource = selectedDevice?.timeSource !== undefined ? selectedDevice.timeSource : storeTimeSource;
  const lastSyncUnixTimeUtc = selectedDevice?.lastSyncUnixTimeUtc !== undefined ? selectedDevice.lastSyncUnixTimeUtc : storeLastSync;
  const rtcKnown = Boolean(selectedDevice && gatewayStatus !== 'offline');
  const [timeString, setTimeString] = useState<string>('--:--:-- ICT');

  const handleSyncClock = async () => {
    if (!gatewayDeviceId || syncClockMutation.isPending) return;
    try {
      await syncClockMutation.mutateAsync(gatewayDeviceId);
      toast.success(
        `Đã gửi lệnh đồng bộ giờ tới Gateway ${gatewayDeviceId}`,
        'Đồng bộ RTC thành công',
      );
    } catch {
      toast.error(
        `Không thể gửi lệnh đồng bộ giờ tới Gateway ${gatewayDeviceId}`,
        'Lỗi đồng bộ RTC',
      );
    }
  };

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
          {/* Gateway Device Selector */}
          <DeviceSelector />

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

          {/* RTC Hardware Status Badge */}
          <div
            className={`inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg border text-xs font-medium transition-colors ${
              rtcKnown && rtcValid
                ? 'bg-primary/15 text-primary border-primary/40'
                : 'bg-danger/15 text-danger border-danger/40 animate-pulse'
            }`}
            title={`Trạng thái module RTC phần cứng (DS1307): ${
              rtcKnown && rtcValid
                ? `Hoạt động chuẩn (Nguồn: ${timeSource || (ntpSynced ? 'SYSTEM_NTP' : 'DS1307_RTC')}${
                    lastSyncUnixTimeUtc
                      ? `, Đồng bộ lúc: ${new Date(Number(lastSyncUnixTimeUtc) * 1000).toLocaleTimeString('vi-VN')}`
                      : ''
                  })`
                : 'Cảnh báo: RTC chưa được đồng bộ hoặc lỗi nguồn pin CMOS (Hệ thống có thể dừng tưới an toàn)'
            }`}
          >
            <Cpu size={14} className={`shrink-0 ${!rtcKnown ? 'text-text-muted' : rtcValid ? 'text-primary' : 'text-danger'}`} aria-hidden="true" />
            <span className="w-2 h-2 rounded-full shrink-0">
              <span
                className={`block w-2 h-2 rounded-full ${
                  !rtcKnown ? 'bg-text-muted' : rtcValid ? 'bg-primary' : 'bg-danger'
                }`}
              />
            </span>
            <span>
              RTC:{' '}
              <strong className="font-semibold">
                {!rtcKnown ? 'Chưa xác định' : rtcValid ? 'Chuẩn' : 'Lỗi/Mất nguồn'}
              </strong>
            </span>
          </div>

          {/* Manual Clock Sync Button */}
          <button
            type="button"
            onClick={handleSyncClock}
            disabled={syncClockMutation.isPending || gatewayStatus === 'offline'}
            className="inline-flex items-center justify-center gap-1.5 px-3 py-2 rounded-lg bg-surface/50 hover:bg-primary/20 hover:text-primary hover:border-primary/40 active:scale-95 border border-border/30 text-xs font-semibold text-text-muted cursor-pointer transition-all duration-150 min-h-[44px] disabled:opacity-40 disabled:cursor-not-allowed"
            title={
              gatewayStatus === 'offline'
                ? 'Gateway đang offline, không thể gửi lệnh đồng bộ'
                : 'Đồng bộ tức thời giờ chuẩn Backend xuống module RTC phần cứng của Gateway'
            }
            aria-label="Đồng bộ RTC phần cứng"
          >
            <RefreshCw
              size={14}
              className={`shrink-0 ${syncClockMutation.isPending ? 'animate-spin text-primary' : ''}`}
              aria-hidden="true"
            />
            <span className="hidden sm:inline">
              {syncClockMutation.isPending ? 'Đang đồng bộ...' : 'Đồng bộ RTC'}
            </span>
          </button>

          {/* Live ICT Clock */}
          <div
            className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-surface/80 border border-border/40 text-xs font-medium text-text"
            title="Giờ thực tế trạm nông nghiệp (Asia/Ho_Chi_Minh)"
          >
            <Clock size={14} className="text-text-muted shrink-0" aria-hidden="true" />
            <span className="font-mono tabular-nums text-text-muted">{timeString}</span>
          </div>

          {/* System Sync Status Badge */}
          <div
            className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg border text-xs font-medium transition-colors bg-primary/15 text-primary border-primary/40"
            title="Đồng bộ viễn thám tự động qua REST & MQTT Broker"
          >
            <Activity size={14} className="shrink-0 text-primary animate-pulse" aria-hidden="true" />
            <span className="w-2 h-2 rounded-full shrink-0">
              <span className="block w-2 h-2 rounded-full bg-primary animate-pulse" />
            </span>
            <span>Đồng bộ: Tự động</span>
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
