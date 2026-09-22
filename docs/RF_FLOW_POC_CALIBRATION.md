# Quy Trình Thiết Lập Flow Bench & Hiệu Chuẩn Đo Lường Cảm Biến Lưu Lượng (Measurement Traceability & Safety Specification)

> **Mã tài liệu:** `SPEC-FLOW-CAL-001`  
> **Phiên bản:** `v1.0.0` (Sprint 1.5 — Track A: Hardware Discovery & Decision Record)  
> **Phạm vi áp dụng:** Băng thử nghiệm thủy lực (Flow Test Bench), Cảm biến lưu lượng (OF06ZAT / YF-S401), Bơm áp lực Aeroponics, Node điều khiển & Gateway.  
> **Tiêu chuẩn viện dẫn:** ISO/IEC 17025:2017 (Yêu cầu chung về năng lực phòng thử nghiệm và hiệu chuẩn), ISO 4064 (Đo lưu lượng nước trong đường ống kín), OIML R 49, TCVN 9800.

---

## 1. Mục Tiêu & Nguyên Tắc Đo Lường Truy Xuất Nguồn Gốc (Measurement Traceability)

Trong hệ thống khí canh áp lực cao (High-Pressure Aeroponics - HPA), lưu lượng và tổng thể tích nước phun qua béc trong mỗi chu kỳ tưới (thường kéo dài từ $5\text{s}$ đến $30\text{s}$) là thông số sống còn quyết định kích thước giọt sương ($20 - 50\mu\text{m}$) và tỷ lệ hấp thụ dinh dưỡng của rễ cây.

Hệ thống đo lưu lượng của dự án phải tuân thủ nghiêm ngặt nguyên tắc **Measurement Traceability (Tính liên kết chuẩn đo lường)**:
1. **Chuỗi liên kết chuẩn không đứt đoạn (Unbroken Traceability Chain):** Mọi kết quả đo xung, quy đổi $L/\text{min}$, và tích phân thể tích $V_{\text{delivered}}$ của Remote Node phải được đối chiếu trực tiếp với Chuẩn thể tích / khối lượng đo lường cấp 1 (Class A Gravimetric & Volumetric Reference Standard) tại phòng thí nghiệm.
2. **Cấu hình hiệu chuẩn có phiên bản (Calibration as Versioned Configuration):** Tuyệt đối **CẤM** hard-code hệ số $K$-factor chung cho toàn bộ hệ thống. Mỗi cảm biến vật lý (gắn với `sensor_serial` và `node_id`) bắt buộc phải có hồ sơ hiệu chuẩn riêng biệt (`calibration_id`, `version_id`), lưu trữ bất biến trong cơ sở dữ liệu `sensor_calibrations`. Không phân phối profile bằng frame HMAC xuống ATmega8; chỉ dùng opcode AGU đã được xác minh hoặc cấu hình ngoài băng.
3. **Độ lặp lại và ngưỡng loại bỏ định lượng (Repeatability & Rejection Threshold):** Một cảm biến chỉ được chấp thuận đưa vào vận hành nếu sai số độ lặp lại $E_{\text{rep}} \le 1.5\%$ qua $\ge 3$ lần thử độc lập tại mỗi điểm đo, và sai số tuyệt đối $E_{\text{acc}} \le \pm 2.0\%$ sau khi áp dụng mô hình hiệu chuẩn đa điểm.

---

## 2. Thiết Kế & Cấu Trúc Băng Thử Nghiệm Thủy Lực (Flow Bench Architecture)

### 2.1 Sơ đồ Nguyên lý Thủy lực & P&ID (Piping and Instrumentation Diagram)

Băng thử được thiết kế khép kín có khả năng kiểm soát áp suất, lưu lượng, nhiệt độ và thu hồi dung dịch tuần hoàn:

