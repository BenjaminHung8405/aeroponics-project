import type { Metadata } from 'next';
import { Suspense } from 'react';
import { LoginForm } from '@/components/auth/LoginForm';
import { Droplets, Loader2 } from 'lucide-react';

export const metadata: Metadata = {
  title: 'Đăng nhập — Aeroponics Smart Farm',
  description: 'Đăng nhập hệ thống điều khiển và giám sát khí canh thông minh',
};

function LoginFormFallback() {
  return (
    <div className="flex items-center justify-center py-12 text-text-muted">
      <Loader2 className="w-8 h-8 animate-spin text-primary" />
    </div>
  );
}

export default function LoginPage() {
  return (
    <main className="min-h-dvh bg-background flex items-center justify-center p-4 sm:p-6">
      <div className="w-full max-w-md">
        <div className="glass-card p-6 sm:p-8 space-y-6">
          {/* Brand Header */}
          <div className="text-center space-y-3">
            <div className="inline-flex items-center justify-center w-14 h-14 rounded-2xl bg-primary/10 border border-primary/20 text-primary mx-auto shadow-inner">
              <Droplets className="w-7 h-7 text-primary" />
            </div>
            <div>
              <h1 className="text-text font-sans text-2xl sm:text-3xl font-bold tracking-tight">
                Aeroponics Smart Farm
              </h1>
              <p className="text-text-muted text-sm mt-1 font-sans">
                Hệ thống điều khiển khí canh thông minh
              </p>
            </div>
          </div>

          {/* Form with Suspense boundary for useSearchParams */}
          <Suspense fallback={<LoginFormFallback />}>
            <LoginForm />
          </Suspense>

          {/* System Footer Note */}
          <div className="pt-2 border-t border-border/50 text-center">
            <p className="text-text-subtle text-xs font-mono">
              Bảo mật 24/7 • IoT Gateway Station
            </p>
          </div>
        </div>
      </div>
    </main>
  );
}
