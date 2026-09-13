'use client';

import React from 'react';
import { useNodes } from '../../hooks/queries/useNodes';
import { NodeCard } from './NodeCard';
import { Droplets } from 'lucide-react';

/**
 * NodeGrid Component
 * Hard Rule S4-D3 & S4-DS-MOBILE-17:
 *  - Responsive grid: 1-col on mobile (375px), 2-col on tablet (640px), 4-col on desktop (1024px+)
 *  - Manages 4 Actuator Nodes (1..4)
 *  - Automatically synchronizes initial server query into useNodeStore
 */
export function NodeGrid() {
  // Syncs initial server query data into useNodeStore
  useNodes();

  return (
    <section aria-labelledby="nodes-heading" className="space-y-3">
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-2">
          <div className="p-1.5 rounded-lg bg-surface/80 border border-border/30 text-text-muted">
            <Droplets size={18} className="text-primary" aria-hidden="true" />
          </div>
          <div>
            <h2 id="nodes-heading" className="text-base sm:text-lg font-bold text-text">
              Trạm Phun Khí Canh (Actuator Nodes 1–4)
            </h2>
            <p className="text-xs text-text-muted">
              Giám sát trạng thái bơm, lưu lượng hồi tiếp tức thời và chuỗi xác nhận an toàn
            </p>
          </div>
        </div>
      </div>

      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
        {[1, 2, 3, 4].map((nodeId) => (
          <NodeCard key={nodeId} nodeId={nodeId} />
        ))}
      </div>
    </section>
  );
}
