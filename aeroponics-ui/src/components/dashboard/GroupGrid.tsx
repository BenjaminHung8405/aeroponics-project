'use client';

import React from 'react';
import { useGroups } from '../../hooks/queries/useGroups';
import { GroupCard } from './GroupCard';
import { Timer } from 'lucide-react';

/**
 * GroupGrid Component
 * Hard Rule S4-D2 & S4-DS-MOBILE-17:
 *  - Responsive grid: 1-col on mobile (375px), 2-col on tablet (640px), 4-col on desktop (1024px+)
 *  - Manages 4 Timer Groups (1..4)
 *  - Automatically synchronizes server state into useGroupStore
 */
export function GroupGrid() {
  // Syncs initial server query data into useGroupStore
  useGroups();

  return (
    <section aria-labelledby="groups-heading" className="space-y-3">
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-2">
          <div className="p-1.5 rounded-lg bg-surface/80 border border-border/30 text-text-muted">
            <Timer size={18} aria-hidden="true" />
          </div>
          <div>
            <h2 id="groups-heading" className="text-base sm:text-lg font-bold text-text">
              Nhóm Điều Khiển Khí Canh (Timer Groups 1–4)
            </h2>
            <p className="text-xs text-text-muted">
              Điều phối chu kỳ Ngày/Đêm độc lập cho từng cụm rễ khí canh
            </p>
          </div>
        </div>
      </div>

      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
        {[1, 2, 3, 4].map((groupId) => (
          <GroupCard key={groupId} groupId={groupId} />
        ))}
      </div>
    </section>
  );
}
