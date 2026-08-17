# Aeroponics Sprint 1.5 FMEA & Fail-Safe Policy Specification

> **Document Status:** Official Safety Architecture & Failure Mode Analysis (`SPEC-SAFETY-001`)  
> **Target Scope:** ESP32 RF Gateway & 12 Remote Pump Nodes (Sprint 1.5 POC & Sprint 2 Production)  
> **Aligned with:** `docs/RF_FLOW_POC_PUMP_FEEDBACK.md`, `docs/RF_PROTOCOL.md`, `docs/RF_FLOW_POC_WIRING.md`  

---

## 1. Safety Architecture Principles (Nguyên Tắc An Toàn Cốt Lõi)

1. **Fail-Closed Default (Mặc định ngắt an toàn):** Toàn bộ cơ cấu chấp hành (MOSFET/Relay) bắt buộc phần cứng kéo trở về `OFF` (Pull-down $10\text{ k}\Omega$) khi khởi động, mất nguồn, brownout, reset CPU hoặc trước khi code ứng dụng chạy.
2. **Autonomous Node Lease (Khóa hạn định độc lập tại Node):** Lệnh `SET_PUMP(ON)` bắt buộc mang tham số `run_lease_ms`. Node duy trì deadman timer độc lập; nếu mất kết nối RF với Gateway trong lúc đang bơm, Node tự động cưỡng bức ngắt bơm `OFF` khi hết hạn lease ($\le 500\text{ms}$).
3. **Stale Node Isolation (Cách ly Node mất liên lạc):** Nếu không nhận được telemetry/heartbeat trong $>15\text{ giây}$, Gateway chuyển trạng thái Node sang `STALE`, đặt `desired_state = OFF`, hủy bỏ toàn bộ lệnh chờ, chốt cờ lỗi và xuất bản `STALE_SAFE_OFF` lên MQTT.
4. **Explicit Recovery Requirement (Phục hồi có kiểm soát):** Khi lỗi đã bị chốt (Fault Latched), hệ thống TUYỆT ĐỐI KHÔNG tự động xóa lỗi hoặc tự động tiếp tục chu kỳ tưới nếu chưa có lệnh `resetFault` hoặc can thiệp có thẩm quyền.
5. **Defence-in-Depth Pump Feedback:** Áp dụng nguyên tắc kiểm định đa tầng: $\text{GPIO} \ne \text{Driver Sense} \ne \text{Load Current} \ne \text{Flow Rate}$.

---

## 2. Failure Mode and Effects Analysis (FMEA) Matrix