```
                  +-------------------------------------------------------------+
                  |                  FLOW BENCH HYDRAULIC CIRCUIT               |
                  +-------------------------------------------------------------+

 [ NGUỒN NƯỚC ]
 +------------------+
 | Thùng chứa 20L   |
 | Lọc đáy 150 um   |
 | Sensor phao Low  |---+
 +------------------+   |
                        | (Ống hút PU 10/8mm)
                        v
                 [ VAN CƠ KHẨN CẤP V1 ] (Manual Ball Valve 1/4 Turn)
                        |
                        v
               +-----------------+
               | BƠM ÁP LỰC 12V  | (2.0A Nominal, 100 PSI Max)
               +-----------------+
                        |
                        +----------> [ VAN AN TOÀN XẢ ÁP PRV ] --------+ (Hồi về thùng)
                        |            (Set áp 6.0 bar / 87 PSI)          |
                        v                                               |
             [ VAN ĐIỀU ÁP / TIẾT LƯU NV1 ]                             |
                        |                                               |
                        v                                               |
              [ ĐỒNG HỒ ÁP SUẤT P1 ] (0-7 bar, Class 1.6, Glycerin)     |
                        |                                               |
                        v (Đoạn ống thẳng upstream >= 10D = 100mm)      |
             +-----------------------+                                  |
             | CẢM BIẾN LƯU LƯỢNG S1 | (OF06ZAT Oval Gear / YF-S401)    |
             +-----------------------+                                  |
                        |                                               |
                        v (Đoạn ống thẳng downstream >= 5D = 50mm)      |
             [ CẢM BIẾN NHIỆT ĐỘ T1 ] (PT100 / DS18B20 0.1°C)           |
                        |                                               |
                        +-----------------------------------------------+
                        |
       +----------------+----------------+
       |                                 |
       v                                 v
 [ NHÁNH 1: BÉC PHUN MIST ]        [ NHÁNH 2: HIỆU CHUẨN THỂ TÍCH ]
 (Van 2 ngả V2)                    (Van 2 ngả V3 - Micro Needle Valve)
       |                                 |
       v                                 v
 [ Giàn béc phun 0.3mm (x4) ]      [ BÌNH ĐO THỂ TÍCH CHUẨN CẤP A ]
       |                            - Bình trụ 2000mL ±10mL (ISO 4788)
       v                            - Cân phân tích 5000g ±0.1g (KERN)
 [ Khay thu hồi nước ]                   |
       |                                 v
       +---------------------------> [ Xả hồi thùng chứa ]
```

### 2.2 Quy định Hình học Thủy lực & Ổn định Dòng chảy (Hydrodynamic Stability)
1. **Đoạn ống thẳng trước cảm biến (Upstream Straight Pipe):** Chiều dài tối thiểu $L_{\text{up}} \ge 10 \times D_{\text{pipe}}$ ($100\text{ mm}$ với ống $D=10\text{ mm}$) để triệt tiêu dòng xoáy (vorticity) và nhiễu loạn vận tốc sau van/co nối.
2. **Đoạn ống thẳng sau cảm biến (Downstream Straight Pipe):** Chiều dài tối thiểu $L_{\text{down}} \ge 5 \times D_{\text{pipe}}$ ($50\text{ mm}$) chống hiện tượng phản xạ áp suất ngược (backpressure wave).
3. **Hướng dòng chảy:** Lắp cảm biến nằm ngang hoặc theo chiều thẳng đứng có dòng chảy hướng **TỪ DƯỚI LÊN TRÊN** nhằm tự động đuổi hết bọt khí (air bleeding), ngăn ngừa bẫy khí gây nhảy sai lệch số đếm xung.
4. **Bộ giảm rung & triệt xung áp (Pulsation Damper):** Bơm màng (diaphragm pump) tạo xung áp suất dao động. Bố trí bình tích áp mini ($0.5\text{ L}$, nạp trước $1.5\text{ bar}$) ngay sau bơm để làm phẳng dạng sóng áp suất trước khi đi vào cảm biến.

---

## 3. Thiết Bị Chuẩn & Yêu Cầu Liên Kết Đo Lường (Reference Equipment)

