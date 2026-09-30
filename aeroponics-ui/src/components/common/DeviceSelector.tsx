'use client';

import React from 'react';
import { useSelectedDevice } from '../../lib/selected-device-context';
import { Cpu, Loader2, ChevronDown } from 'lucide-react';

/**
 * DeviceSelector Component
 * Allows operator to switch between multiple ESP32 Gateways (e.g. Field vs Lab).
 *
 * Follows:
 *  - S4-DS-ICON-14: 100% Lucide SVG components (Zero emoji).
 *  - S4-DS-TOUCH-15: min-h-[44px] touch ergonomics with active:scale-95.
 *  - S4-DS-FONT-12: Font mono for device IDs.
 *  - S4-DS-CONTRAST-16: High contrast bio-glassmorphic styling.
 */
export function DeviceSelector() {
  const {
    devices,
    selectedDeviceId,
    isLoadingDevices,
    selectDevice,
  } = useSelectedDevice();

  if (isLoadingDevices) {
    return (
      <div
        className="inline-flex items-center gap-1.5 px-3 py-2 rounded-lg bg-surface/50 border border-border/30 text-xs font-medium text-text-muted min-h-[44px]"
        role="status"
        aria-label="Đang tải danh sách thiết bị Gateway"
      >
        <Loader2 size={14} className="animate-spin text-primary shrink-0" aria-hidden="true" />
        <span className="hidden sm:inline">Đang tải thiết bị...</span>
      </div>
    );
  }

  if (devices.length === 0) {
    return (
      <div
        className="inline-flex items-center gap-1.5 px-3 py-2 rounded-lg bg-surface/50 border border-border/30 text-xs font-medium text-accent-amber min-h-[44px]"
        role="status"
        aria-label="Không tìm thấy thiết bị Gateway"
      >
        <Cpu size={14} className="text-accent-amber shrink-0" aria-hidden="true" />
        <span>Chưa có thiết bị</span>
      </div>
    );
  }

  return (
    <div className="relative inline-flex items-center" data-testid="device-selector">
      <label
        htmlFor="gateway-device-select"
        className="sr-only"
      >
        Chọn thiết bị ESP32 Gateway
      </label>
      <div className="relative flex items-center">
        <div className="pointer-events-none absolute left-3 flex items-center text-primary">
          <Cpu size={14} aria-hidden="true" />
        </div>
        <select
          id="gateway-device-select"
          aria-label="Chọn thiết bị ESP32 Gateway"
          value={selectedDeviceId ?? ''}
          onChange={(event) => selectDevice(event.target.value)}
          className="min-h-[44px] appearance-none pl-8 pr-8 py-2 rounded-lg border border-border/40 bg-surface/80 text-xs font-semibold text-text shadow-sm hover:border-primary/50 focus:border-primary focus:ring-1 focus:ring-primary outline-none cursor-pointer transition-all duration-150 active:scale-95 disabled:cursor-not-allowed disabled:opacity-60 max-w-[210px] sm:max-w-[240px]"
        >
          {devices.map((device) => {
            const isOnline = device.status === 'online';
            const label = device.displayName
              ? `${device.displayName} (${device.deviceId})`
              : device.deviceId;
            const statusSuffix = isOnline ? '[Online]' : '[Offline]';
            const disabledSuffix = device.enabled === false ? ' - Đã tắt' : '';

            return (
              <option
                key={device.deviceId}
                value={device.deviceId}
                disabled={device.enabled === false}
                className="bg-surface text-text"
              >
                {label} {statusSuffix}{disabledSuffix}
              </option>
            );
          })}
        </select>
        <div className="pointer-events-none absolute right-2.5 flex items-center text-text-muted">
          <ChevronDown size={14} aria-hidden="true" />
        </div>
      </div>
    </div>
  );
}
