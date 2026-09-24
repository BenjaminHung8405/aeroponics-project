# Sprint 1: Gateway Transport & Codec Refactoring (ESP32)

> **Phụ thuộc:** Không có (Bottom-up đầu tiên — RF Wire Codec).
> **Output bàn giao:** Codec AGU legacy chuẩn hóa nodeId + zero-sum canonical, UART HC-12 cô lập trên FreeRTOS Core 1, toàn bộ 273/273 native unit tests PASS.
> **Golden Baseline tham chiếu:** [`docs/interface-wire-contract.md`](../../docs/interface-wire-contract.md) §3–§5, [`docs/STATE_MACHINE_MATRIX.md`](../../docs/STATE_MACHINE_MATRIX.md) §6.

---

## 1. PHẠM VI & MỤC TIÊU

### 1.1 Modules / Components bị tác động trực tiếp

| Module / File | Hành động | Mô tả |
|---|---|---|
| `aeroponics-firmware/include/agu_legacy_codec.h` | **Sửa** | Thêm tham số `nodeId` vào `encodeReadRamBurst`, chuẩn hóa `calculateZeroSumChecksum`, `verifyZeroSumChecksum` |
| `aeroponics-firmware/src/agu_legacy_codec.cpp` | **Sửa** | Loại bỏ byte hardcode rác trong `encodeWriteRam`, chuẩn hóa zero-sum, cập nhật `decodeBurstRam` |
| `aeroponics-firmware/include/uart_rf_transport.h` | **Sửa** | Thêm FreeRTOS queue handle, Core 1 task config, HC-12 baud constant |
| `aeroponics-firmware/src/uart_rf_transport.cpp` | **Sửa** | Cô lập UART RX interrupt handler trên Core 1, thêm bounded ring buffer anti-overrun |
| `aeroponics-firmware/include/config.h` | **Sửa** | Thêm `RF_UART_HC12_BAUD_RATE`, `RF_UART_CORE_PIN`, `RF_UART_RX_QUEUE_DEPTH`, `RF_UART_RING_BUFFER_SIZE` |
| `aeroponics-firmware/src/main.cpp` | **Sửa** | Cập nhật khởi tạo UART transport với Core 1 pinning, cập nhật hàm `executeAguPump`/`executeAguPing` gọi codec đã refactor |
| `aeroponics-firmware/src/agu_legacy_rf_host.cpp` | **Sửa** | Đồng bộ signature gọi `encodeReadRamBurst` mới |
| `aeroponics-firmware/include/agu_legacy_rf_host.h` | **Sửa** | Thêm `AguRfCommand::READ_RAM_BURST` enum value |
| `aeroponics-firmware/test/test_codec/test_codec.cpp` | **Sửa** | Cập nhật unit test vector theo codec signature mới |
| `aeroponics-firmware/test/test_transport/test_transport.cpp` | **Sửa** | Thêm test anti-overrun ring buffer |

### 1.2 Mục tiêu cụ thể Sprint 1

- [ ] `encodeReadRamBurst(nodeId, addr, count)` nhận đủ 3 tham số bắt buộc, trả frame `[0x0E, addr_lo, addr_hi, count, nodeId]`.
- [ ] Zero-sum checksum chuẩn hóa: `checksum = (-sum([Length][Opcode][Params])) mod 256`, mọi encoder pass invariant `sum(frame) & 0xFF == 0`.
- [ ] Byte hardcode rác trong `encodeWriteRam` bị loại bỏ hoặc thay bằng giá trị có nghĩa theo contract.
- [ ] UART HC-12 RX chạy trên FreeRTOS Core 1, Wi-Fi/MQTT trên Core 0, không còn cross-interference.
- [ ] Ring buffer bounded, chống overrun khi 4 node polling liên tục ở nhịp 1s.
- [ ] Toàn bộ 273/273 native unit tests PASS, PlatformIO build sạch.

---

## 2. KIẾN TRÚC & LUỒNG DỮ LIỆU

### 2.1 Sơ đồ Dependency của các Module Sprint 1

