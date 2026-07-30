# Design System Master File: Aeroponics Smart Farm

> **HIERARCHICAL RULE:** When building or modifying a specific page, first check `design-system/pages/[page-name].md`.
> If that file exists, its rules **override** this Master file.
> If no page-specific file exists, strictly adhere to the global rules below.

---

## 📱 Executive Overview & Design Concept

- **Project:** Aeroponics Smart Farm (High-Tech Automated Aeroponic Greenhouse Telemetry & Relay Control System)
- **Design Persona:** Senior UI/UX Architect (10+ Years Experience in Mobile-First IoT & Telemetry Dashboards)
- **Core Concept:** High-Tech Eco Glassmorphism (Bio-Automation, Dark Mode Telemetry & Touch-Optimized Controls)
- **Primary Design Strategy:** Mobile-First Architecture ($375\text{px} - 430\text{px}$ primary viewports, scaling seamlessly to tablet and 4K wall displays)
- **Aesthetic Tone:** Sleek, Premium, Technical, Eco-Friendly, Ultra-Readable OLED Dark Mode

---

## 📐 Mobile-First Responsive Breakpoints

All components must be designed for mobile screens first and expanded progressively using responsive grid utilities.

| Breakpoint Label | Min Width | Target Device | Layout Strategy |
|---|---|---|---|
| **`xs` (Mobile Portrait)** | `375px - 430px` | iPhone 14/15/16, Pixel, Galaxy | Single-column stack, $100\%$ container width, horizontal scroll carousels, sticky bottom navigation bar |
| **`sm` (Mobile Landscape)** | `640px` | Large Mobiles, Small Foldables | 2-column flex grid, compact stat cards |
| **`md` (Tablet Portrait)** | `768px` | iPad Mini, iPad Air | 2-column grid layout, side drawer navigation |
| **`lg` (Tablet Landscape / Laptop)** | `1024px` | iPad Pro, Laptops ($13" - 15"$) | 3-column / $2+1$ column telemetry layout |
| **`xl` (Desktop / Wall Display)** | `1440px+` | 4K Monitoring Screens, Industrial Displays | 4-column full grid with persistent logs & real-time analytics panel |

---

## 👆 Mobile Touch Ergonomics & Micro-Interactions

1. **Touch Target Size**:
   - Primary buttons and interactive toggles: Minimum **$48\text{px}\times 48\text{px}$**.
   - Secondary actions, tabs, and small badges: Minimum **$44\text{px}\times 44\text{px}$**.
   - Touch padding: Minimum $8\text{px}$ touch target clearance between adjacent interactive elements.
2. **Thumb-Zone Optimization**:
   - Primary control actions (Emergency Stop, Manual Spray Override, Zone Switchers) placed in the lower $60\%$ of mobile viewports for easy single-hand reach.
3. **Tactile Feedback & State Transitions**:
   - Active press state: `active:scale-95` transform micro-interaction.
   - Clickable elements: Mandatory `cursor-pointer` class.
   - State transition timing: $150\text{ms} - 250\text{ms}$ smooth `cubic-bezier(0.4, 0, 0.2, 1)`.
   - Layout stability: Zero dimension or padding changes during state switches (e.g., Spraying $\leftrightarrow$ Cooldown).

---

## 🌿 Global Color Palette (High-Tech OLED Eco Theme)

Designed for $24/7$ continuous greenhouse display screens, minimizing power consumption on OLED panels while ensuring maximum legibility (WCAG AAA compliant).

| Role | Hex / RGBA | CSS Variable | Usage & Semantics |
|---|---|---|---|
| **Background (Deep Forest Midnight)** | `#07130E` | `--color-background` | Deep OLED dark background for eye-strain prevention |
| **Surface Card (Bio-Glass Translucent)** | `rgba(15, 35, 27, 0.70)` | `--color-surface` | Glassmorphic card surface with frosted bio-tint |
| **Surface Hover** | `rgba(20, 48, 37, 0.85)` | `--color-surface-hover` | Enhanced contrast surface on hover/focus |
| **Card Border (Emerald Glow)** | `rgba(52, 211, 153, 0.20)` | `--color-border` | Subtle eco-green glowing border |
| **Primary Brand / Active Spray** | `#10B981` | `--color-primary` | Vibrant Emerald Green indicating active mist spraying & pump operational |
| **Secondary Accent / Lush Mint** | `#34D399` | `--color-secondary` | Refreshing Mint Green for sub-metrics, status indicators, and healthy ranges |
| **Sunlight / Day Schedule Accent** | `#F59E0B` | `--color-accent-amber` | Warm Amber Gold for Day Schedule indicator & solar telemetry |
| **Night Schedule Accent** | `#818CF8` | `--color-accent-indigo` | Soft Indigo Blue for Night Schedule indicator |
| **Alert / Alarm Red** | `#EF4444` | `--color-danger` | Critical Red for hardware faults, pump failures, or MQTT disconnects |
| **Text Primary (High Contrast)** | `#F0FDF4` | `--color-text` | Crisp Emerald-tinted White ($\text{Contrast Ratio } \ge 7:1$) |
| **Text Muted / Secondary** | `#86EFAC` | `--color-text-muted` | Soft Mint Green for labels, sub-headers, and unit markers |
| **Text Subtle / Disabled** | `#4B7260` | `--color-text-subtle` | Low contrast green-gray for inactive state text |

