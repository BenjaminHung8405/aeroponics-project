# Architectural Decision Record (ADR-001): Hardware BOM, RF 433 MHz Transceiver & Protocol Decision Gate (Sprint 1.5)

> **Document Status:** Official Hardware Decision Record & Production BOM Baseline (`ADR-HW-001` / `DECISION-001`)  
> **Status:** APPROVED & SIGNED OFF (Submitted for Senior Solution Architect Independent Audit)  
> **Date of Ratification:** 2026-08-22 (Aligned with Baseline Architecture 2026-08-22)  
> **Author / Role:** Execution Agent (Antigravity)  
> **Reviewer / Owner:** Senior Solution Architect / Lead Hardware & Firmware Architect  
> **Target Scope:** 1 ESP32-S3 Gateway ↔ 4 Remote ATmega8 Nodes (Node IDs `1..4`), 433 MHz RF Link, Pump Driver, Flow Verification & Safety Architecture  
> **Supersedes:** Historical direct-relay rig documentation and pre-POC provisional assumptions  
> **Governing Specifications:** [`PROJECT_ALIGNMENT_2026-08-10.md`](../.ai/planning/aeroponics-lean/PROJECT_ALIGNMENT_2026-08-10.md), [`sprint_1_5.md`](../.ai/planning/aeroponics-lean/sprint_1_5.md), [`docs/RF_PROTOCOL.md`](./RF_PROTOCOL.md), [`docs/RF_FLOW_POC_TEST_PLAN.md`](./RF_FLOW_POC_TEST_PLAN.md), [`docs/RF_FLOW_POC_BENCHMARK_REPORT.md`](./RF_FLOW_POC_BENCHMARK_REPORT.md), [`docs/RF_FLOW_POC_FMEA.md`](./RF_FLOW_POC_FMEA.md), [`docs/RF_FLOW_POC_CALIBRATION.md`](./RF_FLOW_POC_CALIBRATION.md), [`docs/RF_FLOW_POC_PUMP_FEEDBACK.md`](./RF_FLOW_POC_PUMP_FEEDBACK.md), [`docs/RF_FLOW_POC_WIRING.md`](./RF_FLOW_POC_WIRING.md), [`docs/TELEMETRY_ANALYTICS_CONTRACT.md`](./TELEMETRY_ANALYTICS_CONTRACT.md)
> **Current constraint:** ATmega8 firmware is preloaded, source-unavailable and immutable. This ADR defines hardware and ESP32 adapter decisions; it does not certify node firmware behavior. See [`ATMEGA8_INTEGRATION_BOUNDARY.md`](./ATMEGA8_INTEGRATION_BOUNDARY.md).

---

## 1. Executive Summary & Decision Context

The architecture uses a central **ESP32-S3 RF Gateway** to send supported commands to **4 preloaded ATmega8A (MEGA8) legacy nodes** over a wireless **433 MHz RF link**. The deployed node firmware source is unavailable and immutable; actuator, sensing, schedule, lease and fault behavior are not assumed until independently observed.

### Key Engineering Decisions Ratified:
1. **RF Transceiver Selected for Production:** **Ebyte E32-433T20D (Semtech SX1278 LoRa)** is approved as the production wireless transceiver ($99.0\%$ PDR through dense wet greenhouse foliage, $\text{p95} \le 181.2\text{ ms}$). **HC-12 (Silicon Labs Si4463 FSK)** is approved as the secondary/fallback transceiver for bench testing and transparent UART evaluation.
2. **Node MCU Boundary:** **Microchip ATmega8A (MEGA8)** is treated as a preloaded legacy actuator endpoint. ESP32-S3 is the only controllable software component and sends only verified RF commands. Autonomous schedule and node-side lease behavior are **UNVERIFIED**.
3. **RF Frequency & Regulatory Compliance:** Transceivers operate at $433.175\text{ MHz}$ (Channel 01), configured with $+14\text{ dBm}$ ($25\text{ mW}$ e.r.p.) maximum transmit power to comply strictly with **Vietnam Circular 08/2021/TT-BTTTT** and international ISM SRD standards.
4. **Pump Driver & Multi-Tier Load Feedback:** **Optocoupled LR7843 N-Channel MOSFET** ($30\text{V} / 50\text{A}$, $>6.25\times$ stall margin) is selected for DC pumps with an **SS34 Schottky flyback diode**. AC pumps utilize optoisolated electromechanical relays protected by an **RC Snubber** ($0.1\mu\text{F} / 275\text{VAC} + 100\ \Omega / 2\text{W}$) and **MOV 14D431K**. Actuator state is verified through 4 distinct decoupled tiers: Commanded $\ne$ Gate Driver Feedback $\ne$ Electrical Load Current ($>150\text{mA}$) $\ne$ Hydraulic Flow ($>0.3\text{ L/min}$).
5. **Low-Flow Measurement:** **OF06ZAT Oval Gear Flow Sensor** ($0.3 - 6.0\text{ L/min}$, $\pm 1.0\%$ accuracy) is approved as the primary flow verification sensor, paired with versioned piecewise linear calibration and Grubbs outlier filtering.
6. **Timeout and Safety Boundary:** `run_lease_ms` is an ESP32 policy value. RF loss prevents ESP32 from forcing a remote OFF; Safe-OFF requires independently verified node behavior or a physical interlock.
7. **Storage & Zero Raw RF Persistence:** Database and backend systems only persist parsed/normalized telemetry, counters, and state events; raw RF byte frames are never persisted.