```text
main.cpp
  │
  ├──▶ config.h                    (Thêm: HC-12 baud, Core pinning, ring buffer size)
  │
  ├──▶ agu_legacy_codec.h/cpp      (Sửa: encodeReadRamBurst signature, zero-sum canonical)
  │       └── Cung cấp: encodePumpOn/Off/Ping/ReadRamBurst/WriteRam, decodeBurstRam
  │
  ├──▶ uart_rf_transport.h/cpp     (Sửa: Core 1 FreeRTOS task, bounded ring buffer)
  │       └── Cung cấp: begin(), send(), receive(), available(), flushRx()
  │
  ├──▶ agu_legacy_rf_host.h/cpp    (Sửa: READ_RAM_BURST command, encodeReadRamBurst call)
  │       └── Cung cấp: transact(), pingNode(), setPump(), readRamBurst()
  │
  └──▶ test_codec / test_transport (Cập nhật theo signature mới)
```

### 2.2 Luồng Dữ Liệu Codec Refactoring (Zero-Sum Canonical)

```text
[Caller: main.cpp / agu_legacy_rf_host.cpp]
        │
        │ Gọi encodeReadRamBurst(nodeId, addr, count, outBuf, outSize)
        ▼
[AguLegacyCodec::encodeReadRamBurst]
        │
        ├── Validate: count == BURST_DATA_SIZE (8) → nếu không, return 0
        │
        ├── Xây payload: [0x0E, addr_lo, addr_hi, count, nodeId]
        │
        ▼
[formatSendComPacket(payload, payloadLen, outBuf, outSize)]
        │
        ├── outBuf[0] = payloadLen + 1  (Length byte)
        ├── outBuf[1..1+payloadLen-1] = payload
        ├── sum = Length + sum(payload)
        ├── checksum = (0x100 - sum) & 0xFF  ← Two's complement zero-sum
        ├── outBuf[1+payloadLen] = checksum
        │
        ▼
[Kiểm tra invariant: sum(outBuf[0..len-1]) & 0xFF == 0]
        │
        ▼
[Return frame_len = payloadLen + 2]
        │
        ▼
[UartRfTransport::send(frame, frame_len)]
        │
        ▼
[HC-12 module over UART → RF 433 MHz → ATmega8 node]
```

### 2.3 Luồng UART HC-12 FreeRTOS Core 1 Isolation

```text
┌─────────────────────────────────────────────────────────────┐
│                      ESP32-S3 Dual Core                     │
├────────────────────────────┬────────────────────────────────┤
│         Core 0             │           Core 1               │
│   (Protocol Core)          │    (Application / RF Core)     │
├────────────────────────────┼────────────────────────────────┤
│ • Wi-Fi driver             │ • UART HC-12 RX ISR handler    │
│ • LwIP stack               │ • Ring buffer consumer task    │
│ • esp_event loop           │ • agu_legacy_rf_host transact()│
│ • wifi_ctrl_task           │ • mqtt_task (existing)         │
│ • Main loop()              │ • GroupScheduler tick          │
│                            │ • NodeRegistry update          │
├────────────────────────────┴────────────────────────────────┤
│                                                             │
│  [HardwareSerial UART1] ──► [RX Ring Buffer (bounded)]      │
│        ▲                         │                          │
│        │ Core 1 ISR              │ Core 1 task              │
│        │ writes to ring buffer   │ reads from ring buffer   │
│        │                         ▼                          │
│        │              [agu_legacy_rf_host.transact()]        │
│        │                         │                          │
│        └───── send() ◄───────────┘                          │
│                                                             │
│  Data Flow:                                                 │
│  HC-12 RX pin (GPIO18) → ISR → ring_buffer → Core 1 task   │
│  → decode → FSM update → MQTT publish (via queue to Core 0) │
└─────────────────────────────────────────────────────────────┘
```

### 2.4 Luồng Anti-Overrun Ring Buffer

