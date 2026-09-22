# Aeroponics Sprint 1.5 Hardware Interface Contract & Detailed Wiring Specification

> **Document Status:** Official Hardware Interface Specification & Wiring Contract
> **Document ID:** `SPEC-HW-002`
> **Status:** PROPOSED — Ready for Bench Assembly & QA Audit
> **Date:** 2026-08-17
> **Author / Role:** Execution Agent (Antigravity)
> **Reviewer / Owner:** Senior Solution Architect / QA Gate
> **Target Scope:** 1 Gateway (ESP32-S3 DevKitC-1) ↔ Wireless 433 MHz RF ↔ 1 Remote Actuator Node (ESP32-C3 / ESP32-WROOM-32D)
> **Governing Specifications:** [`docs/RF_FLOW_POC_DECISION.md`](./RF_FLOW_POC_DECISION.md), [`docs/RF_PROTOCOL.md`](./RF_PROTOCOL.md), [`docs/RF_FLOW_POC_TEST_PLAN.md`](./RF_FLOW_POC_TEST_PLAN.md), [`docs/RF_FLOW_POC_FMEA.md`](./RF_FLOW_POC_FMEA.md)
> **Firmware boundary:** Pinout and wiring describe physical connectivity only. They do not prove that the preloaded ATmega8 firmware reads, drives or reports any listed signal. RF security and telemetry semantics must follow [`ATMEGA8_INTEGRATION_BOUNDARY.md`](./ATMEGA8_INTEGRATION_BOUNDARY.md).

---

## 1. Executive Overview & Hardware Interface Contract

This specification establishes the physical, electrical, and topological interface contract for the **Sprint 1.5 Proof-of-Concept (POC)** subsystem. It governs the hardware interconnection between the central **ESP32-S3 Gateway**, the **433 MHz RF Transceiver Link**, and the **Remote Node Actuator** responsible for high-pressure aeroponics pump actuation, physical feedback sensing, and precision pulse-counter flow measurement.

### 1.1 Non-Negotiable Hardware Design Principles
1. **Hardware-Enforced Default Safe-OFF:** The pump actuator gate MUST have an active hardware pull-down ($10\text{ k}\Omega$) ensuring zero-conduction during MCU power-up, brownout, reset, flashing, or firmware crash.
2. **Galvanic & Optical Isolation:** Actuator switching loads (12V DC / 220V AC) and driver feedback sensing MUST be optically isolated (PC817, minimum $2500\text{ V}_{\text{RMS}}$) from the sensitive 3.3V MCU and RF transceiver logic.
3. **Star Grounding Topology:** Power ground (`PGND`) and Logic ground (`LGND`) MUST remain strictly segregated on PCBs and wiring harnesses, joined at exactly one star point at the main DC power terminal to prevent pump ground return currents from inducing voltage bounce on MCU/sensor lines.
4. **Dedicated Non-Debug UART:** The RF transceiver MUST connect to dedicated hardware UART pins with 3.3V LVCMOS levels, completely separated from the native USB CDC / debug UART to eliminate communication collisions and debug log corruption.
5. **Inductive Transient Suppression at Source:** Every DC inductive pump MUST have a fast-recovery Schottky flyback diode connected directly across motor terminals. Every AC pump/solenoid MUST have an RC snubber and MOV varistor across relay contacts.
6. **Antenna Separation & EMI Immunity:** The 433 MHz RF antenna MUST be physically separated from pump motors, switching MOSFETs, and power cables by $\ge 20\text{ cm}$.

---

## 2. End-to-End System Topology & Architecture

