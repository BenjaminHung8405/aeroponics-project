# Aeroponics Sprint 1.5 Hardware Wiring & Interface Contract

> **Document Status:** Official Hardware Interface Specification  
> **Scope:** Gateway & Node Pinout, Electrical Protection, EMI Decoupling & Isolation

---

## 1. Gateway Hardware Wiring
- **MCU:** ESP32-S3 DevKitC-1
- **USB Debug Serial:** Native USB UART (`115200` baud) for development and monitoring.
- **RF Transceiver UART Interface (Isolated):**
  - `CONFIG_RF_UART_NUM`: UART 1
  - `CONFIG_RF_UART_TX_PIN`: GPIO 17
  - `CONFIG_RF_UART_RX_PIN`: GPIO 16
  - `CONFIG_RF_UART_BAUD_RATE`: 9600 baud
- **Power Supply:** 5V DC via regulated step-down supply with TVS surge diode protection and bulk $470\mu\text{F}$ decoupling capacitor on the 3.3V RF supply rail.

---

## 2. Node Actuator & Flow Sensor Wiring
- **Pump Driver Interface:** MOSFET/Relay driver module connected to GPIO 4 with pulldown resistor ($10\text{k}\Omega$) ensuring hardware default OFF on boot.
- **Flyback & Suppression:** DC pump motor MUST have flyback diode (1N4007 or Schottky SS34) directly across pump terminals. AC pumps MUST use snubber network ($0.1\mu\text{F} + 100\Omega$) and varistor (MOV).
- **Driver Feedback Sensing:** Optocoupler / voltage divider input on GPIO 5 for physical driver state confirmation (`driver_feedback`).
- **Flow Sensor Interface (YF-S201 / OF06ZAT):**
  - Signal pin connected to GPIO 18 (Interrupt capable pin with $10\text{k}\Omega$ pull-up resistor and $10\text{nF}$ anti-bounce ceramic capacitor).
  - VCC: 5V DC power, GND: Common ground.
- **Emergency Isolation:** Manual mechanical E-stop switch cutting main 12V/24V pump bus power line.
