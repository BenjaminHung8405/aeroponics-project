# Đặc Tả Kiến Trúc & Bằng Chứng Cơ Chế Phản Hồi Bơm (Pump Feedback Specification)

> **Mã tài liệu:** `SPEC-FEEDBACK-001`  
> **Trạng thái:** Official Architectural Standard & Safety Specification  
> **Áp dụng cho:** Sprint 1.5 POC (1 Gateway + 1 Node) & Định hướng Chuẩn hóa Sprint 2 Production (12 Nodes)  
> **Nguyên tắc cốt lõi:** Defence-in-Depth (Phòng thủ đa tầng) & Explicit State Verification  

---

## 1. Tuyên Ngôn Kiến Trúc & Định Lý Bất Biến (Fundamental Invariants)

Trong các hệ thống phun sương áp lực cao (High-Pressure Aeroponics - HPA), việc cấp dinh dưỡng cho rễ cây phụ thuộc 100% vào chu kỳ phun chính xác (ví dụ: $10\text{s}$ phun / $300\text{s}$ nghỉ). Việc suy diễn sai lệch trạng thái bơm sẽ dẫn đến hai thảm họa nghiêm trọng:
1. **False Positive (Báo đang chạy nhưng thực tế không phun):** Rễ cây bị khô và chết chỉ sau $15 - 30\text{ phút}$ thiếu nước.
2. **False Negative / Stuck-ON (Báo đã tắt nhưng thực tế bơm đang chạy liên tục):** Đập vỡ đường ống do áp suất vượt ngưỡng, ngập úng buồng rễ và cháy cuộn dây motor.

### 📌 Định Lý Bất Biến Về Trạng Thái Phản Hồi (Invariant Statement):
$$\text{GPIO Control Output} \neq \text{Driver Feedback} \neq \text{Electrical Load Current} \neq \text{Hydraulic Liquid Flow}$$

```
+---------------------------------------------------------------------------------------------------+
|                               DEFENCE-IN-DEPTH MULTI-TIER FEEDBACK                                |
+-----------------------+-------------------------+-------------------------+-----------------------+
|   TIER 0: CONTROL     |     TIER 1: DRIVER      |      TIER 2: LOAD       |    TIER 3: FLOW       |
|      (Intent)         |    (Electronic Gate)    |     (Motor Current)     |  (Physical Liquid)    |
|                       |                         |                         |                       |
|   MCU GPIO Output     |   Optocoupler/MOSFET    |  Current Sensing Shunt  |   Positive Displace   |
|   Active HIGH/LOW     |     Gate Voltage Sense  |   / Hall Sensor ACS712  |    Flow Meter OF06    |
|                       |                         |                         |                       |
| "Tôi muốn bơm chạy"   | "Mạch kích đã mở cổng"  | "Motor đang ăn dòng điện"| "Nước đang thực sự chảy"|
+-----------------------+-------------------------+-------------------------+-----------------------+
```

**Quy tắc Cấm kỵ Tuyệt đối (Strict Negative Rules):**
- **CẤM** gán `reported_state = desired_state` trong firmware hoặc backend.
- **CẤM** coi việc kích GPIO HIGH là bơm đã chạy thành công (`RUNNING`).
- **CẤM** xác nhận một chu kỳ tưới thành công (`SPRAY_SUCCESS`) nếu chưa thỏa mãn chuỗi điều kiện tuần tự:
  $$\text{RF\_ACKED} \longrightarrow \text{DRIVER\_SENSE\_ON} \longrightarrow \text{LOAD\_CURRENT\_CONFIRMED} \longrightarrow \text{FLOW\_CONFIRMED}$$

---

## 2. Phân Tích Kỹ Thuật Các Tầng Phản Hồi (Multi-Tier Classification)

### 2.1. Tier 1: Driver Feedback (Phản Hồi Mạch Kích Điện Tử)
- **Vị trí đo:** Đọc điện áp sau bộ cách ly quang PC817 (ngay tại cực Gate của MOSFET hoặc chân cuộn hút Relay).
- **Cơ chế:** Ngõ vào số GPIO (ESP32-C3 GPIO 5) với mạch phân áp/kéo trở bảo vệ.
- **Ý nghĩa:**
  - Xác nhận vi điều khiển đã xuất lệnh qua tầng cách ly quang thành công.
  - Phát hiện đứt mạch điều khiển, hỏng optocoupler, hoặc lỗi phần mềm treo pin.