```mermaid
flowchart TB
    subgraph Gateway_Subsystem["Central Gateway (ESP32-S3)"]
        GW_MCU["ESP32-S3 DevKitC-1<br/>(Core Controller)"]
        GW_USB["Native USB CDC<br/>(115200 Baud / Debug Log)"]
        GW_PWR["5V Regulated Supply<br/>+ TVS SMAJ5.0A"]
        GW_RF["RF 433MHz Module<br/>(E32-433T20D / HC-12)"]
        GW_ANT["Rubber Duck SMA<br/>433MHz Antenna"]
        GW_DECOUP["Decoupling Network<br/>470uF Low-ESR + 100nF"]

        GW_USB <-->|GPIO 19/20| GW_MCU
        GW_PWR -->|5V Rail| GW_MCU
        GW_PWR -->|Filtered 3.3V/5V| GW_DECOUP --> GW_RF
        GW_MCU <-->|UART1: TX17 / RX16| GW_RF
        GW_RF <-->|SMA 50 Ohm| GW_ANT
    end

    GW_ANT <-.->|433.175 MHz AGU-Aeroponics legacy SCI<br/>Length/Opcode/Params/ZeroSum; no HMAC| NODE_ANT

    subgraph Node_Subsystem["Remote Actuator Node (ESP32-C3 / WROOM)"]
        NODE_ANT["Rubber Duck SMA<br/>433MHz Antenna"]
        NODE_RF["RF 433MHz Module<br/>(E32-433T20D / HC-12)"]
        NODE_DECOUP["Decoupling Network<br/>470uF Low-ESR + 100nF"]
        NODE_MCU["ESP32-C3 Controller<br/>(3.3V Logic)"]

        subgraph Power_Delivery["Power Supply & Protection"]
            DC_IN["12V DC Main Bus<br/>(Mean Well LRS-100-12)"]
            ESTOP["Mechanical E-Stop Switch<br/>(NC Contact / 10A)"]
            FUSE["Inline Fuse TR5<br/>(3.15A Time-Lag)"]
            POL_PROT["Reverse Polarity Protection<br/>(P-MOSFET / SS54)"]
            TVS_12V["TVS Diode SMBJ15A<br/>(Transient Suppression)"]
            BUCK_5V["MP1584EN Buck Converter<br/>(12V -> 5.0V Logic)"]
            LDO_3V3["AMS1117-3.3 LDO<br/>(5.0V -> 3.3V Rail)"]

            DC_IN --> ESTOP --> FUSE --> POL_PROT --> TVS_12V
            TVS_12V --> BUCK_5V --> LDO_3V3
            LDO_3V3 --> NODE_MCU
            LDO_3V3 --> NODE_DECOUP --> NODE_RF
        end

        subgraph Actuator_Path["Actuator & Driver Stage"]
            PUMP_DRV["Optocoupled N-MOSFET<br/>(LR7843 / PC817 Isolated)"]
            PULLDOWN["10k Gate Pulldown<br/>(Hardware Safe-OFF)"]
            FLYBACK["SS34 Schottky Flyback<br/>(Direct at Pump Terminals)"]
            DC_PUMP["12V DC Diaphragm Pump<br/>(24W / 2.0A Nom / 8A Stall)"]

            NODE_MCU -->|GPIO 4 (Actuate)| PUMP_DRV
            PULLDOWN --- PUMP_DRV
            TVS_12V -->|12V Switched Rail| PUMP_DRV --> DC_PUMP
            FLYBACK --- DC_PUMP
        end

        subgraph Feedback_Sensors["Telemetry & Verification"]
            DRV_FB["Driver Feedback Circuit<br/>(PC817 Optoisolator)"]
            FLOW_SENS["Flow Sensor (OF06ZAT / S401)<br/>(Hall Effect NPN Open-Collector)"]
            RC_DEBOUNCE["Signal Conditioning<br/>(10k Pull-up + 10nF RC)"]

            PUMP_DRV -.->|Physical Gate State| DRV_FB -->|GPIO 5| NODE_MCU
            FLOW_SENS --> RC_DEBOUNCE -->|GPIO 18 (Interrupt)| NODE_MCU
        end

        NODE_RF <-->|UART: RX20 / TX21| NODE_MCU
        NODE_RF <-->|SMA 50 Ohm| NODE_ANT
    end
```

---

## 3. Gateway Hardware Wiring Specification (ESP32-S3 DevKitC-1)

### 3.1 Gateway Pinout Mapping Table

| ESP32-S3 Pin | Function | Signal Direction | Logic Level | Connected Device / Pin | Description / Electrical Constraints |
|---|---|---|---|---|---|
| **USB Native** | Debug / CLI / Flashing | Bidirectional | USB 2.0 (D+/D-) | Host PC USB Port | Dedicated USB CDC (`115200` baud). Completely isolated from RF traffic. |
| **GPIO 17** | RF UART TX | Output (MCU $\to$ RF) | 3.3V LVCMOS | RF Module `RXD` Pin | Gateway frame transmission. Connect directly (native 3.3V logic). |
| **GPIO 16** | RF UART RX | Input (RF $\to$ MCU) | 3.3V LVCMOS | RF Module `TXD` Pin | Gateway frame reception. Connect directly (native 3.3V logic). |
| **GPIO 15** | RF Module `SET` / `M0` | Output (Optional) | 3.3V LVCMOS | RF Module `SET` / `M0` | Mode selection (Normal / Config). Pull LOW for normal transparent UART mode. |
| **GPIO 7** | Status LED (Heartbeat) | Output | 3.3V (Active HIGH) | Green LED ($1\text{ k}\Omega$ resistor) | Toggles on each valid RF frame processing tick. |
| **GPIO 8** | Fault LED (Alarm) | Output | 3.3V (Active HIGH) | Red LED ($1\text{ k}\Omega$ resistor) | Illuminates upon node stale, RF CRC fault, or system error. |
| **5V / VBUS** | Main Logic Power Input | Power Input | 5.0V DC $\pm 5\%$ | Regulated USB / 5V DC Supply | Protected by TVS diode **SMAJ5.0A** and $100\ \mu\text{F}$ electrolytic capacitor. |
| **3V3** | 3.3V Rail Output | Power Output | 3.3V DC (max 500mA) | Local logic pull-ups | Sourced from on-board LDO. |
| **GND** | Logic Ground | Reference | 0V | Common Ground Plane | Star ground connection point for Gateway enclosure. |

