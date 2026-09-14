'use client';

import React from 'react';
import { AlertTriangle, AlertCircle, Info, ShieldAlert } from 'lucide-react';
import { formatUserErrorMessage, type FormattedError } from '../../lib/messages';

export type AlertBannerVariant = 'danger' | 'warning' | 'info';

interface AlertBannerProps {
  error?: unknown;
  title?: string;
  message?: string;
  hint?: string;
  variant?: AlertBannerVariant;
  fallbackContext?: string;
  className?: string;
}

export function AlertBanner({
  error,
  title,
  message,
  hint,
  variant = 'danger',
  fallbackContext = 'Đã xảy ra lỗi',
  className = '',
}: AlertBannerProps) {
  let displayTitle = title;
  let displayMessage = message;
  let displayHint = hint;

  if (error) {
    const formatted: FormattedError = formatUserErrorMessage(error, fallbackContext);
    displayTitle = title || formatted.title;
    displayMessage = message || formatted.message;
    displayHint = hint || formatted.hint;
  }

  if (!displayTitle && !displayMessage) {
    return null;
  }

  const isDanger = variant === 'danger';
  const isWarning = variant === 'warning';

  const containerClasses = isDanger
    ? 'bg-danger/15 border-danger/40 text-danger'
    : isWarning
      ? 'bg-accent-amber/15 border-accent-amber/40 text-accent-amber'
      : 'bg-accent-indigo/15 border-accent-indigo/40 text-accent-indigo';

  return (
    <div
      role="alert"
      aria-live="assertive"
      className={`p-3.5 rounded-xl border flex items-start gap-3 transition-all duration-150 ${containerClasses} ${className}`}
    >
      <div className="shrink-0 mt-0.5">
        {isDanger && <AlertTriangle size={18} aria-hidden="true" />}
        {isWarning && <AlertCircle size={18} aria-hidden="true" />}
        {!isDanger && !isWarning && <Info size={18} aria-hidden="true" />}
      </div>

      <div className="flex-1 min-w-0 text-xs sm:text-sm">
        {displayTitle && (
          <h4 className="font-bold leading-tight mb-1">{displayTitle}</h4>
        )}
        {displayMessage && (
          <p className="text-text-muted leading-relaxed text-xs">
            {displayMessage}
          </p>
        )}
        {displayHint && (
          <p className="text-text-subtle text-[11px] mt-1.5 pt-1.5 border-t border-border/20">
            {displayHint}
          </p>
        )}
      </div>
    </div>
  );
}