- **Giới hạn vật lý:**
  - *Không* biết được nguồn động lực 12VDC/220VAC có cấp vào tải hay không.
  - *Không* biết được cầu chì tải có bị đứt hay dây motor có bị tuột.
  - *Không* biết được MOSFET có bị hở chân Drain-Source hay Relay bị trơ tiếp điểm.
  - *Không* biết motor có quay hay bị kẹt rotor.

### 2.2. Tier 2: Electrical Load Feedback (Phản Hồi Dòng Tải Động Cơ)
- **Vị trí đo:** Mắc nối tiếp trên đường dây cấp nguồn động lực $12\text{VDC}$ của motor bơm.
- **Cơ chế lựa chọn:**
  - **Phương án Primary (POC & Production):** Cảm biến dòng hiệu ứng Hall cách ly quang **Allegro ACS712ELCTR-05B-T** (Dải đo $\pm 5\text{A}$, độ nhạy $185\text{ mV/A}$, cách ly điện áp $2.1\text{ kV}_{\text{RMS}}$).
  - **Phương án Secondary:** Điện trở Shunt Low-side ($0.05\Omega / 2\text{W}$) kết hợp OpAmp vi sai LM358 lọc RC vào ADC1.
- **Ý nghĩa:**
  - Xác nhận dòng điện thực tế $I_{\text{load}}(t)$ đang chạy qua cuộn quấn motor.
  - Phân loại chính xác các trạng thái điện:
    1. **Hở mạch (Open Load):** $I < 150\text{ mA}$ khi đang lệnh ON (đứt dây, đứt cầu chì).
    2. **Chạy không tải / Hết nước (Dry Run Undercurrent):** $I \in [0.4\text{A}, 1.2\text{A}]$ (cánh bơm quay tự do trong không khí, giảm tải cơ học).
    3. **Tải định mức bình thường (Nominal Pumping):** $I \in [1.6\text{A}, 2.6\text{A}]$.
    4. **Kẹt cánh bơm / Khóa rotor (Locked Rotor Stall):** $I \ge 3.8\text{A}$ (dòng tăng vọt gấp $3\times - 4\times$).
    5. **Dính tiếp điểm / Chập driver (Stuck-ON):** $I > 50\text{ mA}$ khi lệnh là OFF.

### 2.3. Tier 3: Hydraulic / Flow Feedback (Phản Hồi Lưu Lượng Thủy Lực)
- **Vị trí đo:** Đặt ngay sau cửa xả áp lực của buồng bơm, trước béc phun sương aeroponics.
- **Cơ chế lựa chọn:** Cảm biến lưu lượng bánh răng dịch chuyển tích cực **OF06ZAT Oval Gear** (Dải $0.3 - 6.0\text{ L/min}$, $2500\text{ pulses/L}$).
- **Ý nghĩa:**
  - Là bằng chứng vật lý cuối cùng khẳng định chất lỏng dinh dưỡng đang thực sự di chuyển qua đường ống.
  - Phát hiện: Mất mồi nước, nghẹt lọc, tắc béc phun, nứt vỡ đường ống.

---

## 3. Ma Trận Bao Phủ Sự Cố (Fault Coverage Matrix)

Bảng phân tích khả năng phát hiện các chế độ sự cố giữa các kiến trúc phần cứng khác nhau:

| STT | Chế độ Sự cố (Failure Mode) | Mô tả Hiện tượng Vật lý | (A) Legacy Direct GPIO | (B) Chỉ Driver Sense | (C) Driver + Flow (POC Baseline) | (D) Driver + Current + Flow (Full Target) |
|---|---|---|:---:|:---:|:---:|:---:|
| 1 | **Open Load / Broken Wire** | Đứt dây motor, tuột cọc jack nguồn 12V, đứt cầu chì. | ❌ Không | ❌ Không | 🟡 Báo `NO_FLOW` sau $3\text{s}$ (không rõ lý do điện) | 🟢 **Phát hiện tức thì sau $150\text{ms}$ (`OPEN_LOAD`)** |
| 2 | **Stuck-ON Driver / Shorted FET** | MOSFET bị đánh thủng nối tắt D-S hoặc Relay dính cứng tiếp điểm. | ❌ Không | ❌ Không | 🟡 Báo `UNEXPECTED_FLOW` (nếu van mở) / ❌ Mù nếu van đóng | 🟢 **Phát hiện tức thì sau $150\text{ms}$ (`STUCK_ON_RELAY`)** |
| 3 | **Stuck-OFF / Open FET** | MOSFET cháy đứt kênh hoặc hỏng cuộn hút relay. | ❌ Không | ❌ Không | 🟡 Báo `NO_FLOW` sau $3\text{s}$ | 🟢 **Phát hiện tức thì sau $150\text{ms}$ (`OPEN_LOAD`)** |
| 4 | **Rotor Locked / Motor Stall** | Kẹt rác buồng bơm, vỡ bạc đạn motor, dòng vọt lên $6 - 8\text{A}$. | ❌ Không | ❌ Không | 🔴 **Cực kỳ nguy hiểm** (Chờ $3\text{s}$ mới báo `NO_FLOW`, motor nóng bốc khói) | 🟢 **Cắt điện bảo vệ sau $50\text{ms}$ (`OVERCURRENT_STALL`)** |
| 5 | **Dry Run (Chạy khô mất mồi)** | Hết nước trong bồn, bọt khí đầy buồng bơm, motor quay trơn. | ❌ Không | ❌ Không | 🟡 Báo `NO_FLOW` sau $3\text{s}$ (không phân biệt được nghẹt béc) | 🟢 **Phân biệt chuẩn xác (`DRY_RUN`: Dòng tụt $0.8\text{A}$ + Flow = 0)** |
| 6 | **Blocked Nozzle / Deadhead** | Toàn bộ béc phun bị tắc cặn, áp suất tăng kịch kim, lưu lượng = 0. | ❌ Không | ❌ Không | 🟢 Phát hiện sau $3\text{s}$ (`NO_FLOW`) | 🟢 **Phát hiện chuẩn (`NO_FLOW`: Dòng định mức cao + Flow = 0)** |
| 7 | **Pipe Rupture / Line Burst** | Vỡ khớp nối ống áp lực, nước phun tự do không kiểm soát. | ❌ Không | ❌ Không | 🟢 Phát hiện tức thì (`OVER_RANGE_FLOW` $>6.5\text{ L/min}$) | 🟢 **Phát hiện tức thì (`OVER_RANGE_FLOW` + Dòng thấp)** |
| 8 | **Driver Opto Disconnected** | Hỏng optocoupler PC817 hoặc đứt đường mạch kích từ MCU. | ❌ Không | 🟢 Phát hiện sau $30\text{ms}$ (`DRIVER_MISMATCH`) | 🟢 Phát hiện sau $30\text{ms}$ (`DRIVER_MISMATCH`) | 🟢 **Phát hiện sau $30\text{ms}$ (`DRIVER_MISMATCH`)** |
| 9 | **MCU Brownout / Power Dip** | Nguồn 5V/3.3V sụt áp khi motor đóng ngắt. | ❌ Không | ❌ Không | 🟡 Node reboot $\to$ Safe-OFF | 🟢 **Node reboot $\to$ Safe-OFF + Current Spike Blanked** |

---

## 4. Giải Trình Tường Minh Các Failure Modes Điện KHÔNG THỂ Phát Hiện Được Trong Cấu Hình POC (Driver Sense + Flow Sensor)

Trong giai đoạn POC Lab (1 Gateway + 1 Node) khi chỉ lắp đặt **Tier 1 (Driver Feedback)** và **Tier 3 (Flow Sensor)** mà chưa có **Tier 2 (Current Sensing)**, các hạn chế vật lý sau đây được ghi nhận chính thức:

### ⚠️ Danh sách Failure Modes Điện Bị Giới Hạn & Rủi Ro Tương Ứng:

1. **Rủi ro Quá Dòng & Cháy Cuộn Quấn Motor khi Kẹt Rotor (Locked Rotor Delay):**
   - *Hiện tượng:* Khi cánh bơm bị kẹt cơ học, dòng điện tăng vọt lên $6.0 - 8.0\text{A}$ ($>300\%$ định mức).
   - *Hạn chế:* Flow Sensor chỉ ghi nhận $0\text{ L/min}$ và phải chờ hết cửa sổ Timeout **$3000\text{ ms}$** mới kích hoạt cờ `NO_FLOW_FAULT` để ngắt bơm. Trong $3\text{ giây}$ này, cuộn dây motor phải chịu nhiệt lượng Joule cực lớn ($P = I^2 R \approx 64\times R$), có nguy cơ làm chảy lớp sơn cách điện enamel của motor DC.
   - *Biện pháp Bù đắp trong POC:* Bắt buộc lắp **cầu chì nhiệt / cầu chì cắt chậm 3.15A TR5 Time-Lag Fuse** nối tiếp trên đường nguồn 12V. Nếu dòng $\ge 6\text{A}$, cầu chì cơ khí sẽ tự đứt sau $<1.2\text{s}$ trước khi motor bị tổn hại.