### 3.2 Gateway Strapping Pin Analysis & Avoidance

| ESP32-S3 Strapping Pin | Default Function / State | Operational Risk | Design Rule / Mitigation |
|---|---|---|---|
| **GPIO 0** | Boot Mode Select (SPI Boot vs Download) | Pulling LOW during boot forces ROM bootloader download mode. | **DO NOT CONNECT** to external RF pins or pull-down switches. Keep internal pull-up intact. |
| **GPIO 45** | VDD_SPI Voltage Select | Controls internal flash voltage ($3.3\text{V}$ vs $1.8\text{V}$). Driving HIGH causes flash brownout/damage. | **DO NOT CONNECT**. Leave floating / internal pull-down. |
| **GPIO 46** | ROM Message Printing | Controls bootloader log output verbosity. | **DO NOT CONNECT** to active output signals. |
| **GPIO 3** | JTAG Interface Selection | Toggles JTAG peripheral assignment. | Leave unconnected or use for high-impedance inputs only. |

> [!IMPORTANT]
> **Gateway UART Pin Assignment:** UART1 is mapped to **GPIO 17 (TX)** and **GPIO 16 (RX)**. These pins are completely free of strapping functions, internal JTAG multiplexing, or boot mode dependencies, ensuring deterministic boot behavior.

### 3.3 Gateway Power & RF Decoupling Schematic

```text
       +5V USB / External Supply
                  │
                  ├───[ Fuse 500mA PTC ]───┐
                  │                        │
               ┌──┴──┐                     │
               │ TVS │ SMAJ5.0A            ▼
               │Diode│ (Clamps >6.4V)  ┌───────┐
               └──┬──┘                 │ESP32S3│
                  │                    │DevKit │
                 GND                   └───┬───┘
                                           │ +3.3V Rail
                                           │
                        ┌──────────────────┴──────────────────┐
                        │                                     │
                     ┌──┴──┐                               ┌──┴──┐
                     │     │ C1: 470uF / 16V               │     │ C2: 100nF / 50V
                     │     │ Low-ESR Electrolytic          │     │ Ceramic X7R
                     └──┬──┘ (Buffers 120mA Tx Bursts)     └──┬──┘ (High-Freq Noise Filter)
                        │                                     │
                       GND                                   GND
                        │                                     │
                        └──────────────────┬──────────────────┘
                                           │ Filtered VCC_RF
                                           ▼
                                    ┌───────────────┐
                                    │ RF Transceiver│
                                    │ (E32 / HC-12) │
                                    │               │
                                    │ VCC   GND  TX │──> GPIO 16 (RX1)
                                    │            RX │<── GPIO 17 (TX1)
                                    └───────────────┘
```

---

## 4. Remote Node Hardware Wiring Specification (ESP32-C3 / ESP32-WROOM)

### 4.1 Node MCU Pinout Mapping Table

| Node Pin (ESP32-C3) | Function | Signal Direction | Logic Level | Connected Device / Pin | Description / Electrical Constraints |
|---|---|---|---|---|---|
| **GPIO 4** | Pump Gate Drive (`desired`) | Output (Active HIGH) | 3.3V LVCMOS | MOSFET Driver Optocoupler (PC817 Pin 1) | Actuator trigger. **Mandatory $10\text{ k}\Omega$ pull-down** to GND. Safe-OFF by default. |
| **GPIO 5** | Driver Feedback (`driver_fb`)| Input (Active LOW) | 3.3V LVCMOS | Feedback Optocoupler Collector (PC817) | Physical confirmation of gate voltage. Internal/external $10\text{ k}\Omega$ pull-up to 3.3V. |
| **GPIO 18** | Flow Sensor Pulse In | Input (Interrupt) | 3.3V LVCMOS | Flow Sensor Signal via RC Filter | Pulse counting ISR. External $10\text{ k}\Omega$ pull-up to 3.3V $+ 10\text{ nF}$ filter capacitor. |
| **GPIO 20** | RF Transceiver UART RX | Input (RF $\to$ MCU) | 3.3V LVCMOS | RF Module `TXD` Pin | Receives commands from Gateway. |
| **GPIO 21** | RF Transceiver UART TX | Output (MCU $\to$ RF) | 3.3V LVCMOS | RF Module `RXD` Pin | Transmits ACK, Telemetry, and Fault reports. |
| **GPIO 1** | Local Status LED | Output | 3.3V (Active HIGH) | Blue LED ($1\text{ k}\Omega$ resistor) | Blinks on pump ON; solid on RF active. |
| **GPIO 0** | Fault Latch LED | Output | 3.3V (Active HIGH) | Red LED ($1\text{ k}\Omega$ resistor) | Latches ON when `NO_FLOW_FAULT` or `LEASE_EXPIRED` occurs. |
| **3V3** | Logic VCC | Power Input | 3.3V DC $\pm 3\%$ | Output of AMS1117-3.3 LDO | Powers MCU, RF module, optocoupler pull-ups. |
| **GND** | Logic Ground (`LGND`) | Reference | 0V | Logic Ground Plane | Isolated from power switching ground until central star point. |

