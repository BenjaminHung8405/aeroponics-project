import type { Metadata, Viewport } from 'next';
import './globals.css';
import { Providers } from './providers';

/**
 * Root Layout — Aeroponics Smart Farm Dashboard
 *
 * Responsibilities:
 *  - Google Fonts preload (Outfit + JetBrains Mono) via CSS @import in globals.css
 *  - Viewport meta: mobile-first, no forced zoom
 *  - Wrap with QueryClientProvider via <Providers>
 *  - Apply OLED dark background globally (bg-background)
 *  - iOS safe area bottom padding via env(safe-area-inset-bottom) in globals.css
 *
 * Hard Rules satisfied:
 *  - S4-DS-FONT-12: Outfit declared in globals.css @import
 *  - S4-DS-COLOR-13: bg-background class → #07130E OLED
 *  - S4-DS-MOBILE-17: viewport meta width=device-width, initial-scale=1
 */
export const metadata: Metadata = {
  title: 'Aeroponics Smart Farm',
  description: 'High-Tech Aeroponic Greenhouse Telemetry & Control Dashboard',
  robots: 'noindex, nofollow', // Internal company dashboard — no public indexing
};

export const viewport: Viewport = {
  width: 'device-width',
  initialScale: 1,
  // Allow user zoom for accessibility (no maximumScale restriction)
  themeColor: '#07130E', // OLED background as browser chrome accent
};

export default function RootLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return (
    <html lang="vi" className="bg-background">
      <body className="bg-background text-text font-sans antialiased">
        <Providers>{children}</Providers>
      </body>
    </html>
  );
}