2. **Không Thể Phân Biệt Giữa Chạy Khô (Dry Run) và Tắc Béc / Đứt Dây (Root-Cause Ambiguity):**
   - *Hiện tượng:* Khi nhận được cờ `NO_FLOW_FAULT`, hệ thống chỉ biết là không có nước đến cảm biến.
   - *Hạn chế:* Không thể chẩn đoán từ xa xem nguyên nhân là do:
     - (a) Đứt dây nguồn / cháy cầu chì (Dòng $0\text{A}$).
     - (b) Hết nước trong bồn dinh dưỡng / mất mồi (Dòng $0.8\text{A}$).
     - (c) Nghẹt béc phun / kẹt van điện từ (Dòng $2.2\text{A}$).
   - *Biện pháp Bù đắp trong POC:* Quy trình vận hành POC yêu cầu kỹ thuật viên kiểm tra trực quan bồn chứa và đồng hồ áp kế khi xảy ra lỗi `NO_FLOW_FAULT`.

3. **MOSFET Bị Đánh Thủng Chập D-S khi Van Solenoid Khóa (Undetected Stuck-ON during Static Pressure):**
   - *Hiện tượng:* MOSFET bị hỏng chập chân Drain-Source, liên tục cấp điện $12\text{V}$ vào motor. Nếu đường ống có van điện từ chặn hoặc béc đã đạt áp suất cân bằng, lưu lượng bằng 0.
   - *Hạn chế:* Flow Sensor không quay nên không báo `UNEXPECTED_FLOW`. Driver Sense đọc chân Gate vẫn thấy LOW (theo lệnh MCU) nên không báo `DRIVER_MISMATCH`. Kết quả là bơm bị om điện liên tục mà không có cảnh báo.
   - *Biện pháp Bù đắp trong POC:* Tích hợp nút dừng khẩn cấp cơ khí **E-Stop** ngắt nguồn 12V vật lý và quy định kiểm tra nhiệt độ vỏ motor định kỳ trong quá trình chạy thử nghiệm.

---

## 5. Quy Định Kỹ Thuật Chi Tiết Cho Cơ Chế Phản Hồi Bơm (Specification & Calibration)

### 5.1. Bảng Ngưỡng Dòng Điện Định Lượng (Current Thresholds - Bơm 12VDC 45W)

| Thông số (Parameter) | Ký hiệu | Giá trị Ngưỡng | Ý nghĩa Kỹ thuật |
|---|---|:---:|---|
| **Leakage Current Max (OFF)** | $I_{\text{leakage\_max}}$ | $\le 50\text{ mA}$ | Ngưỡng phát hiện rò điện / hỏng cách ly / dính tiếp điểm khi lệnh OFF |
| **Open Load Minimum (ON)** | $I_{\text{open\_load\_min}}$ | $\ge 150\text{ mA}$ | Dòng điện tối thiểu để xác nhận mạch động lực đã khép kín tải |
| **Dry Run Operating Current** | $I_{\text{dry\_run}}$ | $0.40\text{ A} - 1.20\text{ A}$ | Dải dòng điện khi bơm quay không tải (buồng bơm có bọt khí/hết nước) |
| **Nominal Operating Current** | $I_{\text{nominal}}$ | $1.60\text{ A} - 2.60\text{ A}$ | Dải dòng điện vận hành ổn định định mức khi bơm tải nước qua béc phun |
| **Stall / Overcurrent Threshold** | $I_{\text{stall}}$ | $\ge 3.80\text{ A}$ | Ngưỡng ngắt khẩn cấp khi rotor bị kẹt / chập vòng cuộn dây |

### 5.2. Cửa Sổ Thời Gian Lọc & Chống Rung (Timing Windows & Debounce Specifications)