| Fault Code / Mode | Cause / Trigger | Node Action | Gateway Action | Escalation & Audit | Recovery Semantics |
|---|---|---|---|---|---|
| **FMEA-01: RF Link Lost (Stale)** | Mất tín hiệu RF từ Node trong $>15\text{s}$. | Tự động tắt bơm khi hết hạn lease ($\le 500\text{ms}$). | Đánh dấu Node `STALE`, `desired_state = OFF`, chốt lỗi, hủy lệnh chờ. | Xuất bản sự kiện `STALE_SAFE_OFF` lên MQTT. | Yêu cầu Node online lại + lệnh cưỡng bức `SET_PUMP(OFF)` + reset lỗi. |
| **FMEA-02: No-Flow Fault** | `SET_PUMP(ON)` được kích, nhưng lưu lượng $<0.5\text{ L/min}$ sau $3\text{s}$ (dòng định mức bình thường). | Lập tức ngắt driver `OFF`, chốt cờ `FEEDBACK_FAULT_NO_FLOW`. | Nhận `FAULT_REPORT` / telemetry, chuyển Node sang `FAULT`. | Xuất bản `NO_FLOW_FAULT` safety audit. | Kiểm tra tắc béc / vỡ cánh bơm & gửi lệnh `reset_fault` MQTT. |
| **FMEA-03: Unexpected Flow** | Bơm đang `OFF`, nhưng cảm biến lưu lượng đo được $>0.2\text{ L/min}$ sau $150\text{ms}$. | Giữ driver `OFF`, chốt cờ `FEEDBACK_FAULT_UNEXPECTED_FLOW`. | Cập nhật Node sang `FAULT`, cảnh báo rò rỉ van hoặc dính tiếp điểm. | Xuất bản `UNEXPECTED_FLOW_FAULT` safety audit. | Kiểm tra van một chiều / solenoid valve & manual reset. |
| **FMEA-04: Driver Mismatch** | Lệnh và tín hiệu phản hồi chân kích Opto/Gate lệch nhau sau $30\text{ms}$. | Lập tức ngắt driver `OFF`, chốt `FEEDBACK_FAULT_DRIVER_MISMATCH`. | Cập nhật trạng thái Node sang `FAULT`. | Xuất bản `HARDWARE_MISMATCH` audit. | Yêu cầu kiểm tra phần cứng optocoupler PC817 / FET. |
| **FMEA-05: Gateway Reboot** | Gateway mất nguồn hoặc watchdog reset. | Node duy trì an toàn theo lease hiện hành, rồi tự ngắt `OFF`. | Khởi động lại, phục hồi boot session từ NVS, quét lại toàn bộ Node. | Xuất bản sự kiện `GATEWAY_REBOOT`. | Gateway đồng bộ trạng thái Node; cưỡng bức `OFF` nếu chưa rõ ràng. |
| **FMEA-06: Invalid RTC** | Pin RTC Gateway hết hoặc chưa đồng bộ thời gian. | Node duy trì timer an toàn độc lập. | Vô hiệu hóa toàn bộ lịch tưới tự động, cưỡng bức `desired_state = OFF`. | Xuất bản `RTC_INVALID_SAFE_OFF` audit. | Yêu cầu đồng bộ NTP hoặc cấu hình lại RTC. |
| **FMEA-07: Open Load Fault** | Đứt dây motor, tuột jack nguồn 12V, hở cầu chì ($I < 150\text{mA}$ sau $150\text{ms}$ khi ON). | Ngắt driver `OFF`, chốt `FEEDBACK_FAULT_OPEN_LOAD`. | Cập nhật Node sang `FAULT`. | Xuất bản `OPEN_LOAD_FAULT` audit. | Nối lại dây tải / thay cầu chì & gửi lệnh reset. |
| **FMEA-08: Overcurrent / Stall** | Kẹt cánh bơm, kẹt rotor cơ học ($I \ge 3.8\text{A}$ kéo dài $>50\text{ms}$ sau Inrush). | Ngắt driver khẩn cấp trong $\le 50\text{ms}$, chốt `FEEDBACK_FAULT_OVERCURRENT_STALL`. | Cập nhật Node sang `FAULT`. | Xuất bản `OVERCURRENT_STALL_FAULT` critical alarm. | Vệ sinh buồng bơm, giải kẹt rotor & manual reset. |
| **FMEA-09: Stuck-ON Relay/FET** | Lệnh OFF nhưng dòng điện tải vẫn chảy qua motor ($I > 50\text{mA}$ sau $150\text{ms}$). | Chốt cờ `FEEDBACK_FAULT_STUCK_ON`, kích hoạt còi/đèn cảnh báo. | Cập nhật Node sang `FAULT`, cảnh báo chập FET/dính relay. | Xuất bản `STUCK_ON_FAULT` critical alarm. | Cắt nguồn 12V bằng nút E-Stop & thay thế module driver. |
| **FMEA-10: Dry Run Fault** | Bơm ON nhưng buồng bơm hết nước / mất mồi ($I \le 1.2\text{A}$ và Flow $<0.5\text{L/min}$ sau $3\text{s}$). | Ngắt driver `OFF`, chốt `FEEDBACK_FAULT_DRY_RUN`. | Cập nhật Node sang `FAULT`, cảnh báo cạn bồn dinh dưỡng. | Xuất bản `DRY_RUN_FAULT` audit. | Châm thêm nước vào bồn chứa & gửi lệnh reset. |
| **FMEA-11: Over-Range Flow** | Bể ống dẫn áp lực hoặc cảm biến lưu lượng nhận xung nhiễu quá mức ($>6.5\text{ L/min}$). | Ngắt driver `OFF`, chốt `FEEDBACK_FAULT_OVER_RANGE_FLOW`. | Cập nhật Node sang `FAULT`, cảnh báo vỡ đường ống. | Xuất bản `OVER_RANGE_FLOW_FAULT` safety alarm. | Khắc phục đường ống áp lực & gửi lệnh reset. |

---
*Senior Solution Architect — Bản FMEA mở rộng hoàn tất ngày 2026-08-17.*