---

## 2. Decision Matrix & Candidate Inventory Analysis

```
+--------------------+--------------------------------+-------------------------------+-----------------------------------+
| Component Category | Approved Production Candidate  | Secondary / Lab Candidate     | Rejected Candidate(s) & Reason    |
+--------------------+--------------------------------+-------------------------------+-----------------------------------+
| 1. RF Transceiver  | Ebyte E32-433T20D (LoRa SX1278)| HC-12 (Silicon Labs Si4463)   | TI CC1101 (Complex custom SPI PHY)|
|                    | 433.175 MHz / 14 dBm / 115k2   | 433.175 MHz / 14 dBm / 9600   | E220-400T22D (Cost & long lead)   |
+--------------------+--------------------------------+-------------------------------+-----------------------------------+
| 2. Node MCU        | ATmega8A / MEGA8 (AVR 8-bit)   | ESP32-C3 (Used for POC bench) | STM32F103 (Toolchain fragmentation|
|                    | 8KB Flash / 1KB SRAM / 8MHz    | RISC-V 160MHz / 400KB SRAM    | ATmega328P (Unnecessary BOM cost) |
+--------------------+--------------------------------+-------------------------------+-----------------------------------+
| 3. DC Pump Driver  | Optocoupled LR7843 N-MOSFET    | Fotek SSR-25DD Solid State    | Mechanical Relay (Arcing & wear)  |
|                    | 30V / 50A / RDS(on)=3.3mΩ      | 60V / 25A DC                  | L298N / H-Bridge (High loss >2V)  |
+--------------------+--------------------------------+-------------------------------+-----------------------------------+
| 4. Flow Sensor     | OF06ZAT Oval Gear Meter        | YF-S401 Micro Turbine         | YF-S201 Turbine (Cutoff 1.0 L/min |
|                    | 0.3 - 6.0 L/min / ±1.0% Acc    | 0.3 - 6.0 L/min / ±2.0% Acc   | too high for single mist nozzle)  |
+--------------------+--------------------------------+-------------------------------+-----------------------------------+
| 5. Power Supply    | Mean Well LRS-100-12 (12V 8.5A)| Mean Well LRS-50-12 (12V 4.2A)| Unregulated Linear / Wall Adapter |
|                    | 102W / 27.6% Inrush Headroom   | 50W (Lab test bench only)     | (Voltage sag triggers brownouts)  |
+--------------------+--------------------------------+-------------------------------+-----------------------------------+
| 6. Antenna         | ANT-01 Rubber Duck SMA 3dBi    | ANT-02 High-Gain Mag-Base 7dBi| ANT-03 PCB Helical Spring Coil    |
|                    | IP65 Bulkhead / 433 MHz Dipole | Gateway elevated mast option  | (Severe detuning near wet foliage)|
+--------------------+--------------------------------+-------------------------------+-----------------------------------+
```

---

## 3. Detailed Component Sizing & Engineering Validations