| Thiết bị | Chủng loại / Model | Dải đo | Cấp chính xác / Dung sai | Chuẩn truy xuất (Traceability) |
|---|---|---|---|---|
| **Bình đo thể tích chuẩn** | Pyrex / Duran Class A (ISO 4788) | $1000\text{ mL} - 2000\text{ mL}$ | $\pm 5\text{ mL}$ ($0.5\%$) tại $20^\circ\text{C}$ | Giấy chứng nhận kiểm định Viện Đo lường VMI / Trung tâm Quatest |
| **Cân điện tử phân tích** | KERN PCB 6000-1 / OHAUS Defender | $0 - 6000\text{ g}$ | $\pm 0.1\text{ g}$ ($d = 0.1\text{g}$) | Chuẩn quả cân F1 (OIML R 111) có tem hiệu chuẩn |
| **Đồng hồ bấm giờ chuẩn** | Fluke Timer / Real-Time Hardware Counter | $0 - 3600\text{ s}$ | $\pm 0.001\text{ s}$ ($\le 1\text{ ms}$) | Chuẩn thạch anh TCXO trôi dạt $< 1\text{ ppm}$ |
| **Cảm biến nhiệt độ chuẩn** | PT100 4-wire Class 1/10 DIN | $0 - 50^\circ\text{C}$ | $\pm 0.03^\circ\text{C}$ | Chuẩn ITS-90 tại điểm chuẩn nước đá và nhiệt kế mẫu |
| **Đồng hồ áp suất** | WIKA 213.53 Glycerin-filled | $0 - 10\text{ bar}$ | Class 1.0 ($\pm 0.1\text{ bar}$) | Bàn kiểm áp lực chuẩn Deadweight Tester |

---

## 4. Mô Hình Toán Học & Công Thức Chuyển Đổi (Mathematical Foundations)

### 4.1 Phương pháp Đo Thể tích Bằng Trọng lượng (Gravimetric Method with Density Compensation)

Khối lượng nước thu được $m_{\text{raw}}$ cân bằng cân điện tử được hiệu chỉnh theo lực đẩy Archimedes của không khí và khối lượng riêng của nước theo nhiệt độ:

$$V_{\text{ref}}(T) = \frac{m_{\text{water}}}{\rho_{\text{water}}(T)} \times \left(1 - \frac{\rho_{\text{air}}}{\rho_{\text{weights}}}\right)^{-1} \times \left[1 - \gamma_{\text{vessel}}(T - 20^\circ\text{C})\right]$$

Trong đó:
- $m_{\text{water}}$: Khối lượng tịnh của nước ($g$).
- $\rho_{\text{water}}(T)$: Khối lượng riêng của nước cất ở nhiệt độ $T$ ($g/\text{cm}^3$), tính theo phương trình Tanaka:
  $$\rho(T) = a_5 \left[ 1 - \frac{(T + a_1)^2 (T + a_2)}{a_3 (T + a_4)} \right]$$
  *(Với $T \approx 25^\circ\text{C}$, $\rho \approx 0.997047\text{ g/cm}^3$)*.
- $\rho_{\text{air}} \approx 0.0012\text{ g/cm}^3$, $\rho_{\text{weights}} \approx 8.0\text{ g/cm}^3$ (thép không gỉ).
- Hệ số bù giản nở nhiệt bình chứa thủy tinh $\gamma_{\text{vessel}} \approx 10 \times 10^{-6}\text{ K}^{-1}$ (rất nhỏ, bỏ qua trong dải $20 \pm 5^\circ\text{C}$).

### 4.2 Công thức Hệ số Xung $K$-factor & Tích phân Lưu lượng

1. **Hệ số xung thực nghiệm ($K_{\text{factor}}$ tính bằng pulses/Litre):**
   $$K_{\text{factor}} = \frac{N_{\text{pulses}}}{V_{\text{ref}}} \quad (\text{xung / Lít})$$

2. **Lưu lượng tức thời ($Q$ tính bằng Lít/phút - $\text{L/min}$):**
   $$Q(t) = \frac{\Delta N_{\text{pulses}}}{\Delta t_{\text{sample\_ms}}} \times \frac{60 \times 1000}{K_{\text{factor}}} = \frac{f_{\text{pulse\_Hz}}}{K_{\text{factor}}} \times 60 \quad (\text{L/min})$$