```text
[UART RX ISR (Core 1)]
        │
        │ Mỗi byte nhận được từ HC-12:
        ▼
[ISR Handler: uart_rx_isr()]
        │
        ├── Đọc byte từ UART FIFO
        ├── Tính next_head = (head + 1) % RF_UART_RING_BUFFER_SIZE
        ├── Nếu next_head == tail → BUFFER FULL:
        │     • Increment dropped_bytes counter
        │     • Increment rx_overflows counter
        │     • Bỏ byte này (KHÔNG ghi đè tail)
        │     • Return (không block ISR)
        ├── Nếu không full:
        │     • ring_buffer[head] = byte
        │     • head = next_head  (atomic write)
        │     • xTaskNotifyFromISR() → đánh thức Core 1 consumer task
        └── return
        │
        ▼
[Core 1 Consumer Task: uart_rf_rx_task()]
        │
        ├── xTaskNotifyWait() → block đến khi có byte
        ├── while (head != tail):
        │     • Đọc byte từ ring_buffer[tail]
        │     • tail = (tail + 1) % RF_UART_RING_BUFFER_SIZE
        │     • Feed vào RfRxBuffer accumulator
        │     • Parse AGU frame (Length/Opcode/Params/ZeroSum)
        │     • Update NodeRegistry / FSM
        └── goto xTaskNotifyWait()
```

---

## 3. PHÂN RÃ CHI TIẾU TÁC VỤ

### TRACK A — Codec Refactoring (Zero-Sum & nodeId)

#### Task A-1: `aeroponics-firmware/include/agu_legacy_codec.h` — Cập nhật interface

- **File:** `aeroponics-firmware/include/agu_legacy_codec.h`
- **Hàm bị ảnh hưởng:** `calculateZeroSumChecksum`, `verifyZeroSumChecksum`, `encodeReadRamBurst`

```cpp
// Thay đổi signature encodeReadRamBurst: thêm nodeId làm tham số BẮT BUỘC
static size_t encodeReadRamBurst(uint8_t nodeId, uint16_t addr, uint8_t count,
                                 uint8_t* outBuf, size_t outSize);
// GIỮ overload cũ để backward-compat trong transition period:
static size_t encodeReadRamBurst(uint8_t nodeId, uint16_t addr,
                                 uint8_t* outBuf, size_t outSize);
```

- **Mục tiêu:** Đảm bảo mọi caller đều truyền `nodeId` tường minh; không còn overload ngầm.

#### Task A-2: `aeroponics-firmware/src/agu_legacy_codec.cpp` — Chuẩn hóa zero-sum

- **File:** `aeroponics-firmware/src/agu_legacy_codec.cpp`
- **Hàm bị ảnh hưởng:** `formatSendComPacket`, `calculateZeroSumChecksum` (từ header)

```cpp
// Chuẩn hóa trong formatSendComPacket:
// TRƯỚC: uint8_t checksum = static_cast<uint8_t>((0x100 - sum) & 0xFF);
// SAU:   uint8_t checksum = static_cast<uint8_t>((~sum + 1) & 0xFF);
// Cả hai equivalent nhưng chuẩn hóa theo định nghĩa Two's complement rõ ràng hơn.
```

- **Invariant bắt buộc:** `sum(outBuf[0..frame_len-1]) & 0xFF == 0` cho mọi frame encoder.

#### Task A-3: `aeroponics-firmware/src/agu_legacy_codec.cpp` — Loại bỏ byte hardcode rác

- **File:** `aeroponics-firmware/src/agu_legacy_codec.cpp`
- **Hàm bị ảnh hưởng:** `encodeWriteRam`