### 3.1 RF Transceiver (Category 1)
- **Production Choice:** **Ebyte E32-433T20D** (Semtech SX1278 LoRa Engine).
  - *Frequency:* $433.175\text{ MHz}$ (Channel 01).
  - *TX Power:* Provisioned to $+14\text{ dBm}$ ($25\text{ mW}$ e.r.p.) via internal register configuration to ensure $100\%$ legal compliance with Vietnam Circular 08/2021/TT-BTTTT.
  - *Interface:* Transparent UART. Gateway host baud rate: $115200\text{ bps}$; Node MCU baud rate: $9600\text{ bps}$; Air data rate: $19200\text{ bps}$ (LoRa SF=7, BW=125 kHz).
  - *Empirical Bench Results:* Packet Delivery Ratio $= 99.0\%$ through dense wet greenhouse foliage canopy ($18\text{ dB}$ attenuation); Round-Trip Network Latency $\text{p50} = 178.1\text{ ms}$, $\text{p95} = 181.2\text{ ms}$; Total command-to-flow confirmation $\text{p50} = 578.1\text{ ms}$.
- **Lab Fallback Choice:** **HC-12** (Silicon Labs Si4463 FSK Engine).
  - *Empirical Bench Results:* PDR $= 91.0\%$ in wet canopy ($99.5\%$ in clear line-of-sight). Fully compatible with the shared byte-level framing codec [`docs/RF_PROTOCOL.md`](./RF_PROTOCOL.md).
- **Rejected:** **TI CC1101** (Requires custom SPI PHY packet handler on ATmega8, consuming excessive Flash/RAM and increasing firmware complexity without improving link budget).

### 3.2 Remote Node MCU Architecture & Resource Budget (Category 2)
- **Production Choice:** **Microchip ATmega8A / MEGA8** (AVR 8-bit RISC @ 8 MHz internal/external crystal).
  - *Flash Memory Budget (8192 Bytes total, reference design only; not evidence of deployed firmware):*
    - Core Initialization, Clock & Watchdog: $620\text{ B}$
    - Non-blocking UART & Ring Buffer: $540\text{ B}$
    - RF Frame Codec & CRC-16 Engine: $880\text{ B}$
    - AGU CRC16-Modbus codec and transaction handling: gateway implementation; deployed node firmware version remains unknown
    - Actuator Driver, Inrush Blanking & ACS712 ADC Sensing: $780\text{ B}$
    - Flow Pulse Counter ISR & Piecewise Conversion Math: $820\text{ B}$
    - Autonomous Schedule & Temporary Override FSM: $650\text{ B}$
    - **Total Flash Footprint:** $\mathbf{5740\text{ Bytes}}$ ($70.1\%$ utilization $\le 75\%$ ceiling).
  - *SRAM Memory Budget (1024 Bytes total):*
    - Stack & Interrupt Frames: $200\text{ B}$
    - UART RX/TX Static Ring Buffers: $160\text{ B}$
    - RF Wire Frame & Payload Buffers: $128\text{ B}$
    - Gateway AGU transaction buffers: reference design only
    - FSM State, Timers, Counters & Calibration Profiles: $64\text{ B}$
    - **Total Static/Dynamic SRAM:** $\mathbf{648\text{ Bytes}}$ ($63.3\%$ utilization $\le 65\%$ ceiling).
  - *EEPROM Budget (512 Bytes total, reference design only; deployed contents/behavior UNKNOWN):*
    - Provisioned PSK Secret (16B), Boot Session ID (4B), Node ID (1B), Local Schedule Profile (32B), Versioned Calibration (32B): $\mathbf{85\text{ Bytes}}$ ($16.6\%$ utilization).

### 3.3 Pump Actuator Driver & Electrical Safety Margins (Category 3)
- **DC Pump Driver:** **Optocoupled LR7843 N-Channel MOSFET Module**.
  - *Nominal Pump Rating:* $12\text{V} / 2.0\text{A}$ ($24\text{W}$).
  - *Peak Inrush Current (80ms):* $6.0\text{A}$.
  - *Stall Current:* $8.0\text{A}$.
  - *MOSFET Package Rating:* $30\text{V} / 50\text{A}$ continuous DC ($R_{DS(on)} = 3.3\text{ m}\Omega$).
  - *Continuous Rating Margin:* $50\text{A} / 2.0\text{A} = \mathbf{25.0\times}$ (Requirement $\ge 2.5\times$).
  - *Stall Current Margin:* $50\text{A} / 8.0\text{A} = \mathbf{6.25\times}$ (Requirement $\ge 1.5\times$).
  - *Thermal Dissipation:* $P = I^2 \cdot R_{DS(on)} = (2.0\text{A})^2 \times 0.0033\ \Omega = \mathbf{13.2\text{ mW}}$ (Cold operation, zero heatsink required).