3. **Tổng thể tích đã phân phối ($V_{\text{delivered}}$ tính bằng Lít):**
   $$V_{\text{delivered}} = \frac{N_{\text{total\_pulses}}}{K_{\text{factor}}} \quad (\text{Lít})$$

### 4.3 Mô hình Hiệu Chuẩn Đa Điểm Tuyến Tính Từng Đoạn (Multi-Point Piecewise Calibration)

Do cảm biến lưu lượng cơ học (bánh răng Oval hoặc cánh Turbine) có đặc tính phi tuyến ở dải lưu lượng thấp do ma sát cơ khí và rò rỉ khe hở (slippage), hệ thống áp dụng mô hình nội suy tuyến tính từng đoạn (Piecewise Linear Interpolation) với 5 điểm chuẩn:

$$\{ (Q_1, K_1), (Q_2, K_2), (Q_3, K_3), (Q_4, K_4), (Q_5, K_5) \}$$

Khi firmware đo được tần số xung tức thời $f$:
1. Xác định phân đoạn $[Q_k, Q_{k+1}]$ tương ứng.
2. Nội suy $K(f)$:
   $$K(f) = K_k + \frac{K_{k+1} - K_k}{f_{k+1} - f_k} \times (f - f_k)$$
3. Với $f < f_1$: Áp dụng ngưỡng cắt dòng chảy thấp (Low Flow Cutoff), nếu $f < f_{\text{min\_valid}}$ $\to$ xem như dòng chảy bằng $0$.
4. Với $f > f_5$: Cảnh báo lưu lượng vượt ngưỡng tối đa (Over-range Alert).

---

## 5. Ma Trận Dải Đo & Điều Kiện Thử Nghiệm Đa Điểm (Calibration Matrix)

| Điểm đo (Point) | Lưu lượng danh định ($Q$) | Áp suất kiểm tra ($P$) | Trạng thái tải thủy lực | Số lần thử tối thiểu ($M$) | Thể tích chuẩn tối thiểu ($V_{\text{ref}}$) |
|---|---|---|---|---|---|
| **Point 1 (Min / Rò rỉ)** | $0.35\text{ L/min}$ | $1.5\text{ bar}$ ($22\text{ PSI}$) | Van kim vi chỉnh hé mở | 5 lần | $1000\text{ mL}$ |
| **Point 2 (Low / 1 Béc)** | $1.20\text{ L/min}$ | $2.5\text{ bar}$ ($36\text{ PSI}$) | 1 béc phun sương $0.4\text{mm}$ | 5 lần | $1000\text{ mL}$ |
| **Point 3 (Mid / 2 Béc)** | $2.50\text{ L/min}$ | $3.5\text{ bar}$ ($50\text{ PSI}$) | 2 béc phun sương $0.4\text{mm}$ | 5 lần | $2000\text{ mL}$ |
| **Point 4 (High / 4 Béc)** | $4.00\text{ L/min}$ | $4.5\text{ bar}$ ($65\text{ PSI}$) | 4 béc phun sương $0.4\text{mm}$ | 5 lần | $2000\text{ mL}$ |
| **Point 5 (Max / Xả thẳng)**| $5.50\text{ L/min}$ | $5.2\text{ bar}$ ($75\text{ PSI}$) | Van xả hoàn toàn mở | 5 lần | $2000\text{ mL}$ |

### 5.1 Điều kiện Môi trường & Chất lưu Bắt buộc:
- **Môi chất:** Nước sạch khử khoáng/RO, độ dẫn điện $\text{EC} < 100\mu\text{S/cm}$, $\text{TDS} < 50\text{ ppm}$, lọc qua màng $100\text{ mesh}$ ($150\mu\text{m}$).
- **Nhiệt độ nước:** $25.0^\circ\text{C} \pm 2.0^\circ\text{C}$ (Ghi nhận chính xác giá trị $T$ cho từng lần đo).
- **Nguồn cấp bơm:** Nguồn DC tuyến tính/chính xác $12.00\text{V} \pm 0.05\text{V}$, đo dòng tiêu thụ liên tục.
- **Độ ổn định áp suất:** Biến thiên áp suất trong suốt quá trình chạy $\Delta P \le \pm 0.1\text{ bar}$.