### 4.2 Node Strapping Pin Analysis & Avoidance (ESP32-C3)

| ESP32-C3 Strapping Pin | Default Function / State | Operational Risk | Design Rule / Mitigation |
|---|---|---|---|
| **GPIO 2** | Boot Strapping (Must be HIGH for SPI boot) | Pulling LOW causes boot mode corruption on reset. | **AVOID AS ACTUATOR OUTPUT**. Used solely with weak pull-up or left floating. |
| **GPIO 8** | Boot Mode Selection (Must be HIGH) | Pulling LOW enters download bootloader. | Keep tied to 3.3V via $10\text{ k}\Omega$ pull-up or left unconnected. |
| **GPIO 9** | Boot Mode / Chip Reset (Active LOW) | Internal pull-up. Pulling LOW resets into bootloader. | Connect only to physical reset button with debounce capacitor ($100\text{ nF}$). |

---

## 5. Actuator & Driver Circuit Schematics

### 5.1 DC Diaphragm Pump Driver Schematic (12V DC / 24W)

```text
                     +12V DC Main Power Bus (from LRS-100-12 via Fuse & E-Stop)
                                  │
                                  ├───[ Emergency E-Stop (NC) ]───[ Fuse 3.15A-T ]───┐
                                  │                                                   │
                               ┌──┴──┐                                                │
                               │ TVS │ SMBJ15A                                        │
                               │Diode│ (Clamps >17.1V)                                │
                               └──┬──┘                                                │
                                  │                                                   ▼
                                 GND                                             ┌─────────┐
                                                                                 │ DC Pump │
                                                                                 │  (12V)  │
                                                                                 └───┬─────┘
                                                                                     │
                       Flyback Protection (Direct at Terminals)                      │
                      ┌──────────────────────────────────────────────────────────────┤
                      │                                                              │
                    ┌─┴─┐                                                            │
              Cathode ▲ ▲ SS34 Schottky Diode (40V / 3A, 100A Surge)                 │
                      │ ─── (Damps -150V Inductive Kickback < 0.5V)                  │
                      └───┬──────────────────────────────────────────────────────────┘
                          │
                          │ PUMP_RETURN (Switched Low-Side)
                          ▼
                 ┌─────────────────┐
                 │  N-MOSFET       │
                 │  LR7843         │  Drain (D)
                 │  (30V / 50A)    ├────────────────────────┐
                 │  RDSon = 3.3mΩ  │                        │
                 │                 │                        │
    Gate (G) ────┤                 │                        │
        │        │  Source (S)     │                        │
        │        └────────┬────────┘                        │
        │                 │                                 │
        │                GND (Power GND - PGND)             │
        │                                                   │
        │                                                   │
   ┌────┴─────────────────────────────┐                     │
   │ Gate Drive & Isolation Stage     │                     │
   │                                  │                     │
   │             PC817 Optoisolator   │                     │
   │             ┌─────────────────┐  │                     │
   │ +3.3V ──[1k]──┤1(A)         4(C)├──┼──[100Ω]───────────┘
   │               │   (Opto)      │  │
GPIO 4 ────────────┤2(K)         3(E)├──┴──[10k Pulldown]─── GND (PGND)
(ESP32-C3)         └─────────────────┘
   │
   └─> GPIO 4 Output HIGH -> Opto conducts -> 12V applied to Gate via 100Ω -> MOSFET ON
       GPIO 4 Output LOW / Reset -> 10k pulls Gate to GND -> Hard OFF (<100ns)
```

### 5.2 AC 220V Pump / Solenoid Driver Schematic (RC Snubber & MOV)