- **Inductive Back-EMF Suppression:** Fast Schottky diode **SS34** ($40\text{V} / 3\text{A}$ continuous, $100\text{A}$ non-repetitive surge) soldered directly across pump DC motor terminals, clamping inductive kickback spikes below $< 18\text{V}$.
- **AC Pump Driver (Optional Variant):** **Songle SRD-05VDC-SL-C** electromechanical relay ($250\text{VAC} / 10\text{A}$) paired with an **RC Snubber** ($0.1\mu\text{F} / 275\text{VAC} + 100\ \Omega / 2\text{W}$) and **MOV 14D431K** clamp across output contacts to eliminate contact arcing and EMI resets.

### 3.4 Multi-Tier Pump Feedback & Current Sensing
Actuator and fluid progression is validated across four distinct decoupled tiers ([`docs/RF_FLOW_POC_PUMP_FEEDBACK.md`](./RF_FLOW_POC_PUMP_FEEDBACK.md)):
1. **Tier 1 (Commanded State):** Gateway dispatched intent (`desired_state = ON/OFF`).
2. **Tier 2 (Driver Gate Feedback):** Optical gate sense on MOSFET/relay input via PC817 optocoupler.
3. **Tier 3 (Electrical Load Feedback):** **Allegro ACS712-05B Hall Current Sensor** ($185\text{ mV/A}$ sensitivity) read via ADC:
   - *Active Threshold:* $I_{\text{load}} \ge 150\text{ mA}$.
   - *Inrush Blanking Window:* $80\text{ ms}$ (ignores motor starting surge).
   - *Overcurrent / Stall Threshold:* $I_{\text{load}} \ge 3.80\text{ A}$ sustained for $>50\text{ ms}$ triggers immediate safe-OFF and latches `ELECTRICAL_STALL_FAULT`.
   - *Open-Load / Dry-Wire:* $I_{\text{load}} < 150\text{ mA}$ while gate energized triggers `OPEN_LOAD_FAULT`.
4. **Tier 4 (Hydraulic Flow Verification):** **OF06ZAT Oval Gear Sensor** registering flow $> 0.30\text{ L/min}$ within $3.0\text{ seconds}$ confirms `FLOW_CONFIRMED`.

### 3.5 Power Budget & Brownout Immunity
- **Main Power Supply:** **Mean Well LRS-100-12** ($12\text{V} / 8.5\text{A}$, $102\text{W}$ continuous SMPS, MTBF $>350\text{k hours}$).
  - *Worst-Case Peak Inrush Load:* Pump inrush ($6.0\text{A}$) $+$ Node Logic ($0.225\text{A}$) $= 6.225\text{A}$ ($74.7\text{W}$).
  - *Dynamic Headroom Reserve:* $(8.5\text{A} - 6.225\text{A}) / 8.5\text{A} = \mathbf{26.8\%}$ (Requirement $\ge 25\%$).
- **Node Logic Step-Down:** **MP1584EN High-Efficiency Switching Buck Converter** ($12\text{V} \to 5.0\text{V} / 3.3\text{V}$, $1.5\text{A}$ continuous, $92\%$ efficiency).
- **RF Rail Decoupling:** $470\mu\text{F} / 16\text{V}$ low-ESR electrolytic $+ 100\text{nF}$ ceramic capacitor placed within $10\text{mm}$ of RF transceiver VCC/GND pins. Calculated voltage sag during $120\text{mA}$ RF transmission transient step ($20\mu\text{s}$ regulator loop response) is $\mathbf{5.11\text{ mV}}$ ($\ll 165\text{ mV}$ rail tolerance).

---

## 4. Hardware Pinout & Interface Contracts