---

## 6. Thống Kê Sai Số, Tiêu Chí Đạt/Loại & Phát Hiện Outlier (Statistical Quality Control)

### 6.1 Các Đại Lượng Thống Kê Cần Tính Cho Mỗi Điểm Đo

Với $M \ge 3$ lần thử (khuyến nghị $M=5$) tại cùng một điểm lưu lượng:
1. **Giá trị trung bình của thể tích đo:**
   $$\bar{V} = \frac{1}{M} \sum_{i=1}^M V_i$$

2. **Độ lệch chuẩn thực nghiệm (Standard Deviation):**
   $$s = \sqrt{\frac{1}{M - 1} \sum_{i=1}^M (V_i - \bar{V})^2}$$

3. **Sai số độ lặp lại (Repeatability Relative Error - $E_{\text{rep}}$):**
   $$E_{\text{rep}} = \frac{t_{0.95, M-1} \cdot s}{\bar{V} \cdot \sqrt{M}} \times 100\% \quad \text{hoặc đơn giản hóa: } \frac{2s}{\bar{V}} \times 100\%$$

4. **Sai số tương đối so với chuẩn (Accuracy Error - $E_{\text{acc}}$):**
   $$E_{\text{acc}} = \frac{\bar{V} - V_{\text{ref}}}{V_{\text{ref}}} \times 100\%$$

### 6.2 Tiêu Chí Chấp Thuận / Loại Bỏ Cảm Biến (Accept / Reject Thresholds)

| Thông số đánh giá | Ngưỡng Chấp Thuận (PASS) | Ngưỡng Cảnh Báo (WARNING) | Ngưỡng Loại Bỏ (REJECT / FAIL) | Hành động khi FAIL |
|---|---|---|---|---|
| **Độ lặp lại $E_{\text{rep}}$** | $\le 1.0\%$ | $1.01\% - 1.50\%$ | $> 1.50\%$ | Loại bỏ cảm biến, kiểm tra kẹt cơ khí / bọt khí. |
| **Sai số sau Calib $E_{\text{acc}}$** | $\le \pm 1.5\%$ | $\pm 1.51\% - \pm 2.0\%$ | $> \pm 2.0\%$ | Tính toán lại đa thức nội suy hoặc loại bỏ. |
| **Độ tuyến tính $R^2$ ($K$-factor)** | $\ge 0.995$ | $0.990 - 0.994$ | $< 0.990$ | Chuyển sang mô hình nội suy từng đoạn 5 điểm. |
| **Dòng rò điểm dừng (Zero Leak)**| $0\text{ xung}$ trong $60\text{s}$ | $1\text{ xung}$ trong $60\text{s}$ | $> 1\text{ xung}$ trong $60\text{s}$ | Sửa van chống rò rỉ, kiểm tra nhiễu điện từ. |

### 6.3 Quy Tắc Xử Lý Dữ Liệu Ngoại Lai (Outlier Detection - Grubbs' Test)
Nếu một điểm đo $V_k$ có dấu hiệu bất thường do thao tác hoặc bọt khí, áp dụng kiểm định Grubbs:
$$G_{\text{calc}} = \frac{|V_k - \bar{V}|}{s}$$
So sánh với giá trị tới hạn $G_{\text{critical}}(\alpha=0.05, M=5) = 1.672$.
- Nếu $G_{\text{calc}} > G_{\text{critical}}$: Loại bỏ mẫu $V_k$, ghi nhật ký nguyên nhân và tiến hành đo bổ sung 1 lần thử mới để đảm bảo đủ $M=5$ mẫu hợp lệ.

---

## 7. Quy Trình Vận Hành An Toàn Băng Thử (Flow Bench Safety Protocols)