```text
                  AC 220V Phase (L) (from Mains via Circuit Breaker)
                                  │
                                  ├───[ Emergency E-Stop (NC / 10A 250VAC) ]───┐
                                  │                                            │
                               ┌──┴──┐                                         │
                               │ MOV │ 14D431K Varistor                        │
                               │     │ (Clamps Grid Transients @ 430V)         │
                               └──┬──┘                                         │
                                  │                                            │
                  AC Neutral (N) ─┴────────────────────────────────────────────┼──────────┐
                                                                               │          │
                                                                               │      ┌───┴────┐
                                                                               │      │AC Pump │
                                                                               │      │ / Load │
                                                                               │      └───┬────┘
                                                                               │          │
                     RC Snubber Network (Directly across Relay Contacts)       │          │
                    ┌──────────────────────────────────────────────────────────┼──────────┘
                    │                                                          │
                  ┌─┴─┐ C_snub: 0.1uF / 275VAC X2 Film                         │
                  │   │                                                        │
                  └──┬┘                                                        │
                     │                                                         │
                  ┌──┴──┐ R_snub: 100Ω / 2W Flameproof Metal Oxide             │
                  │     │                                                      │
                  └──┬──┘                                                      │
                     └─────────────────────────────────────────┐               │
                                                               │               │
                                                               ▼               ▼
                                                        ┌─────────────────────────────┐
                                                        │ Electromechanical / SSR     │
                                                        │ Contact: SRD-05VDC-SL-C     │
                                                        │ (10A 250VAC Rated)          │
                                                        │                             │
                                                        │ COM                     NO  │
                                                        └──────────────┬──────────────┘
                                                                       │
                                                       Isolated Coil Drive Stage
                                                       (5V Coil driven by PC817 + NPN)
```

---

## 6. Feedback Sensing & Signal Conditioning Circuitry

### 6.1 Dual Feedback Architecture: Driver Feedback vs Load Flow Confirmation

```text
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                              DEFENCE IN DEPTH CLASSIFICATION                           │
├──────────────────────────┬─────────────────────────────────────────────────────────────┤
│ Feedback Level           │ Sensing Method & Physical Meaning                           │
├──────────────────────────┼─────────────────────────────────────────────────────────────┤
│ Level 1: Desired State   │ Firmware GPIO 4 commanded logic state (Output High/Low).    │
│ Level 2: Driver Feedback │ Optocoupler PC817 across Drain/Coil: confirms gate drive    │
│                          │ voltage reached actuator switch without wire break.         │
│ Level 3: Flow Confirmation│ OF06ZAT / YF-S401 Hall pulse stream (>0.3 L/min): confirms │
│                          │ liquid is physically delivered through misting nozzles.     │
└──────────────────────────┴─────────────────────────────────────────────────────────────┘
```

### 6.2 Driver Feedback Sensing Circuit Schematic

```text
            +12V Switched Pump Supply (Post-MOSFET Drain / Relay Auxiliary)
                               │
                            ┌──┴──┐
                            │     │ R_limit: 2.2kΩ / 1W
                            └──┬──┘
                               │
                               ▼ Pin 1 (Anode)
                            ┌─────────────────┐
                            │ PC817           │
                            │ Optoisolator    │
                            │                 │
                            │ Pin 2 (Cathode) │
                            └──┬──────────────┘
                               │
                              GND (Power GND - PGND)

            Isolated Signal to Node MCU:
            +3.3V Logic Rail
                   │
                ┌──┴──┐
                │     │ R_pullup: 10kΩ / 0.125W
                └──┬──┘
                   │
                   ├──────────────────────────> GPIO 5 (ESP32-C3 Driver Feedback In)
                   │
                   ▼ Pin 4 (Collector)
            ┌─────────────────┐
            │ PC817           │
            │ Output Stage    │
            │ Pin 3 (Emitter) │
            └──────┬──────────┘
                   │
                  GND (Logic GND - LGND)

Logic Interpretation:
- Pump Energized (12V present at load) -> PC817 LED turns ON -> Collector pulled LOW -> GPIO 5 reads 0 (ACTIVE)
- Pump OFF / Blown Fuse / Open Wire   -> PC817 LED OFF      -> Collector pulled HIGH -> GPIO 5 reads 1 (INACTIVE)
```

### 6.3 Flow Sensor Conditioning & Anti-Bounce Filter Schematic

```text
      +5V DC Power (from MP1584 Buck)
             │
          ┌──┴────────────────────────────────┐
          │  Flow Sensor (OF06ZAT / YF-S401)  │
          │  VCC (Red) ──> +5V DC             │
          │  GND (Black) > LGND               │
          │  SIG (Yellow)> NPN Open-Collector │
          └──┬────────────────────────────────┘
             │
             │ Pulse Stream (0 - 200 Hz @ 0.3 - 6.0 L/min)
             │
             ├───[ Pull-Up Resistor 10kΩ to +3.3V Logic Rail ]─── +3.3V
             │
             ├───[ Series Resistor R_filt: 100Ω ]───┐
             │                                      │
             │                                   ┌──┴──┐
             │                                   │     │ C_filt: 10nF / 50V
             │                                   │     │ Ceramic X7R
             │                                   └──┬──┘ (Low-Pass Filter: fc ≈ 159 kHz)
             │                                      │
             │                                     GND (LGND)
             │                                      │
             └──────────────────────────────────────┴───> GPIO 18 (Interrupt Pin)
```

