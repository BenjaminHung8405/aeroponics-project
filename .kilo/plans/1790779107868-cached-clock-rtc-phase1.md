# Phase 1 Implementation Plan: Cached Clock for `RtcManager`

## Scope and current-state findings

- Change only `aeroponics-firmware/include/rtc_manager.h`, `aeroponics-firmware/src/rtc_manager.cpp`, and the RTC unit-test files under `aeroponics-firmware/test/`.
- Do not change `GroupScheduler`, `main.cpp`, `config.h`, PlatformIO environments, or any other production module. Existing callers continue to use `begin()`, `getTime()`, `syncFromNtp()`, `adjustTimeFromUnix()`, `applyUtcClockFromBackend()`, and the existing telemetry/power-loss APIs.
- The current `getTime()` already prefers `time(nullptr)` when the system clock is valid, but it falls back to `rtc_.now()` when it is not. `begin()` currently checks only `begin()`/`isrunning()` and does not seed POSIX time from the RTC. `readHardwareUtcEpoch()` is therefore the I2C path that must be removed from runtime reads.
- No `getUtcEpochSeconds()` exists in the current tree. Add it as a non-breaking public convenience API if the Phase 1 caller/test contract requires it; otherwise keep UTC conversion private and explicitly verify that no existing API is renamed or removed.

## Target design

1. **Cached/system clock source**
   - Keep POSIX system time (`settimeofday`, `gettimeofday`/`time`) as the runtime clock. `getTime()` and the new `getUtcEpochSeconds()` read only POSIX time and convert it to local `SystemTime`; they never call `RTC_DS1307::now()`.
   - Preserve the existing trust metadata and fallback semantics: a valid DS1307 seed makes the clock trusted; NTP/backend updates remain authoritative and continue to update POSIX time and the hardware RTC when available; an invalid/unavailable source returns invalid time rather than inventing a value.
   - `begin()` performs exactly one hardware calendar read after confirming the RTC is present and running. Validate the epoch, call `settimeofday()` before returning, and record the cached/trust source. If the read or POSIX set fails, leave the system clock fallback available and report the existing `begin()` result semantics without introducing a per-call hardware read.

2. **Six-hour drift-compensation task**
   - Add private task state: a FreeRTOS task handle, a hardware-access mutex (or equivalent lock), a task-started flag, and constants for 6-hour period, low priority (`1`), Core 0, and a modest stack size. Keep these declarations conditional so native tests do not require FreeRTOS/Arduino headers.
   - Start the task only after successful RTC initialization/seed setup. Use `xTaskCreatePinnedToCore`; the task sleeps with `vTaskDelay(pdMS_TO_TICKS(6 * 60 * 60 * 1000))`, then performs one guarded `rtc_.now()` read. It must not poll once per second and must not run on the scheduler/FSM task.
   - On a valid periodic reading, compare it with the current POSIX epoch and correct POSIX using `settimeofday()` according to the existing trust policy. Do not let a stale RTC overwrite a newer backend/NTP reference: retain the timestamp/source metadata needed to suppress RTC correction while a recent authoritative backend/NTP value is active, or define the correction rule explicitly in the implementation as “RTC corrects only when no newer network/backend reference is available.”
   - On I2C/read/validation failure, log and retain the current POSIX clock; never invalidate or reset a functioning cached clock. The task continues retrying at the next six-hour period.
   - Destructor/lifecycle handling must stop/delete the task and release the mutex safely. Since the production object is long-lived, avoid any blocking destructor path that can deadlock with an in-progress I2C transaction.

3. **Synchronization and public compatibility**
   - Guard every hardware operation (`begin`, periodic `now`, `adjust`, `isrunning`, and hardware writes from NTP/backend/manual adjustment) with the same mutex. POSIX reads do not need that mutex.
   - Keep all existing public signatures and return meanings. `syncFromNtp()` still uses network time, updates POSIX through the existing path, and writes DS1307 when present; offline operation simply continues from the POSIX clock seeded by DS1307.
   - Preserve `hasPowerLoss()`, telemetry fields, backend-authority suppression, timezone conversion, and invalid-time fail-safe behavior. Update comments that currently describe direct DS1307 reads or imply that every read can recover from an unseeded POSIX clock.

## File-by-file changes

### `aeroponics-firmware/include/rtc_manager.h`

- Add the guarded FreeRTOS/task and synchronization declarations needed on ESP32, without exposing implementation details to current callers.
- Add named cadence/priority/core constants (prefer local `constexpr` values in the class or source rather than modifying global configuration files).
- Add private helpers with narrow responsibilities, such as `seedPosixClockFromHardware()`, `readHardwareUtcEpochLocked()`, `startDriftCompensationTask()`, `stopDriftCompensationTask()`, and the static task trampoline/loop.
- Add a monotonic/reference timestamp or source metadata needed to prevent a six-hour RTC sample from replacing a newer NTP/backend correction.
- If required by the actual interface contract, add `int64_t getUtcEpochSeconds() const` (or the project’s established equivalent) while retaining every current method unchanged. Document that it is POSIX-only and never performs I2C.
- Add a native-test seam without changing production call sites: use a small injectable RTC adapter/function table or a `UNIT_TEST_HOST` fake backend that can script `begin`, `isrunning`, `now`, and `adjust`, plus count `now()` calls. Keep the default production constructor unchanged; any test-only constructor/setter must be compile-time guarded.