Thử nghiệm thủy lực kết hợp điện áp $12\text{V} - 220\text{V}$ và áp suất lên đến $6\text{ bar}$ tiềm ẩn nguy cơ chập điện, rò nước và vỡ ống. Mọi kỹ thuật viên phải tuân thủ 4 lớp bảo vệ an toàn:

### 7.1 Bảo Vệ Cách Ly Điện - Nước (Electrical & Fluid Isolation)
1. **Phân vùng không gian (Spatial Segregation):**
   - Vùng ướt (Wet Zone): Chứa thùng nước, bơm, đường ống, van và cảm biến đo.
   - Vùng khô (Dry Zone): Chứa Gateway, Remote Node MCU, bộ chuyển đổi nguồn AC/DC và máy tính đo lường.
   - Vùng khô phải được đặt cao hơn vùng ướt tối thiểu $30\text{ cm}$ và cách xa theo phương ngang $\ge 50\text{ cm}$. Có vách ngăn acrylic/mica trong suốt chống tia nước bắn.
2. **Tiếp địa & Chống rò điện (GFCI / RCD Protection):**
   - Toàn bộ nguồn AC cấp cho thiết bị và nguồn $12\text{V}$ bắt buộc đi qua Aptomat chống rò RCBO $30\text{mA}$ / thời gian ngắt $< 30\text{ms}$.
   - Khung giá đỡ kim loại của băng thử được nối đất bảo vệ (PE ground) với điện trở nối đất $R_{\text{earth}} < 4\Omega$.

### 7.2 Bảo Vệ Tràn Nước & Rò Rỉ Đường Ống (Catch Basin & Leak Detection)
1. **Khay hứng chống tràn (Secondary Containment Tray):** Đặt toàn bộ đường ống vùng ướt trong khay inox/nhựa có dung tích $\ge 25\text{ L}$ (lớn hơn $120\%$ tổng thể tích thùng chứa $20\text{ L}$).
2. **Cảm biến rò rỉ tự động (Optical/Conductive Leak Sensor):** Gắn cảm biến phát hiện nước ở đáy khay hứng. Nếu có nước đọng $> 2\text{ mm}$, tín hiệu ngắt phần cứng lập tức kích hoạt relay ngắt nguồn toàn bộ bơm trong $\le 50\text{ms}$.

### 7.3 Bảo Vệ Chống Chạy Khô (Dry-Run Protection)
- Bơm màng Aeroponics nếu chạy không tải trong buồng kín $> 60\text{s}$ sẽ quá nhiệt hỏng màng cao su EPDM và cháy motor.
- **Phao báo mức thấp (Low Level Float Switch):** Gắn ở mức $15\%$ dung tích thùng chứa ($3\text{ L}$). Công tắc nối tiếp trực tiếp vào mạch điều khiển relay nguồn. Mực nước dưới ngưỡng $\to$ mạch hở $\to$ bơm cưỡng bức tắt độc lập với vi điều khiển.

### 7.4 Bảo Vệ Quá Áp & Dừng Khẩn Cấp (Overpressure & Emergency Shut-Off)
1. **Van xả áp cơ khí (Mechanical Pressure Relief Valve - PRV):** Lắp ngay sau ngõ ra bơm, cài đặt mở tự động khi áp suất vượt quá $6.0\text{ bar}$ ($87\text{ PSI}$), dẫn dòng hồi trực tiếp về thùng chứa không qua béc.
2. **Nút bấm ngắt khẩn cấp cơ học (E-Stop Pushbutton):** Nút nấm đỏ có khóa xoay bố trí ngay mặt trước bàn thí nghiệm, ngắt đồng thời cả 2 cực nguồn điện $12\text{V}$ và $220\text{V}$.
3. **Van bi khóa nhanh 1/4 vòng (Quarter-Turn Emergency Ball Valve V1):** Lắp ở đầu hút bơm, cho phép cô lập nguồn nước ngay lập tức khi phát hiện nứt vỡ ống.

---

## 8. Cấu Trúc Dữ Liệu & Hợp Đồng Cơ Sở Dữ Liệu / Firmware (Data Contract)