```cpp
// TRƯỚC (hardcode rác — 0x00 và 0x01 không có nghĩa rõ ràng):
size_t AguLegacyCodec::encodeWriteRam(uint8_t addr, uint8_t value, ...) {
    const uint8_t payload[5] = {
        static_cast<uint8_t>(Opcode::WRITE_RAM),
        addr,       // offset addr
        0x00,       // ??? hardcode rác
        value,      // giá trị ghi
        0x01        // ??? hardcode rác
    };
    ...
}

// SAU: Xác minh với RF_PROTOCOL.md / interface-wire-contract.md §5.2
// Nếu 0x00 = dummy hi-byte cho 16-bit addr, 0x01 = write-enable flag → giữ với comment rõ.
// Nếu không có justification → thay bằng constant có tên:
constexpr uint8_t WRITE_RAM_DUMMY_HI = 0x00;
constexpr uint8_t WRITE_RAM_ENABLE_FLAG = 0x01;
```

- **Action:** Đọc `docs/interface-wire-contract.md` §5.2 để xác minh ý nghĩa từng byte; nếu là legacy protocol requirement thì đặt tên constant thay vì magic number.

#### Task A-4: `aeroponics-firmware/src/agu_legacy_codec.cpp` — Cập nhật `decodeBurstRam`

- **File:** `aeroponics-firmware/src/agu_legacy_codec.cpp`
- **Hàm bị ảnh hưởng:** `decodeBurstRam`

```cpp
// Đảm bảo decodeBurstRam kiểm tra cả nodeId trong frame nếu protocol yêu cầu.
// Response format: [8-byte RAM block][checksum]
// Kiểm tra invariant: verifyZeroSumChecksum(inBuf, 9, inBuf[8]) == true
// Nếu fail → return false, caller không update telemetry.
```

#### Task A-5: `aeroponics-firmware/include/agu_legacy_rf_host.h` — Thêm READ_RAM_BURST

- **File:** `aeroponics-firmware/include/agu_legacy_rf_host.h`
- **Enum bị ảnh hưởng:** `AguRfCommand`

```cpp
enum class AguRfCommand : uint8_t {
    PING,
    PUMP_ON,
    PUMP_OFF,
    READ_RAM_BURST,   // THÊM MỚI — cho opcode 0x0E polling
};
```

#### Task A-6: `aeroponics-firmware/src/agu_legacy_rf_host.cpp` — Implement `readRamBurst`

- **File:** `aeroponics-firmware/src/agu_legacy_rf_host.cpp`
- **Hàm mới:** `readRamBurst(uint8_t node_id, uint16_t addr, uint8_t* out_data8)`

```cpp
AguRfTransactionResult AguLegacyRfHost::readRamBurst(
    uint8_t node_id, uint16_t addr, uint8_t* out_data8) {
    // 1. Validate node_id via isValidNodeId()
    // 2. encode via AguLegacyCodec::encodeReadRamBurst(node_id, addr, 8, buf, size)
    // 3. Serialize transaction: flush RX → send → wait response (timeout 300ms)
    // 4. Receive 9 bytes (8 data + 1 checksum)
    // 5. AguLegacyCodec::decodeBurstRam(resp, 9, out_data8)
    // 6. Return AguRfTransactionResult with ACKED / TIMEOUT / UNEXPECTED_RESPONSE
}
```

- **Bắt buộc:** Retry đúng `AGU_LEGACY_MAX_ATTEMPTS = 3`, frame giữ nguyên khi retry.

---

### TRACK B — UART HC-12 FreeRTOS Core 1 Isolation

#### Task B-1: `aeroponics-firmware/include/config.h` — Thêm hằng số UART isolation

- **File:** `aeroponics-firmware/include/config.h`
- **Section:** SECTION 2 (Hardware Pinouts) + SECTION 4 (RF Subsystem)

```cpp
// HC-12 module baud rate (separate from RF_UART_DEFAULT_BAUD_RATE which is 38400)
constexpr uint32_t RF_UART_HC12_BAUD_RATE = 9600;

// FreeRTOS Core pinning for UART RX ISR + consumer task
constexpr BaseType_t RF_UART_RX_TASK_CORE = 1;  // Core 1: RF/Application core
constexpr UBaseType_t RF_UART_RX_TASK_PRIORITY = 4;  // Above mqtt_task (3)
constexpr uint32_t RF_UART_RX_TASK_STACK_SIZE = 4096;
constexpr const char* RF_UART_RX_TASK_NAME = "rf_uart_rx_task";

// Bounded ring buffer anti-overrun
constexpr size_t RF_UART_RING_BUFFER_SIZE = 512;  // bytes, power-of-2 preferred
constexpr size_t RF_UART_RX_QUEUE_DEPTH = 64;     // FreeRTOS queue depth for ISR→task
```

