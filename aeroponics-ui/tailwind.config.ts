import type { Config } from 'tailwindcss';

const config: Config = {
  content: [
    './src/pages/**/*.{js,ts,jsx,tsx,mdx}',
    './src/components/**/*.{js,ts,jsx,tsx,mdx}',
    './src/app/**/*.{js,ts,jsx,tsx,mdx}',
  ],
  theme: {
    extend: {
      // ================================================================
      // MASTER.md: 11 Core Color Tokens — Hard Rule S4-DS-COLOR-13
      // Port 1:1 từ CSS variables sang Tailwind utilities
      // Usage: bg-background, text-primary, border-border, etc.
      // ================================================================
      colors: {
        background:     '#07130E',               // Deep Forest Midnight OLED
        surface:        'rgba(15,35,27,0.70)',   // Bio-Glass Translucent card
        'surface-hover':'rgba(20,48,37,0.85)',   // Enhanced contrast on hover/focus
        border:         'rgba(52,211,153,0.20)', // Emerald Glow card border
        primary:        '#10B981',               // Active Spray / Pump ON
        secondary:      '#34D399',               // Sub-metrics / Healthy range
        'accent-amber': '#F59E0B',               // Day Schedule / Warning
        'accent-indigo':'#818CF8',               // Night Schedule
        danger:         '#EF4444',               // Critical Red — Fault / MQTT disconnect
        text:           '#F0FDF4',               // High Contrast — WCAG AAA 16.8:1
        'text-muted':   '#86EFAC',               // Labels / sub-headers — WCAG AAA 8.2:1
        'text-subtle':  '#4B7260',               // Disabled / inactive state
      },

      // ================================================================
      // MASTER.md: Typography System — Hard Rule S4-DS-FONT-12
      // Primary UI: Outfit | Monospace telemetry: JetBrains Mono
      // ================================================================
      fontFamily: {
        sans: ['Outfit', '-apple-system', 'BlinkMacSystemFont', 'sans-serif'],
        mono: ['JetBrains Mono', 'monospace'],
      },

      // ================================================================
      // MASTER.md: Bio-Glassmorphism keyframes
      // pulse-emerald  — Node active FLOW_CONFIRMED glow (2s cycle)
      // staleness-pulse — Staleness dot blink when ≥120s stale
      // ================================================================
      keyframes: {
        'pulse-emerald': {
          '0%, 100%': { boxShadow: '0 0 20px 0 rgba(16,185,129,0.25)' },
          '50%':       { boxShadow: '0 0 35px 4px rgba(16,185,129,0.45)' },
        },
        'staleness-pulse': {
          '0%, 100%': { opacity: '1' },
          '50%':       { opacity: '0.25' },
        },
      },
      animation: {
        'pulse-emerald': 'pulse-emerald 2s infinite ease-in-out',
        'staleness':     'staleness-pulse 1.5s ease-in-out infinite',
      },

      // ================================================================
      // Touch Ergonomics — MASTER.md §Mobile Touch Ergonomics
      // Hard Rule S4-DS-TOUCH-15
      // ================================================================
      minHeight: {
        touch:         '44px',  // Secondary actions, tabs, badges
        'touch-primary':'48px', // Primary buttons, toggles
      },

      // ================================================================
      // Glassmorphism
      // ================================================================
      backdropBlur: {
        glass: '16px',
      },

      borderRadius: {
        card: '16px',
      },

      // ================================================================
      // State transition timing — MASTER.md: 150ms–250ms cubic-bezier
      // ================================================================
      transitionTimingFunction: {
        'eco': 'cubic-bezier(0.4, 0, 0.2, 1)',
      },
      transitionDuration: {
        '150': '150ms',
        '200': '200ms',
        '250': '250ms',
      },
    },
  },
  plugins: [],
};

export default config;