### `aeroponics-firmware/src/rtc_manager.cpp`

- Initialize task/mutex/cache state in the constructor and clean it up in the destructor.
- Refactor hardware access into locked helpers. Ensure the only normal-runtime call sites of `rtc_.now()` are the one-time `begin()` seed and the six-hour drift task (plus explicit test/control paths if applicable).
- Implement `begin()` sequence: initialize/detect RTC, check oscillator, perform exactly one validated `now()`, set POSIX UTC, update trust/source metadata, then start the low-priority Core-0 task. Define deterministic behavior for each failure stage and preserve the existing boolean contract expected by `main.cpp`.
- Rewrite `getTime()` to resolve trust and read `time(nullptr)`/`gettimeofday` only; remove the fallback branch that reads DS1307 and sets POSIX from inside a caller’s time request.
- Implement the periodic task with `vTaskDelay`, lock acquisition with a bounded timeout, one RTC read per period, validation, correction, logging, and retry behavior. Never hold the RTC mutex while sleeping or while doing unrelated POSIX/logging work.
- Route all hardware writes through the lock and keep POSIX updated first for backend/NTP paths as currently required. Ensure manual `adjustTime(const DateTime&)` also updates POSIX consistently if that API is expected to establish the system clock.
- Keep native compilation free of ESP/FreeRTOS symbols by compiling the task implementation out and using the injectable fake clock backend for deterministic tests.

## Verification plan

### Native RTC mocking

- Extend `aeroponics-firmware/test/test_rtc_manager/test_rtc_manager.cpp` with a scripted fake RTC adapter enabled by `UNIT_TEST_HOST`.
- Script a valid DS1307 epoch and assert `begin()` seeds the host POSIX clock; then call `getTime()`/`getUtcEpochSeconds()` repeatedly and assert the fake `now()` count remains exactly one (the startup read), proving cached reads do not touch I2C.
- Script invalid/stopped RTC data and assert the system clock remains unchanged/invalid as appropriate, with no repeated hardware reads from `getTime()`.
- Script backend/NTP-style POSIX updates and verify subsequent cached reads reflect the POSIX value while hardware writes/counts follow the existing API contract.
- Exercise concurrent or interleaved adjust/read calls at the adapter seam to verify the mutex/serialization contract where host support permits; the six-hour task itself is validated by testing its period/callback helper rather than waiting six real hours.
- Add a focused test-only cadence hook or injectable delay/clock so one simulated six-hour tick can be triggered and asserted to perform one RTC read and one correction. Do not weaken production constants merely to make the test pass.

### Commands

1. `cd aeroponics-firmware && pio test -e native -f test_rtc_manager`
2. `cd aeroponics-firmware && pio test -e native -f test_production`
3. `cd aeroponics-firmware && pio test -e native`
4. Build the target hardware environment to catch ESP32/FreeRTOS integration errors: `cd aeroponics-firmware && pio run -e esp32-s3-devkitc-1`.

### Acceptance criteria

- `begin()` performs one and only one initial DS1307 time read, validates it, and seeds POSIX time with `settimeofday()` when valid.
- `getTime()` and `getUtcEpochSeconds()` perform zero I2C operations during any number of runtime calls; the GroupScheduler remains untouched.
- A dedicated low-priority task is pinned to Core 0 and performs at most one DS1307 read per six-hour period; I2C failures do not block or invalidate the cached POSIX clock.
- NTP/backend synchronization still updates POSIX and hardware according to the existing behavior; loss of network leaves the seeded/internal clock running.
- Existing public `RtcManager` signatures and telemetry/trust semantics remain source-compatible, and no files outside the strict scope are modified.
- Focused RTC tests, `test_production`, the complete native suite, and the ESP32 build pass.
- The final diff contains no `rtc_.now()` in `getTime()` or any one-second/runtime clock-read path, and test instrumentation demonstrates the call-count guarantee.

## Risks and explicit out-of-scope items

- A six-hour RTC correction can step wall time if the DS1307 is badly stale; the implementation must log the delta and obey the newer NTP/backend-authority guard. Slewing rather than stepping, changing scheduler behavior, and changing NTP cadence are out of scope for Phase 1.
- FreeRTOS task creation failure is handled as a degraded mode: keep the valid POSIX seed and report/log the failure; do not add a fallback task in `GroupScheduler`.
- No changes to GroupScheduler, `main.cpp`’s periodic NTP service, global configuration, I2C wiring, or production dependency declarations are part of this phase.