- **static_assert bổ sung:**
```cpp
static_assert(RF_UART_RING_BUFFER_SIZE >= 256,
              "RF UART ring buffer must hold at least one full AGU burst response");
static_assert(RF_UART_RX_TASK_PRIORITY > MQTT_TASK_PRIORITY,
              "UART RX task must have higher priority than MQTT task to prevent overrun");
```

#### Task B-2: `aeroponics-firmware/include/uart_rf_transport.h` — Thêm ring buffer & task interface

- **File:** `aeroponics-firmware/include/uart_rf_transport.h`
- **Class:** `UartRfTransport`

```cpp
class UartRfTransport : public IRfTransport {
public:
    // ... existing interface ...

    // THÊM MỚI: FreeRTOS Core 1 UART RX isolation
    bool startRxTask();                          // Tạo consumer task trên Core 1
    void stopRxTask();                           // Dừng task khi shutdown
    size_t getDroppedBytes() const;              // Đọc counter overrun
    size_t getRxOverflows() const;               // Đọc counter overflow

private:
    // THÊM MỚI: Ring buffer state
    volatile uint8_t* _ring_buffer = nullptr;
    volatile size_t _ring_head = 0;
    volatile size_t _ring_tail = 0;
    size_t _ring_size = 0;

    // THÊM MỚI: FreeRTOS handles
    TaskHandle_t _rx_task_handle = nullptr;
    QueueHandle_t _rx_notify_queue = nullptr;

    // THÊM MỚI: Statistics
    volatile uint32_t _dropped_bytes = 0;
    volatile uint32_t _rx_overflows = 0;

    // THÊM MỚI: Static task function
    static void rxTaskFunction(void* param);
    void rxTaskLoop();

    // THÊM MỚI: ISR handler (static, registered via uart_isr_register)
    static void uartRxIsr(void* arg);
};
```

#### Task B-3: `aeroponics-firmware/src/uart_rf_transport.cpp` — Implement ring buffer + Core 1 task

- **File:** `aeroponics-firmware/src/uart_rf_transport.cpp`
- **Hàm bị ảnh hưởng:** `begin()`, `receive()`, `available()`
- **Hàm mới:** `startRxTask()`, `stopRxTask()`, `rxTaskFunction()`, `rxTaskLoop()`, `uartRxIsr()`

```cpp
bool UartRfTransport::begin() {
    // ... existing UART init code ...
    // THÊM: Allocate ring buffer (bounded, không heap trong loop)
    _ring_size = RF_UART_RING_BUFFER_SIZE;
    _ring_buffer = new volatile uint8_t[_ring_size];  // PHẢI check nullptr
    if (!_ring_buffer) return false;
    _ring_head = 0; _ring_tail = 0;

    // THÊM: Create FreeRTOS queue for ISR→task notification
    _rx_notify_queue = xQueueCreate(1, sizeof(uint32_t));

    // THÊM: Register UART RX ISR (runs on Core 1 via interrupt affinity)
    // uart_isr_register(_uart_num, uartRxIsr, this, 0, nullptr);

    // THÊM: Start consumer task pinned to Core 1
    return startRxTask();
}

bool UartRfTransport::startRxTask() {
    return xTaskCreatePinnedToCore(
        rxTaskFunction, RF_UART_RX_TASK_NAME,
        RF_UART_RX_TASK_STACK_SIZE, this,
        RF_UART_RX_TASK_PRIORITY, &_rx_task_handle,
        RF_UART_RX_TASK_CORE) == pdPASS;
}

void UartRfTransport::uartRxIsr(void* arg) {
    UartRfTransport* self = static_cast<UartRfTransport*>(arg);
    uint32_t uart_num = self->_uart_num;
    uint8_t byte;
    while (uart_read_byte_from_fifo(uart_num, &byte)) {
        size_t next_head = (self->_ring_head + 1) % self->_ring_size;
        if (next_head == self->_ring_tail) {
            self->_dropped_bytes++;      // BUFFER FULL — drop byte
            self->_rx_overflows++;
            return;                       // KHÔNG block ISR
        }
        self->_ring_buffer[self->_ring_head] = byte;
        self->_ring_head = next_head;
    }
    BaseType_t hp_woken = pdFALSE;
    xQueueSendFromISR(self->_rx_notify_queue, &(uint32_t){1}, &hp_woken);
    portYIELD_FROM_ISR(hp_woken);
}

void UartRfTransport::rxTaskLoop() {
    uint32_t notify;
    for (;;) {
        if (xQueueReceive(_rx_notify_queue, &notify, portMAX_DELAY) == pdTRUE) {
            while (_ring_head != _ring_tail) {
                uint8_t byte = _ring_buffer[_ring_tail];
                _ring_tail = (_ring_tail + 1) % _ring_size;
                // Feed byte vào frame accumulator (RfRxBuffer)
                // Parse AGU frame nếu đủ length
                // Update NodeRegistry / callback
            }
        }
    }
}
```

