# Plan: A1 — Add Mandatory `nodeId` Parameter to `encodeReadRamBurst`

## 1. Overview
- **Goal**: Ensure every call to `encodeReadRamBurst` carries an explicit `nodeId` parameter, per S1-CODEC-02 and the Golden Baseline (`docs/interface-wire-contract.md` §3–§5 Rev 3). The new full signature places `nodeId` as the first argument. An overload without `count` is retained temporarily for transition (Strangler Fig) and will be removed after all callers are updated.
- **Scope**: Track A — Codec Refactoring (Zero-Sum & nodeId) of Sprint 1 Refactor Phase.
- **Acceptance**: All 273 native unit tests must still PASS; the new overload signature must compile without warnings; JSDoc/Doxygen comments must be present on every public codec API.

## 2. Baseline Findings (from code inspection)
- `include/agu_legacy_codec.h` currently already declares two `encodeReadRamBurst` overloads (lines 73–76): one with `(nodeId, addr, count, outBuf, outSize)` and one with `(nodeId, addr, outBuf, outSize)`. Both placements match the required param order (nodeId first); no hard-coded `0x01` exists inside the encoder (nodeId comes from caller).
- `src/agu_legacy_codec.cpp` implements both overloads: the 3-arg-addr form delegates to the full form with `BURST_DATA_SIZE` (line 79–82), which already satisfies the requirement.
- `calculateZeroSumChecksum` (header inline, line 43) and `verifyZeroSumChecksum` (line 55) are already two's-complement zero-sum compliant; they only lack explicit Doxygen docs.
- `decodeBurstRam` (line 120) already verifies zero-sum before returning data (fail-closed). This is A4 scope; no change needed for A1.
- Existing callers already pass `nodeId` explicitly (e.g., `test_production.cpp` summons `encodeReadRamBurst(4, 0x0008, 8, ...)`).

## 3. Work Items
### 3.1 Update `aeroponics-firmware/include/agu_legacy_codec.h`
1. Add full Doxygen/JSDoc comment blocks to `calculateZeroSumChecksum`, `verifyZeroSumChecksum`, and both `encodeReadRamBurst` overloads, documenting:
   - Params: `nodeId` (explicit, never hard-coded `0x01` in production), `addr` (little-endian RAM address), `count` (must be `BURST_DATA_SIZE`), `outBuf`/`outSize`.
   - Return: encoded length, or `0` on invalid args (S1-CODEC-01 invariant: `sum(frame)&0xFF == 0`).
   - Note the no-`count` overload is a transition shim (Strangler Fig), removed after callers migrate.
2. Keep both overload signatures unchanged (they already match S1-CODEC-02).
3. No heap allocations in header/implementation paths (already satisfied).

### 3.2 Verify (no code change expected) `src/agu_legacy_codec.cpp`
- Confirm the no-`count` overload forwards to `encodeReadRamBurst(nodeId, addr, BURST_DATA_SIZE, outBuf, outSize)` with correct buffer sizing.
- Confirm `formatSendComPacket` returns 0 on failure and checksum is two's-complement (already correct at line 24).

### 3.3 Update `test/test_codec/test_codec.cpp`
- Ensure the test vector `encodeReadRamBurst(4, 0x0100, 8, buf, sizeof(buf))` yields `len == 7`, `sum==0`, and rejects `count != 8` (== 0). These tests are already present in `test_production.cpp` but must also exist in `test_codec/test_codec.cpp`.
- Add a regression test that the no-`count` overload still returns `len == 7` with `BURST_DATA_SIZE`.
- Run the full native suite to confirm 273/273 PASS.

## 4. Verification & Validation Steps
1. `pio run -e esp32-s3-devkitc-1` from `aeroponics-firmware/` — should succeed with no warnings.
2. `pio test -e native` — expect 273/273 PASS (0 FAIL, 0 SKIP).
3. Static check: `rg "encodeReadRamBurst\\(" aeroponics-firmware/` — no production call passing literal `0x01` as nodeId.
4. Invariant check on any frame built by `encodeReadRamBurst` for a sample `(nodeId, addr, 8)`: sum & 0xFF == 0.
5. `git diff --check`.

## 5. Rollback / Migration
- The no-`count` overload is intentionally kept for a transition window (Strangler Fig) per sprint_1.md §1.2 and Task A-1 note. Remove it in a later pass once `main.cpp` / `agu_legacy_rf_host.cpp` callers are migrated (Track C). This is out of A1 scope; a current grep shows no caller depends on the no-`count` variant, so removal can happen in a follow-up without runtime impact.

## 6. Out of Scope / Follow-ups
- Tasks A2–A6 (zero-sum internals, `encodeWriteRam` magic bytes, `decodeBurstRam`, `READ_RAM_BURST` enum, `readRamBurst` transaction) are separate rows to handle in later turns.
- Track B (UART HC-12 Core 1 isolation) and Track C (caller & test sync) are dependent later tasks.