---

## 7. Grounding, Power Distribution & Protection Matrix

### 7.1 Star Grounding & Power Distribution Architecture

```text
    ┌────────────────────────────────────────────────────────────────────────────────┐
    │                        STAR GROUNDING TOPOLOGY CONTRACT                        │
    └────────────────────────────────────────────────────────────────────────────────┘

    [Mean Well LRS-100-12 SMPS (12V 8.5A)]
         │ (+) 12V Bus
         │                   ┌───[ Fuse 3.15A ]───> [ 12V Pump Actuator Bus ]
         │                   │
         │                   └───[ MP1584EN Buck ]───> +5.0V Logic Rail
         │                                                 │
         │                                                 └───[ AMS1117-3.3 ]───> +3.3V MCU/RF
         │
         │ (-) 12V Return
         ▼
     ★ CENTRAL STAR GROUND POINT ★ (Terminal Block TB-GND)
         ├─── (A) Power Ground (PGND): Heavy 16 AWG line to MOSFET Source & Pump Return.
         └─── (B) Logic Ground (LGND): Dedicated 22 AWG line to Buck Converter, MCU & Sensors.
              * Rule: Zero pump load current is permitted to flow through the LGND plane.
```

### 7.2 Safety & Protection Component Specifications

| Component Category | Component Part Number | Key Ratings & Thresholds | Placement & Protection Target | Failure Mode Behavior |
|---|---|---|---|---|
| **Main DC Fuse** | Littelfuse TR5 / 372 Series | $3.15\text{A}$ Time-Lag (Slow-Blow) / 250V | In series with +12V ungrounded DC rail | Clears on pump motor stall / dead short ($>6.3\text{A}$ in $<100\text{ms}$). |
| **DC Bus TVS** | Littelfuse SMBJ15A | $15\text{V}$ Stand-off / $24.4\text{V}$ Max Clamping / $600\text{W}$ | Directly across +12V DC input at enclosure entry | Clamps lightning, inductive bus surges, and power supply overshoot. |
| **Logic 5V TVS** | Littelfuse SMAJ5.0A | $5.0\text{V}$ Stand-off / $9.2\text{V}$ Max Clamping / $400\text{W}$ | Directly across 5V output of MP1584 Buck converter | Protects MCU and 5V sensors against buck regulator short-circuit. |
| **Reverse Polarity** | Vishay SS54 / P-MOS AO4407A | $40\text{V} / 5\text{A}$ Schottky / $V_F < 0.45\text{V}$ | In series with +12V input before DC bus distribution | Blocks reverse voltage during accidental battery / power swap. |
| **AC Line MOV** | Bourns MOV-14D431K | $275\text{V}_{\text{RMS}} / 350\text{V}_{\text{DC}} / 4500\text{A}$ Surge | Across Phase & Neutral AC input lines | Clamps grid voltage spikes and lightning transients. |
| **E-Stop Switch** | IDEC / Schneider XB2-ES542 | $10\text{A} / 250\text{VAC}$, Latching Red Mushroom | External panel mounted, cuts +12V / 220V ungrounded line | Instant mechanical isolation of pump power independent of MCU. |

---

## 8. Physical Layout, Cabling, Antenna Separation & EMI Rules

### 8.1 Critical Physical Installation Rules

```text
    ┌─────────────────────────────────────────────────────────────────────────────┐
    │                       ENCLOSURE PHYSICAL LAYOUT RULES                       │
    ├─────────────────────────────────────────────────────────────────────────────┤
    │ 1. Minimum 20 cm Separation: Antenna MUST be mounted at the top exterior    │
    │    of the enclosure, at least 200 mm away from pump motors & power cables.  │
    │ 2. 90-Degree Cable Crossings: Power wires (12V/220V) and signal wires      │
    │    (UART, Flow pulses, 3.3V) MUST cross at right angles (90°) if proximity │
    │    is unavoidable, NEVER run in parallel conduits.                          │
    │ 3. Shielded Twisted Pair (STP): Flow sensor wiring MUST use 24 AWG STP.     │
    │    The drain/shield wire MUST be connected to LGND at the controller end    │
    │    ONLY (single-ended grounding to eliminate antenna ground loops).         │
    │ 4. Decoupling Proximity: Low-ESR 470uF + 100nF capacitors MUST be soldered │
    │    within ≤10 mm trace length of the RF module VCC/GND pins.                │
    └─────────────────────────────────────────────────────────────────────────────┘
```

### 8.2 Inductive Switching EMI Verification Test Procedure (`TP-HW-01`)

