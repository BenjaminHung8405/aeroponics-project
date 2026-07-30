# Dashboard Page Design System: High-Tech Eco Greenhouse

> **PROJECT:** Aeroponics Smart Farm  
> **PAGE:** Real-Time Mobile Telemetry & Control Dashboard (`app/page.tsx`)  
> **DESIGN STYLE:** High-Tech Eco Glassmorphism (Mobile-First Architecture)  
> ⚠️ **HIERARCHICAL OVERRIDE:** Rules in this file **override** the Master file ([MASTER.md](file:///Users/benjaminhung8405/Code/mushroom-cp/aeroponics-project/.codex/design-system/aeroponics-smart-farm/MASTER.md)) for the Dashboard route.

---

## 📱 Mobile-First Page Architecture & Adaptive Grid Layout

```text
===================================================================================
📱 MOBILE PORTRAIT LAYOUT (375px - 430px)
===================================================================================
+---------------------------------------------------------------------------------+
|  🌿 HEADER BAR: System Status (Online) | Day Schedule (Sun Icon) | Quick Menu   |
+---------------------------------------------------------------------------------+
|  💧 RELAY 1: ZONE A (ACTIVE SPRAYING)                                           |
|  Status: SPRAYING (30s) | Timer: 00:22s (JetBrains Mono 2.25rem)                 |
|  Schedule: Day (30s / 5m) | Night (30s / 15m)                                    |
|  [ AUTO / MANUAL SWITCH ]  [ ⚡ CALIBRATE / FLUSH (48px) ]                        |
+---------------------------------------------------------------------------------+
|  💧 RELAY 2: ZONE B (COOLDOWN IDLE)                                             |
|  Status: COOLDOWN | Timer: 04:15m                                               |
+---------------------------------------------------------------------------------+
|  💧 RELAY 3 & 4 (Vertical Stack / Mobile Swipeable Stack)                       |
+---------------------------------------------------------------------------------+
|  🧪 TUYA PH-W218 WATER QUALITY TELEMETRY (Horizontal Scroll / 2-Col Grid)       |
|  [ pH: 6.20 ]  [ EC: 1.80 mS ]  [ TDS: 900 ppm ]  [ Temp: 21.5°C ]              |
|  [ ORP: +320mV ] [ Salinity: 450ppm ] [ CF: 18 ] [ SG: 1.002 ]                  |
+---------------------------------------------------------------------------------+
|  📈 REAL-TIME SPRAY HISTORY & EDGE SYSTEM LOGS                                  |
+---------------------------------------------------------------------------------+
|  🚨 STICKY MOBILE ACTION BAR (Emergency Pump Stop | Manual Spray All)           |
+---------------------------------------------------------------------------------+

===================================================================================
💻 DESKTOP / 4K WALL DISPLAY LAYOUT (1024px - 1440px+)
===================================================================================
+---------------------------------------------------------------------------------+
|  🌿 GREENHOUSE HEADER BAR (System Diagnostics | DS3231 RTC Clock | Schedule)    |
+---------------------------------------------------------------------------------+
|  [ Relay 1 - Zone A ] [ Relay 2 - Zone B ] [ Relay 3 - Zone C ] [ Relay 4 - D ] |
+---------------------------------------------------------------------------------+
|  🧪 TUYA PH-W218 WATER QUALITY TELEMETRY (8-in-1 Full Meter Gauge Grid)          |
+---------------------------------------------------------------------------------+
|  [ 📈 REAL-TIME TELEMETRY GRAPH ]  |  [ 📋 LIVE MQTT SYSTEM INCIDENT LOGS ]     |
+---------------------------------------------------------------------------------+
```

---

## 🎨 Component Specifications & Design Tokens

### 1. Top Navigation & System Status Bar (`components/header-bar.tsx`)

- **Container Styling:** Translucent Bio-Glass Header (`background: rgba(15, 35, 27, 0.80)`, `backdrop-filter: blur(12px)`).
- **Mobile Layout:** Fixed sticky top header, $56\text{px}$ height, horizontal layout with auto-ellipsis for small viewports.
- **System Diagnostic Badges:**
  - `ESP32 Edge Master Status`: Online pulse indicator (`#10B981` Emerald glow with $8\text{px}$ dot).
  - `RTC Clock Sync`: Synchronized DS3231 + NTP badge (`#34D399`).
  - `Day / Night Mode Indicator`: Active Schedule Badge.
    - **Day Schedule (06:00 - 18:00):** Lucide `Sun` SVG icon (`#F59E0B` Amber Gold background tint `rgba(245, 158, 11, 0.15)`).
    - **Night Schedule (18:00 - 06:00):** Lucide `Moon` SVG icon (`#818CF8` Indigo background tint `rgba(129, 140, 248, 0.15)`).

---

### 2. 4-Relay Aeroponic Control Cards (`components/relay-card.tsx`)

Each card represents an independent aeroponic misting zone (Zone A, Zone B, Zone C, Zone D).

#### Responsive Grid Distribution:
- Mobile ($375\text{px}$): Single column full-width stack (`w-full`), $16\text{px}$ gap.
- Tablet ($768\text{px}$): $2\times 2$ grid layout (`grid-cols-2`).
- Desktop ($1024\text{px}+$): $4\times 1$ horizontal grid layout (`grid-cols-4`).

#### Card State Visual Specs:
```css
/* Base Mobile Card */
.relay-card {
  background: rgba(15, 35, 27, 0.70);
  backdrop-filter: blur(16px);
  border: 1px solid rgba(52, 211, 153, 0.20);
  border-radius: 20px;
  padding: 20px;
  min-height: 220px; /* Fixed height prevents layout shift */
  display: flex;
  flex-direction: column;
  justify-content: space-between;
}

/* Active Spraying State (30s mist pulse) */
.relay-card.spraying {
  border-color: #10B981;
  box-shadow: 0 0 25px 0 rgba(16, 185, 129, 0.30);
}

/* Cooldown Idle State */
.relay-card.cooldown {
  border-color: rgba(52, 211, 153, 0.20);
}
```

#### Card Content Hierarchy:
1. **Header Row:**
   - Zone Title (e.g. `Relay 1 - Zone A Aeroponics`).
   - Status Badge: `SPRAYING (30s)` (`bg-emerald-500/20 text-emerald-400 border-emerald-500/40`) vs `COOLDOWN IDLE` (`bg-slate-800/40 text-slate-300`).
2. **Real-Time Countdown Timer:**
   - Displayed in `JetBrains Mono` font, large size (`2.25rem` on mobile, `2.5rem` on desktop), glowing Emerald (`#10B981`) when spraying, crisp Mint White (`#F0FDF4`) during cooldown.
3. **Schedule Parameters (Sub-text):**
   - Day Cycle: Spray $30\text{s}$ / Cooldown $5\text{m}$.
   - Night Cycle: Spray $30\text{s}$ / Cooldown $15\text{m}$.
4. **Interactive Mobile Controls:**
   - **Mode Switcher:** Segmented control toggling between `AUTO` (NVS Edge Controller Schedule) and `MANUAL` (Direct relay actuation). Minimum touch height $44\text{px}$.
   - **Manual Action Button:** `Pulse Spray 10s / Flush` button ($48\text{px}$ touch target, `active:scale-95`).

---

### 3. Tuya PH-W218 Water Quality Telemetry Widget (`components/water-gauge.tsx`)

Displays real-time nutrient reservoir telemetry gathered via `tuya-local` bridge (DP101 - DP106).

#### Mobile Display Strategy:
- Mobile ($375\text{px}$): 2-column metric grid (`grid-cols-2 gap-3`) or horizontal swipeable cards to maximize readability without screen clutter.
- Desktop ($1024\text{px}+$): 8-in-1 unified telemetry dashboard grid (`grid-cols-4 lg:grid-cols-8`).

#### Water Quality Sensor Range Thresholds:

| Metric | Optimal Aeroponic Range | Display Color | Safety Indicator | Lucide Icon |
|---|---|---|---|---|
| **pH** | `5.80 – 6.50 pH` | `#10B981` (Normal) / `#EF4444` (Out of Range) | Dynamic Gauge Card | `Activity` |
| **EC** | `1.20 – 2.40 mS/cm` | `#34D399` | Metric Card | `Zap` |
| **TDS** | `600 – 1200 ppm` | `#38BDF8` | Metric Card | `Droplets` |
| **Temp** | `18.0°C – 22.5°C` | `#F59E0B` | Thermal Meter Widget | `Thermometer` |
| **ORP** | `+250 mV – +450 mV` | `#A855F7` | Bio-Oxidation Status | `ShieldAlert` |
| **Salinity** | `< 1000 ppm` | `#64748B` | Secondary Metric | `Sliders` |
| **CF** | Standard Scale | `#94A3B8` | Sub-metric | `Activity` |
| **SG** | `1.000 – 1.005` | `#94A3B8` | Sub-metric | `Activity` |

---

### 4. Live Telemetry Charts & Incident Log (`components/telemetry-log.tsx`)

- **Nutrient Trend Chart:** Real-time dual-axis area chart displaying pH & EC fluctuations over time ($24\text{h}$ window).
- **Mobile Optimization:** Touch-enabled tooltip inspection, collapsible timeline height on mobile devices ($240\text{px}$ max height on mobile, $380\text{px}$ on desktop).
- **Incident Alarm Console:** Live MQTT log feed displaying hardware events (e.g. Pump Overcurrent, Wi-Fi Reconnect, LWT Keepalive Disconnect).

---

### 5. Sticky Mobile Action Bar (`components/mobile-action-bar.tsx`)

- **Container:** Fixed bottom action bar (`position: fixed; bottom: 0; left: 0; right: 0;`), height $68\text{px}$, blurred bio-glass backdrop (`backdrop-filter: blur(20px)`), with `padding-bottom: env(safe-area-inset-bottom)`.
- **Visible Viewports:** Displayed on mobile viewports ($< 768\text{px}$), hidden on desktop screens (`md:hidden`).
- **Control Actions:**
  1. **Emergency Pump Stop:** High-visibility Red Solid Button (`bg-red-600 hover:bg-red-700`), minimum touch size $48\text{px}\times 48\text{px}$, with Lucide `ShieldAlert` icon.
  2. **Manual Override All Zones:** Secondary Outline Button (`border-emerald-500 text-emerald-400`), minimum touch size $48\text{px}\times 48\text{px}$, with Lucide `Zap` icon.

---

## 🚫 Page Anti-Patterns

1. ❌ **No Browser Reloads on Update:** Data changes (timers, pH/EC metrics, relay states) must stream live via WebSocket / MQTT subscriptions.
2. ❌ **No Vertical Overflow Shifts:** Cards must maintain constant heights (`min-height`) during state transitions to prevent jumpy layout shifts on mobile screens.
3. ❌ **No Tiny Tap Buttons:** All interactive controls must satisfy the $44\text{px} - 48\text{px}$ mobile touch target requirement.
4. ❌ **No Hardcoded Emojis:** Replace all inline emojis with official Lucide SVG icons.

---

## ✅ Pre-Delivery Verification Checklist for Dashboard

- [ ] Mobile navigation bar and sticky bottom action bar function correctly on mobile viewports ($375\text{px} - 430\text{px}$).
- [ ] 4-Relay cards render countdown timers cleanly in `JetBrains Mono` with zero layout shifting between Spray and Cooldown states.
- [ ] Tuya PH-W218 8-in-1 water quality values highlight out-of-bounds metrics in Alarm Red (`#EF4444`).
- [ ] Interactive buttons feature tactile feedback (`active:scale-95`) and explicit `cursor-pointer` classes.
- [ ] All icon representations use valid Lucide SVG components (Zero Emojis).
- [ ] Page document written strictly in technical English.
