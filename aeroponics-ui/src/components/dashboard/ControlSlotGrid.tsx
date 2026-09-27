'use client';

import React from 'react';
import { useControlSlots } from '../../hooks/queries/useControlSlots';
import { useGroups } from '../../hooks/queries/useGroups';
import { useNodes } from '../../hooks/queries/useNodes';
import { ControlSlotCard } from './ControlSlotCard';
import { Sliders } from 'lucide-react';

export function ControlSlotGrid() {
  useGroups();
  useNodes();
  const { data: slots = [], isLoading } = useControlSlots();
  return (
    <section aria-labelledby="control-slots-heading" className="space-y-3">
      <div className="flex items-center gap-2">
        <div className="p-1.5 rounded-lg bg-surface/80 border border-border/30 text-text-muted">
          <Sliders size={18} aria-hidden="true" />
        </div>
        <div>
          <h2 id="control-slots-heading" className="text-base sm:text-lg font-bold text-text">Khe điều khiển động</h2>
          <p className="text-xs text-text-muted">Gán từng khe cho Node 01–15 hoặc Nhóm 1–4</p>
        </div>
      </div>
      {isLoading ? <p className="text-sm text-text-muted">Đang tải cấu hình khe...</p> : (
        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
          {[1, 2, 3, 4].map((slotIndex) => <ControlSlotCard key={slotIndex} slotIndex={slotIndex} slots={slots} />)}
        </div>
      )}
    </section>
  );
}