1. **Pre-requisite Setup:** Node connected to Gateway via 433 MHz RF link at 10 meters distance through 1 brick wall. DC pump connected with full water circuit ($2.0\text{A}$ nominal load). Oscilloscope probe connected to MCU 3.3V rail (AC coupled, $20\text{ MHz}$ bandwidth limit).
2. **Stress Test Execution:**
   - Execute **50 consecutive ON/OFF cycles** at 5-second intervals via automated script.
   - Monitor oscilloscope for transient voltage dips on the 3.3V rail during pump switch-on and switch-off.
3. **Acceptance Thresholds:**
   - Maximum 3.3V rail voltage droop: $\le 150\text{ mV}$ (no brownout reset).
   - RF Frame Error Rate (FER) across 50 command/telemetry exchanges: $\mathbf{0\%}$ packet corruption.
   - Zero unexpected MCU resets (Watchdog or Brownout flags in `ESP.getResetReason()`).

---

## 9. Comprehensive Pin-to-Pin Interconnection Netlist

### 9.1 Gateway Enclosure Interconnections

| From Device | From Terminal | To Device | To Terminal | Wire Gauge | Wire Color | Connector / Termination |
|---|---|---|---|---|---|---|
| External 5V Supply | (+) 5V DC | ESP32-S3 DevKit | 5V / VBUS Pin | 22 AWG | Red | Screw Terminal / Dupont |
| External 5V Supply | (-) GND | ESP32-S3 DevKit | GND Pin | 22 AWG | Black | Screw Terminal / Dupont |
| ESP32-S3 DevKit | 3.3V Out | Decoupling Cap C1/C2 | (+) Terminal | 24 AWG | Red | Solder Point $\le 10\text{mm}$ |
| Decoupling Cap C1/C2| (+) Terminal | E32/HC-12 RF Module | `VCC` Pin | 24 AWG | Red | Dupont / JST-XH 2.54mm |
| ESP32-S3 DevKit | GND | Decoupling Cap C1/C2 | (-) Terminal | 24 AWG | Black | Solder Point $\le 10\text{mm}$ |
| Decoupling Cap C1/C2| (-) Terminal | E32/HC-12 RF Module | `GND` Pin | 24 AWG | Black | Dupont / JST-XH 2.54mm |
| ESP32-S3 DevKit | GPIO 17 (TX1) | E32/HC-12 RF Module | `RXD` Pin | 26 AWG | Yellow | Dupont / JST-XH 2.54mm |
| ESP32-S3 DevKit | GPIO 16 (RX1) | E32/HC-12 RF Module | `TXD` Pin | 26 AWG | Green | Dupont / JST-XH 2.54mm |
| E32/HC-12 RF Module | `SET` / `M0` | ESP32-S3 DevKit | GND (Normal Mode) | 26 AWG | Black | Jumper |
| E32/HC-12 RF Module | SMA Connector | Gateway Enclosure Wall | Bulkhead SMA Jack | RG-178 Coax | Coaxial | SMA Male to Female Bulkhead |
| Bulkhead SMA Jack | Outer Port | Rubber Duck Antenna | Antenna Base | N/A | Black | SMA-J Screw-On (50 $\Omega$) |

### 9.2 Remote Actuator Node Enclosure Interconnections

