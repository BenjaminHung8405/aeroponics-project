# Architectural Decision Record (ADR): RF 433 MHz Transceiver & Flow Candidate Selection

> **Document Status:** Official ADR for Sprint 1.5 Hardware Selection  
> **Status:** PROPOSED — POC candidate; pending raw evidence and independent sign-off

---

## 1. Context & Problem Statement

The production aeroponics architecture requires controlling 12 remote pump nodes from a central ESP32-S3 Gateway over wireless 433 MHz RF link. We need to evaluate RF transceivers, microcontrollers, pump drivers, and flow sensors for safety, range, latency, and reliability.

---

## 2. Candidate Evaluation & Trade-offs

### 2.1 RF Transceiver Candidates
- **Candidate 1: HC-12 433 MHz Wireless Transceiver (SI4463 based)**
  - *Pros:* Transparent UART interface, 20dBm power (up to 1km line of sight), low cost, simple baud rate configuration.
  - *Cons:* Airtime latency ~20-50ms per frame, requires external antenna for dense foliage.
  - *Decision:* **PROPOSED for POC validation**.

- **Candidate 2: EBYTE E32 LoRa 433 MHz UART Module (SX1276 based)**
  - *Pros:* High sensitivity, long range, reliable transparent mode.
  - *Cons:* Higher unit cost, slightly higher packet transmission latency.
  - *Decision:* Secondary candidate for long-range deployment.

### 2.2 Microcontroller Candidates
- **Gateway:** ESP32-S3 DevKitC-1 (Dual-core 240MHz, 8MB Flash, Wi-Fi/BLE, Hardware UART, NVS). **PROPOSED**.
- **Node Candidate:** ESP32-C3 / ESP32-WROOM-32 (Hardware UART, Pulse Counter, Low Power). **PROPOSED**.

### 2.3 Flow Sensor Candidates
- **Sensor Candidate:** YF-S201 / OF06ZAT Hall-effect Pulse Flow Meter ($450\text{ pulses/L}$, operational range $0.5 - 6.0\text{ L/min}$). **PROPOSED**.

---

## 3. Decision & Risk Acceptance

1. **Candidate Configuration:** ESP32-S3 Gateway + HC-12 RF Transceiver (UART @ 9600 baud) + YF-S201 Flow Sensor.
2. **Security Posture:** HMAC-SHA256 authenticated header with provisioned 16-byte PSK + CRC-16 check. Anti-replay enforced via boot session ID and monotonic sequence counter.
3. **Air-gapped Lab Risk Acceptance:** For initial bench testing, air-gapped RF spectrum in 433.05–434.79 MHz ISM band is approved. All PSK keys are stored in local NVS and excluded from Git tracking.
