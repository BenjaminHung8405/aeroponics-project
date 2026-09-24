# WALKTHROUGH_LOG — Refactor Phase Tracking

> Nhật ký thực thi theo thứ tự thời gian đảo ngược (mới nhất lên đầu). Mỗi Agent ghi lại tác vụ đã làm, files tác động, trạng thái và kết quả kiểm tra nội bộ.

---

## 2026-09-24T05:45:38Z — Track A Codec Refactor (A3-A6)

**Agent:** Execution Agent (Kilo)  
**Kế hoạch:** `.ai/planning/refactor-phase/`  
**Task IDs:** A3, A4, A5, A6

**Trạng thái hiện tại:** Đang chờ QA Review.

**Files đã tạo mới hoặc sửa đổi:**
- `aeroponics-firmware/src/agu_legacy_codec.cpp` — sửa
- `aeroponics-firmware/include/agu_legacy_codec.h` — sửa
- `aeroponics-firmware/include/agu_legacy_rf_host.h` — sửa
- `aeroponics-firmware/src/agu_legacy_rf_host.cpp` — sửa
- `.ai/planning/refactor-phase/PROGRESS.md` — cập nhật trạng thái Task

**Giải trình giải pháp logic:**
- **A3:** Thay thế magic number `0x00` và `0x01` trong `encodeWriteRam` bằng named constants `WRITE_RAM_DUMMY_HI = 0x00` và `WRITE_RAM_ENABLE_FLAG = 0x01`, kèm comment giải thích là legacy protocol-fixed fields. Giữ nguyên byte values → không đổi wire contract.
- **A4:** Xác nhận `decodeBurstRam` đã fail-closed (kiểm tra `verifyZeroSumChecksum` trước khi `memcpy`, trả `false` khi checksum sai). Bổ sung Doxygen comment mô tả rõ frame layout [8 data + 1 checksum] và fail-closed semantics.
- **A5:** Thêm `READ_RAM_BURST` vào `AguRfCommand` enum theo quy ước `SCREAMING_SNAKE_CASE`.
- **A6:** Implement `readRamBurst()` tuần tự: validate `isValidNodeId()` → encode `READ_RAM_BURST` với count=8 → flush RX → send → collect 9-byte response trong timeout 300ms → `decodeBurstRam()` → trả `AguRfTransactionResult`. Retry đúng `AGU_LEGACY_MAX_ATTEMPTS = 3`, giữ nguyên frame, KHÔNG retry vô hạn. Struct result có trường `result`.

**Kết quả tự kiểm tra mã nguồn:**
- Build native test environment sạch (chỉ warning switch case `READ_RAM_BURST` cần xử lý khi bổ sung codec `encode()` — không ảnh hưởng runtime).
- Chạy `pio test -e native -f test_production`:
  - Baseline (commit 2957893): 198 test cases — 97 failed, 100 succeeded, SIGSEGV.
  - Với thay đổi A3-A6: 198 test cases — 97 failed, 100 succeeded, SIGSEGV.
  - Kết luận: không có regression mới; 97 failures là pre-existing baseline không liên quan đến codec refactor.
- AGU legacy codec tests (`test_agu_legacy_codec_encodes_commands_matching_delphi_spec`, `test_agu_legacy_codec_checksum_and_decoders`) nằm trong nhóm 100 tests thành công và không bị ảnh hưởng.
- Zero-sum invariant `sum(frame) & 0xFF == 0` được kiểm tra qua `verifyZeroSumChecksum` trên các encoder/decoder.
