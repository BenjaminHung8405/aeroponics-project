### [2026-09-24 05:23 UTC] - Task A1: Cập nhật interface agu_legacy_codec.h — nodeId bắt buộc + Doxygen

* **Trạng thái:** `[ ] QA Review` (Chờ Auditor kiểm tra)
* **Files tác động:**
  - `[MODIFIED]` `aeroponics-firmware/include/agu_legacy_codec.h` — Thêm/tu chỉnh Doxygen/JSDoc trên 4 API public: `calculateZeroSumChecksum`, `verifyZeroSumChecksum`, và 2 overload `encodeReadRamBurst`.
* **Giải pháp kỹ thuật:**
  1. **`calculateZeroSumChecksum`** — Mở rộng Doxygen: ghi rõ S1-CODEC-01 invariant `(sum + checksum) & 0xFF == 0`, mô tả param/return an toàn (null-safe → return 0).
  2. **`verifyZeroSumChecksum`** — Mở rộng Doxygen: ghi rõ invariant tương tự, mô tả false khi null data.
  3. **`encodeReadRamBurst(nodeId, addr, count, outBuf, outSize)`** — Thêm Doxygen chi tiết: mô tả exact 7-byte frame `[0x06, 0x0E, addr_lo, addr_hi, count, nodeId, checksum]`; little-endian address (khác big-endian EEPROM); nodeId phải do caller truyền runtime, KHÔNG hardcode `0x01` (S1-CODEC-02); count = BURST_DATA_SIZE bắt buộc; return 0 khi invalid.
  4. **`encodeReadRamBurst(nodeId, addr, outBuf, outSize)`** — Thêm Doxygen: đánh dấu là Strangler Fig transition shim, giữ tạm để backward-compat trước khi remove ở Track C.
* **Kiểm tra static:**
  - `rg "encodeReadRamBurst\\("` aeroponics-firmware/src/ → chỉ definition, KHÔNG có caller nào dùng `0x01` literals.
  - `rg "encodeReadRamBurst\\("` aeroponics-firmware/test/ → test vectors dùng nodeId `4`, `7`, `0` (reject case) — đúng contract.
* **Kết quả tự kiểm thử:**
  - `pio test -e native` — 198 tests: **100 PASS / 97 FAIL / 1 ERRORED (SIGSEGV)** — **PRE-EXISTING**; chạy baseline `git stash` cho kết quả tương đương chính xác, xác nhận thay đổi Doxygen không gây regress.
  - Changeset: `git diff --stat` = `aeroponics-firmware/include/agu_legacy_codec.h | 53 insertions(+) 4 deletions(-)` — comment-only, không có thay đổi runtime.
* **Ghi chú:** Plan ghi kỳ vọng 273/273 PASS nhưng thực tế suite có 198 test cases trên `main` hiện tại; SIGSEGV xảy ra ở `test_c4_configurable_per_node_and_treatment_provenance_isolation` — đây là tech debt hiện có, không thuộc phạm vi A1. Task A1 hoàn tất phần header documentation; runtime logic cpp đã đúng và không cần thay đổi.
