#include <Arduino.h> // Hoặc <stdint.h>, <stdbool.h> nếu dùng ESP-IDF thuần
#include <stdint.h>
#include <stdbool.h>

/* ==============================================================================

HÀM 1: CalCRC16 (Dành cho bên gửi)

Cấu trúc buffer đầu vào: [len] [cmd] [data1]...

Hoạt động:

Tính toán CRC cho chiều dài mới (len + 2) và dữ liệu.

Ghi đè chiều dài mới vào buffer[0].

Gắn CRC_Lo và CRC_Hi vào cuối buffer.

LƯU Ý: Mảng buffer phải được cấp phát dư ít nhất 2 byte để chứa CRC.

============================================================================== */

void CalCRC16(uint8_t buffer) {
        uint16_t crc = 0xFFFF; // Khởi tạo Modbus

        // Đọc chiều dài dữ liệu gốc (không bao gồm chính byte len)
        uint8_t original_len = buffer[0];

        // Tổng số byte cần quét = 1 byte (len) + số byte dữ liệu
        uint8_t total_len = original_len + 1;

        // Cập nhật chiều dài mới chứa cả CRC (tăng thêm 2)
        buffer[0] = original_len + 2;

        // Quét toàn bộ dữ liệu (bắt đầu từ byte len mới)
        for (uint8_t i = 0; i < total_len; i++) {
                crc ^= buffer[i]; // crc_lo = crc_lo XOR data

                // Vòng lặp 8-bit
                for (uint8_t j = 0; j < 8; j++) {
                        if (crc & 0x0001) { // Nếu cờ Carry (bit 0) == 1
                                crc = (crc >> 1) ^ 0xA001;
                        } else {
                                crc >>= 1;
                        }
                }
        }

        // Ghi 2 byte CRC vào cuối mảng
        // Vị trí đuôi chính là index = total_len và total_len + 1
        buffer[total_len] = (uint8_t)(crc & 0xFF);         // Ghi CRC_Lo
        buffer[total_len + 1] = (uint8_t)((crc >> 8) & 0xFF); // Ghi CRC_Hi
}

/* ==============================================================================

HÀM 2: CheckCRC16 (Dành cho bên nhận)

Cấu trúc buffer đầu vào: [len] [cmd] [data1]... [crc_lo] [crc_hi]

Hoạt động:

Đọc tổng chiều dài từ buffer[0] (đã bao gồm 2 byte CRC).

Quét qua toàn bộ mảng.

Nếu dữ liệu không lỗi, kết quả CRC tự triệt tiêu về 0x0000.

============================================================================== */

bool CheckCRC16(const uint8_t buffer) {
        uint16_t crc = 0xFFFF;

        // Tổng số byte cần quét = 1 byte (len) + số byte dữ liệu (đã có CRC)
        uint8_t total_len = buffer[0] + 1;

        for (uint8_t i = 0; i < total_len; i++) {
                crc ^= buffer[i];

                 for (uint8_t j = 0; j < 8; j++) {
                     if (crc & 0x0001) {
                         crc = (crc >> 1) ^ 0xA001;
                     } else {
                         crc >>= 1;
                     }
                 }
        }

        // Đặc tính phép chia đa thức: chuẩn xác thì crc == 0
        return (crc == 0x0000);
}

/* ==============================================================================

HÀM 3: sendComm

Nhiệm vụ: Nhận chuỗi lệnh thô, tự động tính/ghép CRC16 và thực hiện gửi đi.

Tham số: sc - Con trỏ tới mảng dữ liệu (phải được cấp phát dư ít nhất 2 byte).

============================================================================== */

void sendComm(uint8_t sc) {
        // 1. Tính toán và tự động ghép 2 byte CRC vào cuối chuỗi sc, đồng thời tăng len thêm 2.
        // Hàm CalCRC16 đã được thiết kế hoàn hảo để xử lý trực tiếp trên con trỏ này.
        CalCRC16(sc);

        // 2. Xác định tổng số byte cần gửi đi ra ngoại vi
        // Lưu ý: sc[0] lúc này chứa chiều dài dữ liệu (đã cộng 2 byte CRC).
        // Nhưng khi truyền UART/RS485, ta phải truyền cả chính byte sc[0] đó đi.
        // Vậy tổng số byte thực tế đẩy ra đường truyền là: sc[0] + 1
        uint8_t total_bytes_to_send = sc[0] + 1;

        // 3. Thực hiện gửi đi
        Serial.print("=> [sendComm] Truyen du lieu ra thiet bi: ");
        for(uint8_t i = 0; i < total_bytes_to_send; i++) {
                Serial.printf("%02X ", sc[i]);
        }
        Serial.println();

// -------------------------------------------------------------
// GỬI THỰC TẾ TRÊN NỀN TẢNG ESP32:
// Nếu bạn dùng phần cứng UART (VD: Serial2 nối với IC MAX485),
// hãy mở comment dòng dưới đây:
//
// Serial2.write(sc, total_bytes_to_send);
// -------------------------------------------------------------
}

/* ==============================================================================

VÍ DỤ SỬ DỤNG TRÊN ESP32

============================================================================== */

void setup() {
        Serial.begin(38400);
        Serial.println("\nESP32 Da khoi dong. Bat dau chu trinh dieu khien Bom...");
}



void loop() {
// ------------------------------------------------------------------
// LƯU Ý QUAN TRỌNG:
// Phải khởi tạo lại mảng lệnh gốc ở mỗi vòng lặp vì hàm CalCRC16
// sẽ ghi đè và làm thay đổi trực tiếp kích thước của mảng.
// Cấp phát dư (ví dụ 10 bytes) để an toàn bộ nhớ khi chèn thêm CRC.
// ------------------------------------------------------------------

        // 1. Kịch bản BẬT BƠM (onPump)
        uint8_t onPump[10] = {0x02, 0x06, 0x08};

        Serial.println("\n[1] --- GUI LENH BAT BOM (onPump) ---");
        sendComm(onPump); // Gửi lệnh đi (hàm tự tính CRC, in ra Serial)

        // (Tuỳ chọn: Kiểm tra luôn gói tin vừa gửi xem hàm CheckCRC16 hoạt động không)
        if(CheckCRC16(onPump)) {
            Serial.println("    -> Kiem tra thu lai: Packet onPump dung chuan CRC16!");
        }

        Serial.println("    -> Cho 5 giay...");
        delay(5000); 

        // 2. Kịch bản TẮT BƠM (offPump)
        uint8_t offPump[10] = {0x02, 0x07, 0x08};

        Serial.println("\n[2] --- GUI LENH TAT BOM (offPump) ---");
        sendComm(offPump);

        Serial.println("    -> Cho 7 giay...");
        delay(7000); 
}
