'use client';

import React from 'react';
import { getOutcomeConfig } from '../../lib/constants';
import {
  CheckCircle2,
  Zap,
  Clock,
  AlertTriangle,
  Timer,
} from 'lucide-react';

interface OutcomeBadgeProps {
  outcome?: string | null;
  className?: string;
  showIcon?: boolean;
}

/**
 * OutcomeBadge Component
 * Hard Rule S4-OUTCOME-08: Renders outcome badges with styling per OUTCOME_CONFIG
 * and handles FAULT_* prefixes gracefully with danger styling.
 * Hard Rule S4-DS-ICON-14: Zero emoji, 100% Lucide SVG icons.
 */
export function OutcomeBadge({
  outcome,
  className = '',
  showIcon = true,
}: OutcomeBadgeProps) {
  const config = getOutcomeConfig(outcome);

  const renderIcon = () => {
    if (!showIcon) return null;

    if (config.isFault || outcome?.startsWith('FAULT_')) {
      return <AlertTriangle size={13} className="shrink-0" aria-hidden="true" />;
    }

    switch (outcome) {
      case 'FLOW_CONFIRMED':
        return <CheckCircle2 size={13} className="shrink-0" aria-hidden="true" />;
      case 'RF_ACKED':
        return <Zap size={13} className="shrink-0" aria-hidden="true" />;
      case 'TIMEOUT':
        return <Timer size={13} className="shrink-0" aria-hidden="true" />;
      default:
        return <Clock size={13} className="shrink-0" aria-hidden="true" />;
    }
  };

  return (
    <span
      className={`inline-flex items-center gap-1.5 px-2.5 py-0.5 rounded-full text-xs font-medium border tabular-nums transition-colors duration-200 ${config.bgClass} ${config.textClass} ${config.borderClass} ${className}`}
      role="status"
      aria-label={`Trạng thái lệnh: ${config.label}`}
    >
      {renderIcon()}
      <span>{config.label}</span>
    </span>
  );
}
