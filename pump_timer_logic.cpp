// Cấu trúc lưu thời gian ON và OFF cho một buổi (tính bằng giây)
struct CycleParams {
  long onSeconds;
  long offSeconds;
};

// Cấu trúc định nghĩa một cấu hình nghiệm thức hoàn chỉnh (gồm ngày và đêm)
struct TreatmentConfig {
  CycleParams day;
  CycleParams night;
};

/**
 * Hàm kiểm tra trạng thái bơm tổng quát
 * @param h, m, s: Giờ, phút, giây hiện tại (h: 0-23)
 * @param config: Cấu hình chu kỳ ON/OFF của nghiệm thức
 * @param dayStartH: Giờ bắt đầu ban ngày (mặc định 6h sáng)
 * @param nightStartH: Giờ bắt đầu ban đêm (mặc định 18h tối)
 * @return 1 nếu bơm cần ON, 0 nếu bơm cần OFF
 */
int getPumpState(int h, int m, int s, TreatmentConfig config, int dayStartH = 6, int nightStartH = 18) {
  // 1. Kiểm tra tính hợp lệ của input thời gian
  if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) {
    return 0; 
  }

  // 2. Xác định thời điểm hiện tại là Ban Ngày hay Ban Đêm
  bool isDaytime = false;
  if (dayStartH < nightStartH) {
    // Ví dụ: Ngày từ 6h đến 18h
    isDaytime = (h >= dayStartH && h < nightStartH);
  } else {
    // Trường hợp đặc biệt nếu ca ngày vắt qua nửa đêm (ví dụ: 18h đến 6h sáng hôm sau)
    isDaytime = (h >= dayStartH || h < nightStartH);
  }

  // 3. Lấy thông số chu kỳ và mốc giờ bắt đầu tương ứng với buổi hiện tại
  CycleParams activeParams = isDaytime ? config.day : config.night;
  int startHour = isDaytime ? dayStartH : nightStartH;

  // Nếu thời gian ON = 0 thì chắc chắn bơm luôn OFF
  if (activeParams.onSeconds <= 0) {
    return 0;
  }

  // 4. Tính tổng thời gian của 1 chu kỳ (ON + OFF)
  long cycleDuration = activeParams.onSeconds + activeParams.offSeconds;
  
  // Tránh lỗi chia cho 0 nếu ai đó cấu hình chu kỳ = 0
  if (cycleDuration <= 0) return 0;

  // 5. Tính số giây đã trôi qua kể từ mốc giờ bắt đầu (startHour)
  // Công thức này tự động xử lý việc vắt qua mốc 0h (nửa đêm)
  long elapsedHours = (h >= startHour) ? (h - startHour) : (h + 24 - startHour);
  long elapsedSeconds = elapsedHours * 3600L + m * 60L + s;

  // 6. Tìm vị trí của thời điểm hiện tại trong chu kỳ đang chạy
  long positionInCycle = elapsedSeconds % cycleDuration;

  // 7. Quyết định trạng thái (nếu nằm trong khoảng thời gian ON thì trả về 1)
  if (positionInCycle < activeParams.onSeconds) {
    return 1;
  } else {
    return 0;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- TEST THUAT TOAN TONG QUAT ---");

  // Định nghĩa các cấu hình dựa theo bảng nghiệm thức cũ (để chứng minh tính tổng quát)
  
  // Nghiệm thức 1: Ngày (ON 30s, OFF 10p) - Đêm (ON 30s, OFF 30p)
  TreatmentConfig M1 = {
    {30, 10 * 60}, // Day params
    {30, 30 * 60}  // Night params
  };

  // Nghiệm thức 2: Ngày (ON 30s, OFF 15p) - Đêm (ON 30s, OFF 30p)
  TreatmentConfig M2 = {
    {30, 15 * 60}, 
    {30, 30 * 60}  
  };

  // Cấu hình tùy chỉnh (Custom): Ngày (ON 1 phút, OFF 5 phút) - Đêm (Không bật)
  TreatmentConfig customConfig = {
    {60, 5 * 60}, // Day
    {0, 0}        // Night (Tắt hoàn toàn)
  };

  // --- CHẠY TEST CASE ---
  
  // Test 1: M1 lúc 06:00:15 -> Phải ON (1)
  Serial.print("M1 @ 06:00:15 : "); 
  Serial.println(getPumpState(6, 0, 15, M1));

  // Test 2: M1 lúc 06:01:00 (Trong lúc nghỉ) -> Phải OFF (0)
  Serial.print("M1 @ 06:01:00 : "); 
  Serial.println(getPumpState(6, 1, 0, M1));

  // Test 3: M2 lúc 18:30:10 (Thuộc ban đêm của M2) -> Phải ON (1) vì chu kỳ đêm là 30p rưỡi
  Serial.print("M2 @ 18:30:10 : "); 
  Serial.println(getPumpState(18, 30, 10, M2));

  // Test 4: Custom lúc 10:00:30 (Trong thời gian 1 phút ON) -> Phải ON (1)
  Serial.print("Custom @ 10:00:30 : "); 
  Serial.println(getPumpState(10, 0, 30, customConfig));
  
  // Test 5: Custom lúc 22:00:00 (Ban đêm tắt hoàn toàn) -> Phải OFF (0)
  Serial.print("Custom @ 22:00:00 : "); 
  Serial.println(getPumpState(22, 0, 0, customConfig));
}

void loop() {
  // Để trống trong ví dụ
}