- **Lưu ý:** `receive()` hiện tại đọc trực tiếp từ `HardwareSerial`. Sau khi ring buffer active, `receive()` phải đọc từ ring buffer thay vì `_rf_serial.available()/read()`. Giữ đường code cũ trong `#if !defined(RF_UART_RING_BUFFER_ACTIVE)` để backward-compat.

#### Task B-4: `aeroponics-firmware/src/main.cpp` — Cập nhật khởi tạo UART với Core 1 pinning

- **File:** `aeroponics-firmware/src/main.cpp`
- **Hàm bị ảnh hưởng:** `initializeRfTransport()`

```cpp
static bool initializeRfTransport(const RfHardwareConfig& config) {
    static UartRfTransport uart(config.uart_num, config.rx_pin, config.tx_pin,
                                RF_UART_HC12_BAUD_RATE,  // THAY vì config.baud_rate
                                UART_RF_DEFAULT_RX_BUFFER_CAPACITY,
                                config.m0_pin, config.m1_pin, config.aux_pin);
    if (!uart.begin()) return false;
    if (!uart.startRxTask()) {           // THÊM: Start Core 1 RX task
        ESP_LOGE(TAG, "Failed to start RF UART RX task on Core 1");
        return false;
    }
    g_rf_transport = &uart;
    // ... rest unchanged ...
}
```

---

### TRACK C — Cập nhật Caller & Test

#### Task C-1: `aeroponics-firmware/src/main.cpp` — Đồng bộ gọi `encodeReadRamBurst`

- **File:** `aeroponics-firmware/src/main.cpp`
- **Hàm bị ảnh hưởng:** `executeAguPump()`, `executeAguPing()`, bất kỳ hàm nào gọi `encodeReadRamBurst`

```cpp
// TRƯỚC (implicit count):
// AguLegacyCodec::encodeReadRamBurst(node_id, addr, buf, size);
// SAU (explicit count = 8, nodeId tường minh):
AguLegacyCodec::encodeReadRamBurst(node_id, addr, 8, buf, size);
```

#### Task C-2: `aeroponics-firmware/test/test_codec/test_codec.cpp` — Cập nhật unit test

- **File:** `aeroponics-firmware/test/test_codec/test_codec.cpp`
- **Test case mới:**