### 8.1 Schema Lưu Trữ Lịch Sử Hiệu Chuẩn (`sensor_calibrations`)
Theo thiết kế Sprint 0–1 Remediation (R5), thông tin hiệu chuẩn được quản lý bất biến:

```sql
CREATE TABLE IF NOT EXISTS sensor_calibrations (
    calibration_id       UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    sensor_serial        VARCHAR(64) NOT NULL,
    node_id              SMALLINT NOT NULL,
    version              INTEGER NOT NULL DEFAULT 1,
    pulses_per_litre     NUMERIC(10, 4) NOT NULL,
    zero_offset_lpm      NUMERIC(6, 4) NOT NULL DEFAULT 0.0000,
    operating_temp_c     NUMERIC(5, 2) NOT NULL DEFAULT 25.00,
    operating_pressure_bar NUMERIC(5, 2) NOT NULL DEFAULT 3.50,
    repeatability_pct    NUMERIC(5, 3) NOT NULL,
    accuracy_error_pct   NUMERIC(5, 3) NOT NULL,
    multi_point_data_json JSONB NULL,
    calibration_hash     VARCHAR(64) NOT NULL,
    is_active            BOOLEAN NOT NULL DEFAULT TRUE,
    calibrated_at        TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    created_at           TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT uq_sensor_version UNIQUE (sensor_serial, version),
    CONSTRAINT chk_pulses_per_litre CHECK (pulses_per_litre > 0 AND pulses_per_litre < 50000),
    CONSTRAINT chk_repeatability CHECK (repeatability_pct >= 0 AND repeatability_pct <= 5.0)
);

CREATE INDEX IF NOT EXISTS idx_calibrations_active ON sensor_calibrations (node_id, is_active);
```

### 8.2 Cấu Trúc Dữ Liệu Firmware C++ (Deterministic Zero-Allocation Model)

```cpp
struct CalibrationPoint {
    uint16_t flow_lpm_x100;     // Ví dụ: 250 = 2.50 L/min
    uint16_t pulse_freq_hz_x10; // Ví dụ: 1850 = 185.0 Hz
    uint32_t pulses_per_litre;  // Ví dụ: 4440
};

struct SensorCalibrationProfile {
    uint32_t calibration_id;
    uint32_t version;
    uint8_t node_id;
    char sensor_serial[16];
    uint32_t nominal_pulses_per_litre; // K-factor mặc định
    uint16_t low_flow_cutoff_lpm_x100; // Ngưỡng cắt dòng chảy nhỏ (ví dụ 15 = 0.15 L/min)
    uint16_t max_flow_limit_lpm_x100;  // Ngưỡng báo động vượt dải (ví dụ 600 = 6.00 L/min)
    uint8_t num_calibration_points;    // Thường là 5 điểm
    CalibrationPoint points[5];
    uint32_t checksum_crc32;
};
```

---

## 9. Quy Trình Thực Hiện Hiệu Chuẩn 8 Bước Tiêu Chuẩn (Step-by-Step SOP)