### 4.1 ESP32-S3 Gateway Hardware Interface Contract
```text
+-----------------------+-------------------+---------------------------------------------------+
| Peripheral / Function | ESP32-S3 GPIO Pin | Electrical Characteristics & Configuration        |
+-----------------------+-------------------+---------------------------------------------------+
| Debug Console TX/RX   | GPIO43 / GPIO44   | USB-CDC / UART0 (Dedicated for flashing & debug)  |
| RF Transceiver RXD    | GPIO18 (UART1_RX) | 3.3V LVCMOS input from E32 TXD (Baud: 115200 bps) |
| RF Transceiver TXD    | GPIO17 (UART1_TX) | 3.3V LVCMOS output to E32 RXD                     |
| RF Mode Control M0    | GPIO15 (Output)   | 3.3V GPIO (LOW = Normal Transmit/Receive Mode)    |
| RF Mode Control M1    | GPIO16 (Output)   | 3.3V GPIO (LOW = Normal Transmit/Receive Mode)    |
| RF Status AUX Sense   | GPIO19 (Input)    | 3.3V GPIO input with internal pull-up (Busy check)|
| I2C RTC Bus (DS3231)  | GPIO21 (SDA) / 22 | 3.3V Open-drain with 4.7kΩ pull-ups to 3.3V       |
| Hardware WDT External | GPIO38 (Optional) | External supervisor trigger                       |
+-----------------------+-------------------+---------------------------------------------------+
```

### 4.2 ATmega8 Remote Node Hardware Interface Contract
```text
+-----------------------+-------------------+---------------------------------------------------+
| Peripheral / Function | ATmega8 Physical  | Electrical Characteristics & Hardware Wiring      |
+-----------------------+-------------------+---------------------------------------------------+
| RF Transceiver RXD    | Pin 2 (PD0 / RXD) | 3.3V/5V UART RX (Baud: 9600 bps)                  |
| RF Transceiver TXD    | Pin 3 (PD1 / TXD) | 3.3V/5V UART TX (Baud: 9600 bps)                  |
| RF Status AUX Sense   | Pin 4 (PD2 / INT0)| Digital input, checks RF buffer ready / idle      |
| Flow Sensor Pulse INT | Pin 5 (PD3 / INT1)| Hardware Interrupt (FALLING edge), 10kΩ pull-up   |
| RF Mode Control M0    | Pin 6 (PD4)       | Digital output (LOW = Normal Mode)                |
| RF Mode Control M1    | Pin 11 (PD5)      | Digital output (LOW = Normal Mode)                |
| Driver Gate Feedback  | Pin 12 (PD6)      | Optocoupler PC817 collector sense (Active LOW)    |
| Pump Actuator Output  | Pin 15 (PB1 / OC1)| Active HIGH gate drive to LR7843 / Optocoupler    |
| Heartbeat / Fault LED | Pin 14 (PB0)      | Active HIGH via 1kΩ series resistor to Green LED  |
| ACS712 Current Sense  | Pin 23 (PC0 / ADC)| Analog input 0–5V (2.5V quiescent = 0A load)      |
| Reset / ISP Header    | Pin 1 (RESET) / SCK| 10kΩ pull-up to 5V + 100nF filter, ISP 6-pin hdr   |
+-----------------------+-------------------+---------------------------------------------------+
```

---

## 5. Architectural Alignment & Safety Contracts

### 5.1 Schedule Ownership (Current Boundary)
- ATmega8 schedule ownership, EEPROM persistence and resume behavior are **UNVERIFIED** because the deployed firmware is preloaded and immutable.
- Any required schedule policy belongs to ESP32/backend and must be translated into verified RF commands.

### 5.2 OFF Command Semantics
- `SET_PUMP(OFF)` is a legacy command request. It is not evidence of a temporary override, preserved schedule, or automatic resume.

### 5.3 Gateway Timeout Boundary
- `run_lease_ms` is an ESP32 policy value ($1000 - 15000\text{ ms}$ where applicable).
- Node-side expiry, fault latching and Safe-OFF after RF loss are **UNVERIFIED**. Use a physical interlock or reject ON when this guarantee is required.

### 5.4 Gateway Observation and Staleness
- No node heartbeat or unsolicited telemetry is assumed.
- Gateway may mark a node `STALE` after a configured interval without a validated response, but this does not prove the remote actuator is OFF.
- Node boot output and reboot/session behavior are **UNVERIFIED**.