```cpp
TEST_CASE("encodeReadRamBurst with nodeId produces correct frame") {
    uint8_t buf[16];
    size_t len = AguLegacyCodec::encodeReadRamBurst(4, 0x0100, 8, buf, sizeof(buf));
    REQUIRE(len == 7);  // Length + Opcode + AddrLo + AddrHi + Count + NodeId + Checksum
    REQUIRE(buf[0] == 0x06);  // Length = payloadLen + 1 = 5 + 1
    REQUIRE(buf[1] == 0x0E);  // Opcode READ_RAM_BURST
    REQUIRE(buf[2] == 0x00);  // addr_lo (little-endian)
    REQUIRE(buf[3] == 0x01);  // addr_hi
    REQUIRE(buf[4] == 0x08);  // count = 8
    REQUIRE(buf[5] == 0x04);  // nodeId = 4
    // Zero-sum invariant
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) sum += buf[i];
    REQUIRE(sum == 0);
}

TEST_CASE("encodeReadRamBurst rejects count != 8") {
    uint8_t buf[16];
    REQUIRE(AguLegacyCodec::encodeReadRamBurst(4, 0x0100, 4, buf, sizeof(buf)) == 0);
}
```

#### Task C-3: `aeroponics-firmware/test/test_transport/test_transport.cpp` — Test anti-overrun

- **File:** `aeroponics-firmware/test/test_transport/test_transport.cpp`
- **Test case mới:**

```cpp
TEST_CASE("Ring buffer drops bytes when full without corrupting tail") {
    // Fill ring buffer to capacity - 1
    // Attempt to write one more byte
    // Assert: dropped_bytes incremented, tail unchanged, head wrapped to tail-1
}

TEST_CASE("ISR notification wakes consumer task") {
    // Inject bytes via injectRxBytes (host test mode)
    // Assert: consumer task processes all bytes in order
}
```

---

## 4. TIÊU CHUẨN RÀ SOÁT CỨNG (Sprint 1)

### Rule S1-CODEC-01: Zero-Sum Invariant cho mọi encoder
```
PASS: sum(frame[0..len-1]) & 0xFF == 0 cho MỌI frame do encoder tạo ra
FAIL: Bất kỳ encoder nào trả frame có sum != 0 mod 256
FAIL: Checksum tính bằng phép toán khác Two's complement chuẩn
```

### Rule S1-CODEC-02: nodeId bắt buộc trong encodeReadRamBurst
```
PASS: encodeReadRamBurst(nodeId, addr, count, buf, size) — nodeId là tham số thứ nhất
FAIL: Gọi overload không có nodeId trong production code path
FAIL: nodeId hardcode = 0x01 trong hàm encode (phải là node_id thực tế từ caller)
```

### Rule S1-UART-03: Core 1 Isolation cho UART HC-12
```
PASS: UART RX ISR và consumer task đều pinned to Core 1
PASS: Wi-Fi/MQTT tasks không pinned to Core 1
FAIL: UART RX chạy trên Core 0 cùng Wi-Fi driver
FAIL: ISR handler chứa blocking call (delay, malloc, printf)
```

### Rule S1-UART-04: Bounded Ring Buffer — Không Heap trong Loop
```
PASS: Ring buffer allocate 1 lần trong begin(), không allocate trong ISR/task loop
PASS: Ring buffer size là compile-time constant (RF_UART_RING_BUFFER_SIZE)
PASS: Khi buffer full → drop byte + increment counter, KHÔNG block ISR
FAIL: malloc/new trong ISR hoặc trong rxTaskLoop()
FAIL: Ring buffer size = 0 hoặc không bounded
```

### Rule S1-UART-05: Anti-Overrun khi Polling 1s
```
PASS: 4 node polling 0x0E ở nhịp 1s → rx_overflows == 0 trong 60s soak test
PASS: dropped_bytes counter increment đúng khi故意 inject overrun
FAIL: RX task priority < MQTT task priority (gây priority inversion)
```

### Rule S1-TEST-06: 273/273 Native Unit Tests PASS
```
PASS: pio test → 273/273 PASS, 0 FAIL, 0 SKIP
FAIL: Bất kỳ test nào fail sau khi refactor
FAIL: Thêm test mới nhưng không update test count trong PROGRESS.md
```

---

*Sprint 1 Planning — Gateway Transport & Codec Refactoring. Golden Baseline: interface-wire-contract.md Rev 3.*