```
 [ BƯỚC 1: KIỂM TRA AN TOÀN TRƯỚC VẬN HÀNH ]
 -> Khay chứa khô ráo, ELCB hoạt động, E-Stop mở, mức nước >= 80%, phao an toàn ON.
                       |
                       v
 [ BƯỚC 2: ĐUỔI BỌT KHÍ KHỞI ĐỘNG (AIR PURGING) ]
 -> Mở van xả thẳng, bật bơm chạy 30s ở áp suất 2.0 bar cho đến khi dòng chảy trong suốt không bọt.
                       |
                       v
 [ BƯỚC 3: CÂN TARA BÌNH CHỨA CHUẨN ]
 -> Đặt bình đo lên cân phân tích, bấm TARE (Zero = 0.0g), đo nhiệt độ nước T (°C).
                       |
                       v
 [ BƯỚC 4: THỰC HIỆN LẦN THỬ THEO ĐIỂM ĐO ]
 -> Chỉnh van kim NV1 đạt lưu lượng mục tiêu Q_k.
 -> Kích hoạt đo: Node bắt đầu đếm xung N_pulses đồng thời chuyển van xả vào bình chuẩn.
 -> Khi đạt thể tích V_ref >= 1000mL (hoặc 2000mL), chuyển van xả hồi, dừng đếm xung.
                       |
                       v
 [ BƯỚC 5: GHI NHẬN KHỐI LƯỢNG & TÍNH TOÁN ]
 -> Cân khối lượng m_water, đọc số xung N_pulses và thời gian delta_t.
 -> Tính V_ref(T), tính K_i = N_pulses / V_ref.
                       |
                       v
 [ BƯỚC 6: LẶP LẠI THỬ NGHIỆM (M >= 5 LẦN) ]
 -> Lặp lại Bước 3-5 đủ 5 lần cho điểm lưu lượng Q_k.
 -> Tính mean K_k, s, E_rep. Nếu E_rep > 1.0% -> Kiểm tra lại hệ thống và đo lại.
                       |
                       v
 [ BƯỚC 7: CHUYỂN ĐIỂM ĐO TIẾP THEO TRONG DẢI (1 -> 5) ]
 -> Lặp lại Bước 4-6 cho toàn bộ 5 điểm trong ma trận.
                       |
                       v
 [ BƯỚC 8: TỔNG HỢP HỒ SƠ & PHÁT HÀNH PROFILE HIỆU CHUẨN ]
 -> Tính toán bảng nội suy, tạo mã băm SHA256/CRC32, lưu DB và cấp phát version mới.
```

---

## 10. Biểu Mẫu Ghi Nhận Dữ Liệu Thực Nghiệm (Calibration Certificate Template)

```
========================================================================================
                  AEROPONICS FLOW SENSOR CALIBRATION CERTIFICATE
========================================================================================
Certificate No: CAL-20260817-NODE01-S01          Date: 2026-08-17
Sensor Model:   OF06ZAT Oval Gear Flowmeter      Sensor Serial: OF06-2026-0042
Target Node ID: Node 01 (Sprint 1.5 POC)         Firmware Ver:  v1.5.0-poc
Reference Std:  KERN PCB 6000-1 (Cal: 2026-06)   Operator:      Antigravity QA
Fluid:          Purified Water (TDS: 32 ppm)     Fluid Temp:    25.2 °C
Ambient Temp:   26.1 °C                          Atm Pressure:  1013.2 hPa
----------------------------------------------------------------------------------------
Calibration Results (5-Point Multi-Range):
Point | Target Q | Ref Mass (g) | Vol V_ref (L) | Pulses (N) | K-factor (p/L) | E_rep (%)
------+----------+--------------+---------------+------------+----------------+----------
  1   | 0.35 L/m |   997.1 g    |   1.0000 L    |    4410    |     4410.0     |  0.62 %
  2   | 1.20 L/m |   997.2 g    |   1.0001 L    |    4438    |     4437.6     |  0.41 %
  3   | 2.50 L/m |  1994.3 g    |   2.0002 L    |    8912    |     4455.6     |  0.35 %
  4   | 4.00 L/m |  1994.0 g    |   1.9999 L    |    8940    |     4470.2     |  0.28 %
  5   | 5.50 L/m |  1994.4 g    |   2.0003 L    |    8968    |     4483.3     |  0.31 %
----------------------------------------------------------------------------------------
Summary Statistics:
- Nominal K-Factor:         4451.3 pulses/Litre
- Maximum Repeatability:    0.62 % (PASS <= 1.0 %)
- Max Post-Cal Error:       0.38 % (PASS <= 1.5 %)
- Zero-Flow Leak Pulses:    0 pulses / 60s (PASS)
- Calibration Audit Hash:   SHA256: 7d9a8e2b...f419c80a
========================================================================================
DECISION: [X] APPROVED / PASS           [ ] REJECTED
Senior Solution Architect Signature: _______________________
========================================================================================
```

---
*Senior Solution Architect — Bản đặc tả kỹ thuật Flow Bench & Hiệu chuẩn đo lường ban hành ngày 2026-08-17.*
