import type { Metadata } from 'next';

export const metadata: Metadata = {
  title: 'Đăng nhập — Aeroponics Smart Farm',
};

/**
 * Login page — Public route.
 *
 * Route group: (auth) — không có shared layout, truy cập trực tiếp.
 * middleware.ts sẽ redirect /login → /dashboard nếu đã có token.
 *
 * LoginForm component sẽ được implement ở Track B-1.
 * Placeholder hiển thị scaffold Dark OLED login screen.
 */
export default function LoginPage() {
  return (
    <main className="min-h-dvh bg-background flex items-center justify-center px-4">
      <div className="w-full max-w-sm">
        {/* LoginForm — implement Track B-1 */}
        <div className="glass-card p-8 space-y-6">
          <div className="text-center space-y-2">
            <h1 className="text-text font-sans text-2xl font-bold">
              Aeroponics Smart Farm
            </h1>
            <p className="text-text-muted text-sm">
              Hệ thống điều khiển khí canh thông minh
            </p>
          </div>

          {/* LoginForm placeholder — Track B-1 sẽ replace */}
          <p className="text-text-subtle text-xs text-center">
            [LoginForm — Track B-1]
          </p>
        </div>
      </div>
    </main>
  );
}
