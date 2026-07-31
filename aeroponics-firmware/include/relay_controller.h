#pragma once

#include <atomic>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

/**
 * @brief Represents the physical state of a relay output channel.
 */
enum RelayState {
    RELAY_OFF = 0,
    RELAY_ON = 1
};

/**
 * @brief Struct capturing the current manual override state for a relay.
 */
struct RelayOverrideState {
    bool active;
    uint32_t remaining_s;
    RelayState forced_state;
    TickType_t expires_at;
};

/**
 * @brief Hardware Abstraction Layer (HAL) controller for aeroponics relay channels.
 *
 * Encapsulates direct GPIO manipulation and internal state caching for 4 relays.
 * RelayController is the single source of truth for physical relay operations.
 * Thread-safe across FreeRTOS tasks via internal mutex and atomic safe-state latches.
 */
class RelayController {
public:
    RelayController();
    ~RelayController();

    /**
     * @brief Initialize relay GPIO pins with hardware fail-safe sequence.
     * MUST execute digitalWrite(pin, LOW) before pinMode(pin, OUTPUT) for each relay
     * to prevent electrical power-on glitching.
     */
    void initPins();

    /**
     * @brief Set physical state of specified relay channel and update internal cache.
     * Thread-safe.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     * @param state Target state (RELAY_OFF or RELAY_ON).
     * @return true if parameter valid and state applied, false otherwise.
     */
    bool setRelay(uint8_t relay_id, RelayState state);

    /**
     * @brief Retrieve cached physical state of specified relay channel.
     * Thread-safe.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     * @return RELAY_ON or RELAY_OFF (defaults to RELAY_OFF if relay_id invalid).
     */
    RelayState getRelayState(uint8_t relay_id) const;

    /**
     * @brief Activate manual override for a specific relay for a duration in seconds.
     * Validates duration_s in [MIN_OVERRIDE_DURATION_S, MAX_OVERRIDE_DURATION_S].
     * Thread-safe.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     * @param forced_state State to force (RELAY_ON or RELAY_OFF).
     * @param duration_s Duration of override in seconds.
     * @return true if override started successfully, false if parameters invalid or fault latched.
     */
    bool startManualOverride(uint8_t relay_id, RelayState forced_state, uint32_t duration_s);

    /**
     * @brief Immediately cancel active manual override for specified relay.
     * Thread-safe.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     * @return true if cancelled or was inactive, false if relay_id invalid or mutex timeout.
     */
    bool cancelOverride(uint8_t relay_id);

    /**
     * @brief Check whether manual override is currently active for specified relay.
     * Thread-safe.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     * @return true if override active, false otherwise.
     */
    bool isOverrideActive(uint8_t relay_id) const;

    /**
     * @brief Decrement manual override timer for specified relay by 1 second.
     * Automatically deactivates override when remaining duration reaches 0.
     * Thread-safe.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     */
    void tickOverride(uint8_t relay_id);

    /**
     * @brief Atomically process override timer tick and apply correct relay state.
     * If override is active and expires on this tick, immediately transitions relay
     * to scheduled_state (RELAY_ON or RELAY_OFF).
     * Thread-safe.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     * @param scheduled_state Target state prescribed by active schedule phase.
     * @return true if state applied successfully, false on mutex timeout or invalid parameter.
     */
    bool applyScheduledStateUnlessOverride(uint8_t relay_id, RelayState scheduled_state);

    /**
     * @brief Emergency fail-safe method to force relay pin LOW directly and latch safe-state.
     * Sets atomic fault latch, drives GPIO LOW, and synchronizes cache state.
     * Blocks all subsequent write HIGH commands until reset.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     * @return true if pin valid and driven LOW, false otherwise.
     */
    bool forceRelayOffEmergency(uint8_t relay_id);

    /**
     * @brief Query whether target relay channel is latched in fault safe-state.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     * @return true if fault latched, false otherwise.
     */
    bool isFaultLatched(uint8_t relay_id) const;

#ifdef ENABLE_FAULT_INJECTION_TEST
    /**
     * @brief Reset fault safe-state latch for target relay channel (TEST ONLY).
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     */
    void resetFaultLatch(uint8_t relay_id);

    /**
     * @brief Fault-injection verification test for 2-barrier concurrency & safe-state latch.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     * @return true if test passes (safe-state held), false otherwise.
     */
    bool testFaultInjectionEmergency(uint8_t relay_id);

private:
    bool createFaultTestResources(struct FaultTestParam &param);
    void cleanupFaultTestResources(struct FaultTestParam &param);
    bool startFaultWriterTask(struct FaultTestParam &param, TaskHandle_t &writer_handle);
    bool waitForFaultWriterCompletion(struct FaultTestParam &param, TaskHandle_t writer_handle);
    bool verifyFaultSafeState(uint8_t relay_id, const struct FaultTestParam &param);
#endif

public:
    /**
     * @brief Get full snapshot of manual override state for specified relay.
     * Thread-safe.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     * @return RelayOverrideState struct.
     */
    RelayOverrideState getOverrideState(uint8_t relay_id) const;

private:
    RelayState state_cache_[TOTAL_RELAYS];
    RelayOverrideState override_state_[TOTAL_RELAYS];
    std::atomic<bool> fault_latched_[TOTAL_RELAYS];
    mutable SemaphoreHandle_t mutex_;
    mutable portMUX_TYPE spinlock_;

    /**
     * @brief Helper mapping zero-based relay_id to physical GPIO pin number.
     * @param relay_id Zero-based relay index [0..TOTAL_RELAYS-1].
     * @return GPIO pin number, or 255 if invalid.
     */
    uint8_t getPinForRelay(uint8_t relay_id) const;

    /**
     * @brief Internal helpers setting relay state without acquiring mutex (caller must hold mutex_).
     */
    bool validateRelayPin(uint8_t relay_id, uint8_t &out_pin) const;
    void applySafeLatchedStateLocked(uint8_t relay_id, uint8_t pin);
    void applyRelayOutputLocked(uint8_t relay_id, uint8_t pin, RelayState state);
    bool setRelayLocked(uint8_t relay_id, RelayState state);
};