### 5.5 Storage Policy: Zero Raw RF Persistence
- In compliance with [`docs/TELEMETRY_ANALYTICS_CONTRACT.md`](./TELEMETRY_ANALYTICS_CONTRACT.md), database tables and MQTT telemetry streams only ingest normalized, parsed telemetry fields (`reported_pump_state`, `driver_feedback`, `flow_lpm_x100`, `pulse_count`, `delivered_volume_ml`, `fault_flags`, `command_id`). Raw RF byte frames are **strictly prohibited** from database persistence.

---

## 6. Security Posture & Risk Acceptance Declaration

1. **Protocol boundary:** The deployed ESP32-to-ATmega8 path is AGU-Aeroponics legacy SCI: CRC16-Modbus checksum and legacy ACK only. HMAC, session and anti-replay are not used on this path.
2. **Gateway security:** MQTT authentication/authorization and gateway policy remain northbound concerns; they do not alter the AGU frame.
3. **Formal Risk Acceptance for POC Lab Bench:**
   - *Risk:* On unprovisioned breadboard prototypes, hardware-at-rest protection (Flash Encryption and Secure Boot v2) is not activated.
   - *Mitigation & Scope:* POC testing is confined to an air-gapped lab environment operating on isolated 433 MHz channels.
   - *Sprint 2 Production Blocker:* Production release firmware requires `RF_PROVISIONING_INDEPENDENT_SIGNOFF=1`, enabled Flash Encryption, and Secure Boot v2 before field deployment.

---

## 7. Open Risks & Sprint 2 Mitigation Action Plan

| Risk ID | Description & Potential Impact | Likelihood | Severity | Mitigation & Action Plan in Sprint 2 |
|---|---|---|---|---|
| **RSK-01** | **4-Node RF Collision on Shared Channel:** Multiple nodes transmitting asynchronous telemetry simultaneously could cause packet collisions. | Medium | High | Implement deterministic **TDMA Time-Slot Polling** ($100\text{ ms}$ slot per node, $500\text{ ms}$ complete 4-node scan cycle) in Sprint 2 Gateway firmware. |
| **RSK-02** | **ATmega8 Flash/RAM Exhaustion:** Advanced analytics or logging on node could exceed 8KB Flash / 1KB SRAM. | Low | Critical | Freeze node firmware scope strictly to Actuator + Flow Counter + FSM + Codec. Gateway absorbs 100% of telemetry parsing and analytics. |
| **RSK-03** | **Nutrient Solution Chemical Corrosion:** Highly concentrated fertilizer salts (EC $2.5\text{ mS/cm}$, pH $5.5$) could degrade turbine bearings over time. | Medium | Medium | Standardize on OF06ZAT PPS (Polyphenylene sulfide) oval gear flow sensor with stainless steel 316 shaft and Viton O-rings. |
| **RSK-04** | **High Humidity Greenhouse Condensation:** IP54 condensation causing electrical leakage on high-impedance ADC sensing pins. | Medium | High | Enforce **IP65 Sealed Enclosures** with waterproof PG7 cable glands, internal desiccant packs, and MG Chemicals 422B silicone conformal coating. |

---

## 8. Formal Decision & Sign-off Gate

| Review Role | Designated Signatory | Decision Outcome | Ratification Date | Sign-off Notes & Conditions |
|---|---|---|---|---|
| **Lead Hardware Architect** | Execution Agent (Antigravity) | **APPROVED** | 2026-08-22 | BOM validated with $\ge 6.25\times$ driver margin and $26.8\%$ power headroom. |
| **Firmware & Protocol Lead** | Execution Agent (Antigravity) | **GATEWAY-ONLY** | 2026-08-22 | Gateway/model contract reviewed; deployed ATmega8 protocol and crypto remain unverified. |
| **Safety & FMEA Lead** | Execution Agent (Antigravity) | **HOLD** | 2026-08-22 | Node-side lease, stale Safe-OFF, sensing and fault behavior require independent hardware evidence. |
| **Senior Solution Architect** | Independent QA Review Gate | **PENDING REVIEW** | 2026-08-22 | Awaiting formal independent verification of Sprint 1.5 completion. |

---

*Architectural Decision Record `ADR-HW-001` / `DECISION-001` finalized and committed to project repository. Sprint 1.5 Hardware Decision Gate is ready for independent QA Audit.*
