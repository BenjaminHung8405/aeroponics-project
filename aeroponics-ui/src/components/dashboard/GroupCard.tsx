'use client';

import React, { useState, useEffect } from 'react';
import { useGroup } from '../../store/useGroupStore';
import { AssignGroupModal } from './AssignGroupModal';
import { Sun, Moon, Clock, Sliders, CheckCircle2 } from 'lucide-react';

interface GroupCardProps {
  groupId: number;
}

/**
 * Formats seconds into HH:mm:ss
 */
function formatCountdown(seconds: number): string {
  if (seconds <= 0) return '00:00:00';
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  return `${h.toString().padStart(2, '0')}:${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`;
}

/**
 * GroupCard Component
 * Follows:
 *  - S4-D2: Phase indicator with Sun (DAY) / Moon (NIGHT) (Zero emoji, Lucide only)
 *  - Countdown timer with font-mono tabular-nums text-2xl font-bold
 *  - Client-side 1s interval ticker derived from nextTransitionAt, cleaned up safely
 *  - Granular Zustand subscription via useGroup(groupId) to isolate re-renders
 *  - Touch targets >= 44px, active:scale-95
 */
export function GroupCard({ groupId }: GroupCardProps) {
  const group = useGroup(groupId);
  const [countdown, setCountdown] = useState<string>('00:00:00');
  const [isAssignModalOpen, setIsAssignModalOpen] = useState(false);

  // Client-side 1s ticker for phase transition countdown
  useEffect(() => {
    if (!group.nextTransitionAt) {
      setCountdown('00:00:00');
      return;
    }

    const updateCountdown = () => {
      const targetTime = new Date(group.nextTransitionAt!).getTime();
      const now = Date.now();
      const diffSeconds = Math.max(0, Math.floor((targetTime - now) / 1000));
      setCountdown(formatCountdown(diffSeconds));
    };

    updateCountdown();
    const interval = setInterval(updateCountdown, 1000);

    return () => clearInterval(interval);
  }, [group.nextTransitionAt]);

  const isActive = group.status === 'ACTIVE';

  return (
    <>
      <div className="glass-card p-4 sm:p-5 flex flex-col justify-between h-full min-h-[220px] space-y-4">
        {/* Card Header: Group Name & Status Badge */}
        <div>
          <div className="flex items-center justify-between gap-2 mb-2">
            <h3 className="text-base sm:text-lg font-bold text-text">
              {group.name || `Nhóm #${groupId}`}
            </h3>

            <span
              className={`inline-flex items-center gap-1 px-2.5 py-0.5 rounded-full text-xs font-semibold border ${
                isActive
                  ? 'bg-primary/15 text-primary border-primary/40'
                  : 'bg-surface/60 text-text-subtle border-border/30'
              }`}
            >
              {isActive && <CheckCircle2 size={11} aria-hidden="true" />}
              <span>{isActive ? 'ĐANG CHẠY' : 'CHƯA GÁN'}</span>
            </span>
          </div>

          {/* Phase Badge (Sun / Moon - Lucide SVG Zero Emoji) */}
          <div className="flex items-center gap-2">
            {group.phase === 'DAY' ? (
              <span className="inline-flex items-center gap-1.5 px-2.5 py-1 rounded-lg text-xs font-semibold bg-accent-amber/15 text-accent-amber border border-accent-amber/40">
                <Sun size={14} className="shrink-0" aria-hidden="true" />
                <span>Pha Ngày (Daylight)</span>
              </span>
            ) : group.phase === 'NIGHT' ? (
              <span className="inline-flex items-center gap-1.5 px-2.5 py-1 rounded-lg text-xs font-semibold bg-accent-indigo/15 text-accent-indigo border border-accent-indigo/40">
                <Moon size={14} className="shrink-0" aria-hidden="true" />
                <span>Pha Đêm (Darkness)</span>
              </span>
            ) : (
              <span className="inline-flex items-center gap-1.5 px-2.5 py-1 rounded-lg text-xs font-medium bg-surface/50 text-text-subtle border border-border/30">
                <Clock size={14} className="shrink-0" aria-hidden="true" />
                <span>Chưa kích hoạt chu kỳ</span>
              </span>
            )}
          </div>
        </div>

        {/* Center: Countdown Timer */}
        <div className="py-2 border-y border-border/20 text-center">
          <span className="block text-xs font-medium text-text-muted mb-0.5 uppercase tracking-wider">
            Thời gian chuyển pha
          </span>
          <div className="font-mono tabular-nums text-2xl sm:text-3xl font-bold tracking-tight text-text">
            {isActive ? countdown : '—'}
          </div>
        </div>

        {/* Treatment Info & Assigned Nodes */}
        <div className="space-y-2 text-xs">
          <div>
            <span className="text-text-subtle block mb-0.5">Công thức khí canh:</span>
            <p className="font-semibold text-text truncate">
              {group.treatment ? (
                <span>
                  {group.treatment.treatment_name} (v{group.treatment.version_num})
                </span>
              ) : (
                <span className="text-text-subtle italic">Chưa có công thức</span>
              )}
            </p>
          </div>

          {/* Spray / Cooldown Parameters */}
          {group.treatment && (
            <div className="space-y-1.5">
              {/* Day Phase Row */}
              <div className={`flex items-center justify-between px-2.5 py-1.5 rounded-lg border text-[11px] font-mono tabular-nums transition-all duration-300 ${
                group.phase === 'DAY'
                  ? 'bg-accent-amber/20 border-accent-amber/50 ring-1 ring-accent-amber/30'
                  : 'bg-background/50 border-border/20 opacity-60'
              }`}>
                <span className="font-sans flex items-center gap-1 font-semibold text-accent-amber">
                  <Sun size={11} aria-hidden="true" />
                  {group.phase === 'DAY' && <span className="text-[10px] uppercase tracking-wider">LIVE</span>}
                  <span>Ngày</span>
                </span>
                <span className="text-text">
                  {group.treatment.spray_day_s}s phun / {group.treatment.cooldown_day_s}s nghỉ
                </span>
              </div>
              {/* Night Phase Row */}
              <div className={`flex items-center justify-between px-2.5 py-1.5 rounded-lg border text-[11px] font-mono tabular-nums transition-all duration-300 ${
                group.phase === 'NIGHT'
                  ? 'bg-accent-indigo/20 border-accent-indigo/50 ring-1 ring-accent-indigo/30'
                  : 'bg-background/50 border-border/20 opacity-60'
              }`}>
                <span className="font-sans flex items-center gap-1 font-semibold text-accent-indigo">
                  <Moon size={11} aria-hidden="true" />
                  {group.phase === 'NIGHT' && <span className="text-[10px] uppercase tracking-wider">LIVE</span>}
                  <span>Đêm</span>
                </span>
                <span className="text-text">
                  {group.treatment.spray_night_s}s phun / {group.treatment.cooldown_night_s}s nghỉ
                </span>
              </div>
            </div>
          )}

          {/* Assigned Nodes Badge List */}
          <div>
            <span className="text-text-subtle block mb-1">Trạm phụ trách:</span>
            <div className="flex flex-wrap gap-1">
              {group.nodeIds && group.nodeIds.length > 0 ? (
                group.nodeIds.map((nodeId) => (
                  <span
                    key={nodeId}
                    className="inline-flex items-center px-2 py-0.5 rounded-md bg-surface border border-border/40 text-[11px] font-medium text-text-muted"
                  >
                    Trạm #{nodeId}
                  </span>
                ))
              ) : (
                <span className="text-text-subtle text-[11px] italic">0 trạm</span>
              )}
            </div>
          </div>
        </div>

        {/* Action Button */}
        <button
          type="button"
          onClick={() => setIsAssignModalOpen(true)}
          className="btn-secondary w-full inline-flex items-center justify-center gap-1.5 px-3 py-2 rounded-xl bg-surface/80 hover:bg-surface border border-border/40 text-xs font-semibold text-text active:scale-95 cursor-pointer transition-all duration-150 min-h-[44px]"
          aria-label={`Cấu hình nhóm ${group.name || groupId}`}
        >
          <Sliders size={14} aria-hidden="true" />
          <span>{isActive ? 'Thay đổi cấu hình' : 'Gán công thức'}</span>
        </button>
      </div>

      <AssignGroupModal
        group={group}
        isOpen={isAssignModalOpen}
        onClose={() => setIsAssignModalOpen(false)}
      />
    </>
  );
}
