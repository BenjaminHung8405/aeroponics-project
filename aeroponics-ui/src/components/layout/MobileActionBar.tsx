'use client';

import React from 'react';
import { useAllNodes } from '../../store/useNodeStore';
import { useAllGroups } from '../../store/useGroupStore';
import { ShieldAlert, Activity, Thermometer, Sun, Moon } from 'lucide-react';

/**
 * MobileActionBar Component
 *
 * Implements MASTER.md §Mobile Touch Ergonomics & dashboard.md §5:
 *  - Fixed bottom sticky bar for mobile viewports (< 768px, hidden on desktop md:hidden).
 *  - High-tech Bio-Glassmorphic background with 20px blur and border.
 *  - Safe-area inset bottom support (iOS home indicator bar).
 *  - Quick system diagnostics: Fault status, Day/Night phase, Quick scroll to sensor telemetry.
 *  - Zero emoji (100% Lucide React SVG).
 *  - Touch targets >= 44px with active:scale-95 tactile micro-interaction.
 */
export function MobileActionBar() {
  const nodes = useAllNodes();
  const groups = useAllGroups();

  // Determine if any node has an active fault
  const hasFault = nodes.some(
    (n) =>
      n.healthStatus === 'FAULT' ||
      n.outcome?.startsWith('FAULT_') ||
      n.outcome === 'TIMEOUT' ||
      n.isStale
  );

  // Determine overall active phase from assigned groups
  const hasNightPhase = groups.some((g) => g.phase === 'NIGHT');

  const scrollToTelemetry = () => {
    const el = document.getElementById('measurement-heading');
    if (el) {
      el.scrollIntoView({ behavior: 'smooth', block: 'start' });
    }
  };

  return (
    <aside
      aria-label="Thanh điều khiển nhanh di động"
      className="md:hidden fixed bottom-0 left-0 right-0 z-40 bg-surface/90 backdrop-blur-20 border-t border-border/40 px-4 py-2 pb-[max(10px,env(safe-area-inset-bottom))] shadow-2xl transition-all duration-200"
    >
      <div className="flex items-center justify-between gap-2 max-w-lg mx-auto">
        {/* 1. Hardware Status Indicator */}
        <div
          className={`flex items-center gap-1.5 px-3 py-2 rounded-xl text-xs font-semibold border min-h-[44px] transition-colors ${
            hasFault
              ? 'bg-danger/15 text-danger border-danger/40 animate-pulse'
              : 'bg-primary/15 text-primary border-primary/30'
          }`}
          role="status"
          aria-live="polite"
        >
          {hasFault ? (
            <>
              <ShieldAlert size={16} className="shrink-0" aria-hidden="true" />
              <span className="truncate">Cảnh báo lỗi</span>
            </>
          ) : (
            <>
              <span className="w-2 h-2 rounded-full bg-primary shrink-0" aria-hidden="true" />
              <span className="truncate">4 Trạm sẵn sàng</span>
            </>
          )}
        </div>

        {/* 2. Day / Night Phase Badge */}
        <div
          className={`flex items-center gap-1.5 px-2.5 py-2 rounded-xl border text-xs font-semibold min-h-[44px] ${
            hasNightPhase
              ? 'bg-accent-indigo/15 text-accent-indigo border-accent-indigo/40'
              : 'bg-accent-amber/15 text-accent-amber border-accent-amber/40'
          }`}
          title={hasNightPhase ? 'Chu kỳ ban đêm đang chạy' : 'Chu kỳ ban ngày đang chạy'}
        >
          {hasNightPhase ? (
            <>
              <Moon size={15} className="shrink-0" aria-hidden="true" />
              <span className="hidden xs:inline text-[11px]">Đêm</span>
            </>
          ) : (
            <>
              <Sun size={15} className="shrink-0" aria-hidden="true" />
              <span className="hidden xs:inline text-[11px]">Ngày</span>
            </>
          )}
        </div>

        {/* 3. Quick Action: Scroll to On-Demand Measurement */}
        <button
          type="button"
          onClick={scrollToTelemetry}
          className="btn-primary flex items-center justify-center gap-2 px-3.5 py-2 rounded-xl bg-primary text-background font-bold text-xs shadow-md shadow-primary/25 min-h-[44px] cursor-pointer active:scale-95 transition-all duration-150"
          aria-label="Cuộn nhanh tới bảng đo đạc cảm biến nước Tuya"
        >
          <Thermometer size={16} aria-hidden="true" />
          <span>Đo nước</span>
        </button>
      </div>
    </aside>
  );
}
