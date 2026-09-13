import React from 'react';
import { getStalenessLevel, type StalenessLevel } from '../../lib/constants';

interface StalenessIndicatorProps {
  lastSeenAt?: string | null;
  isStale?: boolean;
  staleForMs?: number;
  className?: string;
  showLabel?: boolean;
}

export type { StalenessLevel };

/**
 * StalenessIndicator Component
 * Hard Rule S4-STALENESS-09:
 *  - < 60s  -> bg-primary
 *  - 60-120s -> bg-accent-amber
 *  - >= 120s -> bg-danger animate-pulse
 */
export function StalenessIndicator({
  lastSeenAt,
  isStale,
  staleForMs,
  className = '',
  showLabel = false,
}: StalenessIndicatorProps) {
  const { level, label } = getStalenessLevel(lastSeenAt, isStale, staleForMs);

  let dotColorClass = 'bg-primary';
  if (level === 'warning') {
    dotColorClass = 'bg-accent-amber';
  } else if (level === 'stale') {
    dotColorClass = 'bg-danger animate-pulse';
  }

  return (
    <span
      className={`inline-flex items-center gap-1.5 ${className}`}
      role="status"
      aria-label={`Trạng thái kết nối viễn thám: ${label}`}
      title={label}
    >
      <span
        className={`w-2.5 h-2.5 rounded-full shrink-0 transition-colors duration-200 ${dotColorClass}`}
        aria-hidden="true"
      />
      {showLabel && (
        <span className="text-xs text-text-muted tabular-nums">{label}</span>
      )}
    </span>
  );
}