```
Time Axis (ms) after Commanded ON:
0ms        30ms       80ms           150ms                             3000ms
|----------|----------|--------------|---------------------------------|----->
[  Driver  ][ Inrush  ][  Open Load  ][      Normal Operation /         ][ Flow Confirm /
[ Mismatch ][ Blanking][ Verification][    Overcurrent Debounce (50ms) ][ Dry Run Timeout
```

1. **Inrush Blanking Window ($t_{\text{inrush}} = 80\text{ ms}$):**
   - Bỏ qua các xung đỉnh dòng điện khởi động motor ($I_{\text{inrush}} \approx 6.0\text{A}$ trong $20 - 50\text{ms}$) do nạp điện cảm cuộn quấn.
   - Trong $80\text{ms}$ đầu tiên, cờ `OVERCURRENT_STALL` bị vô hiệu hóa tạm thời để tránh ngắt nhầm.
2. **Overcurrent Debounce Window ($t_{\text{debounce}} = 50\text{ ms}$):**
   - Sau giai đoạn inrush, nếu dòng $I \ge 3.8\text{A}$ duy trì liên tục vượt quá $50\text{ms}$ (tương đương 50 mẫu ADC), hệ thống lập tức chốt lỗi `FEEDBACK_FAULT_OVERCURRENT_STALL` và ngắt MOSFET.
3. **Driver Mismatch Timeout ($t_{\text{driver\_mismatch}} = 30\text{ ms}$):**
   - Thời gian cho phép mạch optocoupler PC817 và mạch lọc thông thấp RC nạp/xả điện áp logic.
   - Nếu sau $30\text{ms}$ mà `DriverSensePin != CommandedState`, chốt lỗi `FEEDBACK_FAULT_DRIVER_MISMATCH`.
4. **Open Load Verification Window ($t_{\text{open\_load}} = 150\text{ ms}$):**
   - Thời gian xác nhận dòng điện tải đã vượt qua $150\text{ mA}$. Nếu sau $150\text{ms}$ dòng vẫn $<150\text{ mA}$, chốt lỗi `FEEDBACK_FAULT_OPEN_LOAD`.
5. **Flow Confirmation Timeout ($t_{\text{flow\_confirm}} = 3000\text{ ms}$):**
   - Thời gian cho chất lỏng điền đầy đường ống, sinh áp suất và đẩy bánh răng cảm biến lưu lượng OF06ZAT quay vượt ngưỡng $0.5\text{ L/min}$.
   - Nếu sau $3000\text{ms}$ mà lưu lượng vẫn $<0.5\text{ L/min}$:
     - Nếu $I \le 1.2\text{A} \implies$ Chốt lỗi `FEEDBACK_FAULT_DRY_RUN`.
     - Nếu $I > 1.2\text{A} \implies$ Chốt lỗi `FEEDBACK_FAULT_NO_FLOW`.

---

## 6. Thiết Kế Mạch Phần Cứng Đo Dòng (Hardware Current Sensing Conditioning)

### 6.1. Sơ Đồ Nguyên Lý Đo Dòng với ACS712-05B
```
 +12V_PWR >---+-----------------------------------------+
              |                                         |
             [FUSE 3.15A TR5]                           |
              |                                         |
              +---> [ACS712 IP+ (Pin 1,2)]              |
                    [ACS712 IP- (Pin 3,4)] ---> [DC PUMP (+)]
                                                [DC PUMP (-)] ---> [MOSFET D-S] ---> PGND
                                                                                     |
 +5V_ISO  >---+---> [ACS712 VCC (Pin 8)]                                             |
              |     [ACS712 GND (Pin 5)] --------------------------------------------+
             [C_dec 100nF]
              |
              +---> [ACS712 FILTER (Pin 6)] ---> [C_flt 10nF] ---> GND
              |
              +---> [ACS712 VOUT (Pin 7)]
                          |
                         [R1 10k 0.1%]
                          |
                          +-------------> [ESP32-C3 ADC1 (GPIO 1)]
                          |
                         [R2 15k 0.1%]
                          |
                         [C_adc 100nF]
                          |
                         LGND
```

### 6.2. Tính Toán Phân Áp & Chuyển Đổi ADC
1. **Điện áp ngõ ra ACS712-05B ($V_{\text{CC}} = 5.0\text{V}$):**
   $$V_{\text{OUT}}(I) = 2.50\text{V} + (0.185\text{V/A} \times I)$$
   - Tại $I = 0\text{A} \implies V_{\text{OUT}} = 2.50\text{V}$.
   - Tại $I = +3.8\text{A} \implies V_{\text{OUT}} = 2.50\text{V} + (0.185 \times 3.8) = 3.203\text{V}$.
   - Tại $I = +5.0\text{A} \implies V_{\text{OUT}} = 2.50\text{V} + (0.185 \times 5.0) = 3.425\text{V}$.