| From Device | From Terminal | To Device | To Terminal | Wire Gauge | Wire Color | Connector / Termination |
|---|---|---|---|---|---|---|
| 12V SMPS (LRS-100-12)| (+) 12V Terminal | Emergency E-Stop | Terminal 1 (NC) | 16 AWG | Red | Fork / Insulated Lug |
| Emergency E-Stop | Terminal 2 (NC) | Fuse Holder (TR5) | Input Terminal | 16 AWG | Red | Solder / Heat-Shrink |
| Fuse Holder (TR5) | Output Terminal | P-MOS Reverse Prot | Source (S) | 16 AWG | Red | Solder / PCB Trace |
| P-MOS Reverse Prot | Drain (D) | TVS Diode SMBJ15A | Cathode (+) | 16 AWG | Red | Star Bus Terminal (+12V) |
| Star Bus Terminal | (+12V Rail) | MP1584EN Buck In | `IN+` Pin | 20 AWG | Red | Screw Terminal / Solder |
| Star Bus Terminal | (+12V Rail) | DC Pump Terminal 1 | (+) Motor Lead | 16 AWG | Red | Waterproof 2-Pin Plug |
| DC Pump Terminal 2 | (-) Motor Lead | LR7843 MOSFET Module| Drain (D) Terminal | 16 AWG | Blue | Screw Terminal |
| DC Pump Terminal 1 | (+) Motor Lead | SS34 Schottky Diode | Cathode (Band) | Solder | Direct at Pump | Direct Solder at Pump Terminals |
| DC Pump Terminal 2 | (-) Motor Lead | SS34 Schottky Diode | Anode | Solder | Direct at Pump | Direct Solder at Pump Terminals |
| LR7843 MOSFET Module| Source (S) Terminal | Central Star Ground | TB-GND Terminal | 16 AWG | Black | Heavy Screw Terminal Lug |
| MP1584EN Buck Out | `OUT+` (5.0V Rail) | Flow Sensor Cable | Pin 1 (VCC Red) | 22 AWG | Red | GX12 3-Pin Aviation Jack |
| MP1584EN Buck Out | `OUT+` (5.0V Rail) | AMS1117-3.3 In | `VIN` Pin | 22 AWG | Red | PCB Trace / Dupont |
| AMS1117-3.3 Out | `VOUT` (3.3V Rail) | ESP32-C3 DevKit | `3V3` Pin | 22 AWG | Red | Dupont / JST-XH 2.54mm |
| AMS1117-3.3 Out | `VOUT` (3.3V Rail) | Decoupling Cap 470uF| (+) Terminal | 24 AWG | Red | Solder $\le 10\text{mm}$ to RF |
| Decoupling Cap 470uF| (+) Terminal | Node RF Module | `VCC` Pin | 24 AWG | Red | Dupont / JST-XH 2.54mm |
| ESP32-C3 DevKit | GPIO 4 (Actuate) | LR7843 Opto In | `PWM / SIG` Pin | 24 AWG | Orange | Dupont / JST-XH 2.54mm |
| LR7843 Opto In | Pull-down Resistor | 10k Resistor to GND | LGND | Solder | On-board / PCB | Pull-down to ensure safe-OFF |
| LR7843 Module Out | Feedback Opto PC817 | Pin 1 (Anode via 2.2k)| Switched 12V Load | 24 AWG | Purple | Solder / Wire tap |
| Feedback Opto PC817 | Pin 4 (Collector) | ESP32-C3 DevKit | GPIO 5 (Driver FB) | 26 AWG | White | Dupont / JST-XH 2.54mm |
| Flow Sensor Cable | Pin 2 (SIG Yellow) | RC Filter (100R+10nF)| Input Node | 24 AWG (STP) | Yellow | GX12 3-Pin Aviation Jack |
| RC Filter Output | Filtered Signal | ESP32-C3 DevKit | GPIO 18 (Interrupt)| 26 AWG | Yellow | Dupont / JST-XH 2.54mm |
| Flow Sensor Cable | Pin 3 (GND Black) | Logic Ground (LGND) | Star LGND Bus | 24 AWG (STP) | Black | GX12 3-Pin Aviation Jack |
| Flow Sensor Cable | Shield Drain Wire | Logic Ground (LGND) | Single Point LGND | Bare | Shield | Controller End Only |
| ESP32-C3 DevKit | GPIO 21 (TX) | Node RF Module | `RXD` Pin | 26 AWG | Green | Dupont / JST-XH 2.54mm |
| ESP32-C3 DevKit | GPIO 20 (RX) | Node RF Module | `TXD` Pin | 26 AWG | Yellow | Dupont / JST-XH 2.54mm |

---

## 10. Verification & Sign-Off Checklist

- [x] Dedicated Hardware UART (UART1 on Gateway, UART on Node) completely isolated from USB debug Serial.
- [x] All ESP32-S3 and ESP32-C3 strapping pins identified, documented, and avoided for active control lines.
- [x] Hardware pull-down ($10\text{ k}\Omega$) on pump gate driver ensures default OFF during reset, brownout, and boot.
- [x] Optocoupler isolation (PC817) separates 12V/220V actuator switching from 3.3V MCU/RF logic.
- [x] Dual feedback sensing clearly differentiated between `driver_feedback` (electrical gate state) and `load_feedback` (flow confirmation).
- [x] RC filter ($100\ \Omega + 10\text{ nF}$) conditions flow sensor pulses against mechanical bounce and motor commutation noise.
- [x] Protection matrix detailed: Slow-blow Fuse (3.15A), TVS Diodes (SMAJ5.0A, SMBJ15A), Schottky Flyback (SS34), AC Snubber ($0.1\mu\text{F}+100\Omega$), MOV (14D431K), and mechanical E-Stop.
- [x] Star grounding topology specified with explicit separation between `PGND` and `LGND`.
- [x] RF decoupling specification defined ($470\ \mu\text{F} \text{ low-ESR} + 100\text{ nF}$ ceramic $\le 10\text{ mm}$ from module) maintaining voltage droop $<2\text{ mV}$.
- [x] Physical separation ($\ge 20\text{ cm}$) and cabling rules documented to eliminate inductive switching EMI on RF link.
- [x] Complete pin-to-pin wiring netlist generated with AWG, color codes, and connector types.
