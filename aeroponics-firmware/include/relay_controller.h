#pragma once

#include <atomic>
#include <cstdint>
#include "config.h"
#include "core/IRelayOutput.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

/**
 * @brief ESP32 Hardware Abstraction Layer (HAL) controller for aeroponics relay channels.
 * Encapsulates direct GPIO manipulation and internal state caching for 4 relays.
 * Implements IRelayOutput core interface.
 */
class RelayController : public IRelayOutput {
public:
    RelayController();
    ~RelayController() override;

    /**
     * @brief Initialize relay GPIO pins with hardware fail-safe sequence.
     * MUST execute digitalWrite(pin, LOW) before pinMode(pin, OUTPUT) for each relay
     * to prevent electrical power-on glitching.
     */
    void initPins() override;

    /**
     * @brief Set physical state of specified relay channel and update internal cache.
     * Thread-safe.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     * @param state Target state (RELAY_OFF or RELAY_ON).
     * @return true if parameter valid and state applied, false otherwise.
     */
    bool setRelay(uint8_t relay_id, RelayState state) override;

    /**
     * @brief Retrieve cached physical state of specified relay channel.
     * Thread-safe.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     * @return RELAY_ON or RELAY_OFF (defaults to RELAY_OFF if relay_id invalid).
     */
    RelayState getRelayState(uint8_t relay_id) const override;

    /**
     * @brief Activate manual override for a specific relay for a duration in seconds.
     * Validates duration_s in [MIN_OVERRIDE_DURATION_S, MAX_OVERRIDE_DURATION_S].
     * Thread-safe.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     * @param forced_state State to force (RELAY_ON or RELAY_OFF).
     * @param duration_s Duration of override in seconds.
     * @return true if override started successfully, false if parameters invalid or fault latched.
     */
    bool startManualOverride(uint8_t relay_id, RelayState forced_state, uint32_t duration_s) override;

    /**
     * @brief Immediately cancel active manual override for specified relay.
     * Thread-safe.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     * @return true if cancelled or was inactive, false if relay_id invalid or mutex timeout.
     */
    bool cancelOverride(uint8_t relay_id) override;

    /**
     * @brief Check whether manual override is currently active for specified relay.
     * Thread-safe.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     * @return true if override active, false otherwise.
     */
    bool isOverrideActive(uint8_t relay_id) const override;

    /**
     * @brief Decrement manual override timer for specified relay by 1 second.
     * Automatically deactivates override when remaining duration reaches 0.
     * Thread-safe.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     */
    void tickOverride(uint8_t relay_id) override;

    /**
     * @brief Atomically process override timer tick and apply correct relay state.
     * If override is active and expires on this tick, immediately transitions relay
     * to scheduled_state (RELAY_ON or RELAY_OFF).
     * Thread-safe.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     * @param scheduled_state Target state prescribed by active schedule phase.
     * @return true if state applied successfully, false on mutex timeout or invalid parameter.
     */
    bool applyScheduledStateUnlessOverride(uint8_t relay_id, RelayState scheduled_state) override;

    /**
     * @brief Emergency fail-safe method to force relay pin LOW directly and latch safe-state.
     * Sets atomic fault latch, drives GPIO LOW, and synchronizes cache state.
     * Blocks all subsequent write HIGH commands until reset.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     * @return true if pin valid and driven LOW, false otherwise.
     */
    bool forceRelayOffEmergency(uint8_t relay_id) override;

    /**
     * @brief Query whether target relay channel is latched in fault safe-state.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     * @return true if fault latched, false otherwise.
     */
    bool isFaultLatched(uint8_t relay_id) const override;

    /**
     * @brief Get full snapshot of manual override state for specified relay.
     * Thread-safe.
     * @param relay_id Zero-based index of target relay [0..TOTAL_RELAYS-1].
     * @return RelayOverrideState struct.
     */
    RelayOverrideState getOverrideState(uint8_t relay_id) const override;

private:
    RelayState state_cache_[TOTAL_RELAYS];
    RelayOverrideState override_state_[TOTAL_RELAYS];
    std::atomic<bool> fault_latched_[TOTAL_RELAYS];

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    mutable SemaphoreHandle_t mutex_;
    mutable portMUX_TYPE spinlock_;
#endif

    uint8_t getPinForRelay(uint8_t relay_id) const;
    bool validateRelayPin(uint8_t relay_id, uint8_t &out_pin) const;
    void applySafeLatchedStateLocked(uint8_t relay_id, uint8_t pin);
    void applyRelayOutputLocked(uint8_t relay_id, uint8_t pin, RelayState state);
    bool setRelayLocked(uint8_t relay_id, RelayState state);
};