2. **Cầu phân áp hạ áp vào dải ADC ESP32-C3 ($0 - 3.0\text{V}$):**
   $$K_{\text{div}} = \frac{R_2}{R_1 + R_2} = \frac{15\text{ k}\Omega}{10\text{ k}\Omega + 15\text{ k}\Omega} = 0.600$$
   - Tại $I = 0\text{A} \implies V_{\text{ADC}} = 2.50\text{V} \times 0.60 = 1.500\text{V}$.
   - Tại $I = +3.8\text{A} \implies V_{\text{ADC}} = 3.203\text{V} \times 0.60 = 1.922\text{V}$.
   - Tại $I = +5.0\text{A} \implies V_{\text{ADC}} = 3.425\text{V} \times 0.60 = 2.055\text{V} \le 3.0\text{V}$ (An toàn tuyệt đối cho chân ADC ESP32-C3).
3. **Bộ lọc thông thấp RC chống nhiễu đóng cắt:**
   - $R_1 // R_2 = \frac{10 \times 15}{25} = 6\text{ k}\Omega$.
   - $C_{\text{adc}} = 100\text{ nF}$.
   - Tần số cắt: $f_c = \frac{1}{2 \pi \times 6000 \times 100 \times 10^{-9}} \approx 265\text{ Hz}$ (Triệt tiêu hoàn toàn nhiễu chổi than và xung hài RF 433 MHz).

---

## 7. Máy Trạng Thái Đánh Giá Phản Hồi (Pump Feedback State Machine FSM)

```
                    +--------------------------------+
                    |    PUMP_HEALTH_OFF_HEALTHY     |
                    +--------------------------------+
                                   |
                     Commanded ON (t = 0)
                                   v
                    +--------------------------------+
                    |  PUMP_HEALTH_STARTING_INRUSH   | <---+
                    +--------------------------------+     |
                                   |                       | (t < 3000ms)
                     Flow & Current Confirmed              |
                                   v                       |
                    +--------------------------------+     |
                    | PUMP_HEALTH_RUNNING_CONFIRMED  |-----+
                    +--------------------------------+
                                   |
         +-------------------------+-------------------------+
         | (Stall / Open Load / Dry Run / Mismatch / No Flow) |
         v                                                   v
+---------------------------------------------------------------------------------+
|                          PUMP_HEALTH_FAULT_LATCHED                              |
|  - Force Hard Safe-OFF (GPIO LOW)                                               |
|  - Latch Fault Code (OVERCURRENT, OPEN_LOAD, DRY_RUN, STUCK_ON, MISMATCH, etc.) |
|  - Audit Telemetry via RF to Gateway                                            |
|  - MUST NOT auto-clear without explicit reset command                           |
+---------------------------------------------------------------------------------+
```

---

## 8. Kết Luận & Quyết Định Kỹ Thuật (Architectural Conclusion)

1. **Phương án POC:** Triển khai **Tier 1 (Driver Opto Feedback)** + **Tier 3 (OF06ZAT Flow Feedback)** kết hợp **Cầu chì cơ khí 3.15A TR5 Time-Lag** và **Khóa an toàn Hard Deadman Lease $\le 500\text{ms}$**. Toàn bộ các hạn chế về đo dòng điện đã được phân tích và kiểm soát trong FMEA.
2. **Phương án Sprint 2 Production:** Bắt buộc tích hợp đầy đủ **Tier 2 (Current Sensing ACS712-05B / Shunt)** trên toàn bộ 12 Remote Nodes để đạt mức bao phủ sự cố 100% (Full Fault Coverage).
3. **Mã nguồn Logic Đánh giá:** Module `PumpFeedbackEvaluator` (`aeroponics-firmware/include/pump_feedback_evaluator.h` và `src/pump_feedback_evaluator.cpp`) được chuẩn hóa và xác minh 100% bằng bộ kiểm thử tự động.

---
*Senior Solution Architect — Bản đặc tả và chứng minh cơ chế Pump Feedback hoàn tất ngày 2026-08-17.*
