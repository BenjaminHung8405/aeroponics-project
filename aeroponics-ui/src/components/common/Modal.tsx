'use client';

import React, { useEffect, useState } from 'react';
import { createPortal } from 'react-dom';
import { X } from 'lucide-react';

interface ModalProps {
  isOpen: boolean;
  onClose: () => void;
  title: string;
  titleId?: string;
  children: React.ReactNode;
  maxWidth?: 'sm' | 'md' | 'lg' | 'xl';
  closeOnBackdrop?: boolean;
}

const MAX_WIDTH_MAP = {
  sm: 'max-w-md',
  md: 'max-w-lg',
  lg: 'max-w-2xl',
  xl: 'max-w-4xl',
};

/**
 * Accessible Bio-Glass Modal Component
 * Follows:
 *  - MASTER.md: Bio-glassmorphism styling with backdrop blur.
 *  - ui-ux-pro-max: A11y WCAG AAA dialog with Escape key listener and body scroll lock.
 *  - S4-DS-TOUCH-15: Close button with min-h-[44px] min-w-[44px] touch target.
 *  - S4-DS-ICON-14: Lucide SVG vector icon (Zero emoji).
 *  - React Portal: Mounts directly to document.body to prevent being trapped in
 *    ancestor containers with backdrop-filter or transform (fixes overflow & clipping bugs).
 */
export function Modal({
  isOpen,
  onClose,
  title,
  titleId = 'modal-title',
  children,
  maxWidth = 'md',
  closeOnBackdrop = true,
}: ModalProps) {
  const [mounted, setMounted] = useState(false);

  useEffect(() => {
    setMounted(true);
  }, []);

  useEffect(() => {
    if (!isOpen) return;

    const originalOverflow = document.body.style.overflow;
    document.body.style.overflow = 'hidden';

    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        onClose();
      }
    };

    window.addEventListener('keydown', handleKeyDown);

    return () => {
      document.body.style.overflow = originalOverflow;
      window.removeEventListener('keydown', handleKeyDown);
    };
  }, [isOpen, onClose]);

  if (!isOpen || !mounted) {
    return null;
  }

  const handleBackdropClick = (e: React.MouseEvent<HTMLDivElement>) => {
    if (closeOnBackdrop && e.target === e.currentTarget) {
      onClose();
    }
  };

  return createPortal(
    <div
      className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-background/80 backdrop-blur-md transition-opacity duration-200"
      onClick={handleBackdropClick}
      role="dialog"
      aria-modal="true"
      aria-labelledby={titleId}
    >
      <div
        className={`glass-card w-full ${MAX_WIDTH_MAP[maxWidth]} overflow-hidden shadow-2xl border border-border/40 transform transition-transform duration-200 scale-100`}
        onClick={(e) => e.stopPropagation()}
      >
        {/* Modal Header */}
        <div className="flex items-center justify-between px-5 py-4 border-b border-border/20">
          <h3 id={titleId} className="text-lg font-bold text-text">
            {title}
          </h3>
          <button
            type="button"
            onClick={onClose}
            className="inline-flex items-center justify-center w-11 h-11 rounded-lg text-text-subtle hover:text-text hover:bg-surface-hover active:scale-95 transition-colors cursor-pointer"
            aria-label="Đóng cửa sổ"
          >
            <X size={20} aria-hidden="true" />
          </button>
        </div>

        {/* Modal Body */}
        <div className="p-5 max-h-[calc(90dvh-120px)] overflow-y-auto">
          {children}
        </div>
      </div>
    </div>,
    document.body,
  );
}