---

## 🔤 Typography System

- **Primary UI & Headings:** [Outfit](https://fonts.google.com/specimen/Outfit) (Modern, clean, rounded geometric sans-serif)
- **Monospace Telemetry & Timers:** [JetBrains Mono](https://fonts.google.com/specimen/JetBrains+Mono) (High-precision monospace for real-time timers & sensor values)

```css
@import url('https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;500;600;700&family=Outfit:wght@300;400;500;600;700;800&display=swap');

:root {
  --font-sans: 'Outfit', -apple-system, BlinkMacSystemFont, sans-serif;
  --font-mono: 'JetBrains Mono', monospace;
}

/* Mobile-First Typography Hierarchy */
h1 { font-family: var(--font-sans); font-size: 1.75rem; font-weight: 700; line-height: 1.2; letter-spacing: -0.02em; }
h2 { font-family: var(--font-sans); font-size: 1.35rem; font-weight: 600; line-height: 1.3; }
h3 { font-family: var(--font-sans); font-size: 1.125rem; font-weight: 600; line-height: 1.4; }
.metric-value { font-family: var(--font-mono); font-size: 1.75rem; font-weight: 700; letter-spacing: -0.03em; }
.timer-countdown { font-family: var(--font-mono); font-size: 2.25rem; font-weight: 700; font-variant-numeric: tabular-nums; }
```

---

## 🔮 Bio-Glassmorphism & Visual Effects

```css
/* Glassmorphic Bio-Card Component */
.glass-card {
  background: rgba(15, 35, 27, 0.70);
  backdrop-filter: blur(16px);
  -webkit-backdrop-filter: blur(16px);
  border: 1px solid rgba(52, 211, 153, 0.20);
  border-radius: 16px;
  box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.45);
  transition: border-color 200ms ease, box-shadow 200ms ease, transform 150ms ease;
}

.glass-card:hover, .glass-card:focus-within {
  border-color: rgba(52, 211, 153, 0.45);
  box-shadow: 0 12px 40px 0 rgba(16, 185, 129, 0.18);
}

/* Active Spraying Glow */
.relay-glow-active {
  border-color: #10B981 !important;
  box-shadow: 0 0 25px 0 rgba(16, 185, 129, 0.35) !important;
  animation: pulse-emerald 2s infinite ease-in-out;
}

@keyframes pulse-emerald {
  0%, 100% { box-shadow: 0 0 20px 0 rgba(16, 185, 129, 0.25); }
  50% { box-shadow: 0 0 35px 4px rgba(16, 185, 129, 0.45); }
}
```

---

## 🖼️ Iconography & Visual Asset Standards

- **Strict SVG Icon Standard:** Use Lucide Icons or Heroicons packages ONLY (`Droplets`, `Timer`, `Activity`, `Sun`, `Moon`, `Zap`, `Thermometer`, `ShieldAlert`, `RefreshCw`, `Sliders`, `CheckCircle2`, `AlertTriangle`).
- 🚫 **Zero Emojis:** Emojis (e.g. 💧, ☀️, 🌙, ⚙️, 🧪) are strictly prohibited as UI icons due to operating system rendering inconsistencies and unprofessional appearance.
- **Consistent Icon Sizing:** Standardize on fixed $24\times 24\text{px}$ viewBox (`w-6 h-6` or `w-5 h-5` for badges).

---

## 🚫 Strict Anti-Patterns

1. ❌ **No Emoji Icons:** Emojis are strictly banned from UI layouts. Always use official Lucide/Heroicons SVG components.
2. ❌ **No Desktop-Only Layouts:** Never design fixed desktop grids that wrap awkwardly or produce horizontal overflow on mobile screens ($375\text{px}$).
3. ❌ **No Layout Shifting:** Card state transitions (e.g., Spray Active $\leftrightarrow$ Cooldown Idle) must maintain fixed card dimensions without altering padding or font boundaries.
4. ❌ **No Page Reloads for Data Updates:** Live telemetry values and countdown timers must update seamlessly via WebSockets / MQTT streams without triggering browser reloads.
5. ❌ **No Low-Contrast Text:** Primary body text and telemetry metrics must maintain a contrast ratio $\ge 7:1$ against dark OLED backgrounds.
6. ❌ **No Tiny Touch Targets:** Interactive elements smaller than $44\times 44\text{px}$ are prohibited on mobile viewports.

---

## ✅ Pre-Delivery Quality Checklist

- [ ] Mobile-First verification passed at $375\text{px}$, $640\text{px}$, $768\text{px}$, $1024\text{px}$, and $1440\text{px}$ viewports.
- [ ] Primary font set to `Outfit` and numerical telemetry / timers set to `JetBrains Mono` with `tabular-nums`.
- [ ] Interactive buttons and cards have explicit `cursor-pointer` and `active:scale-95` states.
- [ ] Minimum touch target sizes ($44\text{px} - 48\text{px}$) strictly enforced across all mobile controls.
- [ ] All icons mapped to valid Lucide SVG components (Zero Emojis present).
- [ ] High contrast text compliant with WCAG AAA standards.
- [ ] Safe areas respected on iOS devices (`env(safe-area-inset-bottom)` applied to mobile bottom bars).
