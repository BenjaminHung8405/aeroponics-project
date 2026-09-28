'use client';

import React, { useState } from 'react';
import { useNodes } from '../../hooks/queries/useNodes';
import { NodeCard } from './NodeCard';
import { RfDiscoveryModal } from './RfDiscoveryModal';
import { Droplets, Radio } from 'lucide-react';

/**
 * NodeGrid Component
 * Hard Rule S4-D3 & S4-DS-MOBILE-17:
 *  - Responsive grid: 1-col on mobile (375px), 2-col on tablet (640px), 4-col on desktop (1024px+)
 *  - Manages physical AGU RF Nodes 4..7
 *  - Automatically synchronizes initial server query into useNodeStore
 *  - RF Discovery & Node Commissioning integration
 */
export function NodeGrid() {
  // Syncs initial server query data into useNodeStore
  const { data: serverNodes } = useNodes();
  const [isRfModalOpen, setIsRfModalOpen] = useState(false);

  // Render all registered nodes dynamically (e.g. 4..8), fallback to [4, 5, 6, 7] if server list empty
  const nodeIds = React.useMemo(() => {
    if (serverNodes && serverNodes.length > 0) {
      const ids = serverNodes.map((n) => n.node_id);
      return Array.from(new Set(ids)).sort((a, b) => a - b);
    }
    return [4, 5, 6, 7];
  }, [serverNodes]);

  return (
    <section aria-labelledby="nodes-heading" className="space-y-3">
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-3">
        <div className="flex items-center gap-2">
          <div className="p-1.5 rounded-lg bg-surface/80 border border-border/30 text-text-muted">
            <Droplets size={18} className="text-primary" aria-hidden="true" />
          </div>
          <div>
            <h2 id="nodes-heading" className="text-base sm:text-lg font-bold text-text">
              Trạm Phun Khí Canh (RF Nodes)
            </h2>
            <p className="text-xs text-text-muted">
              Giám sát trạng thái bơm, lưu lượng hồi tiếp tức thời và chuỗi xác nhận an toàn
            </p>
          </div>
        </div>

        <button
          type="button"
          onClick={() => setIsRfModalOpen(true)}
          className="flex items-center gap-2 px-3.5 py-2 rounded-lg bg-surface-raised border border-border/50 hover:border-primary/40 text-xs font-semibold text-text hover:text-primary transition-all active:scale-95 shadow-sm self-start sm:self-auto"
        >
          <Radio size={14} className="text-primary animate-pulse" />
          <span>Quét Node RF</span>
        </button>
      </div>

      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
        {nodeIds.map((nodeId) => (
          <NodeCard key={nodeId} nodeId={nodeId} />
        ))}
      </div>

      <RfDiscoveryModal
        isOpen={isRfModalOpen}
        onClose={() => setIsRfModalOpen(false)}
      />
    </section>
  );
}
