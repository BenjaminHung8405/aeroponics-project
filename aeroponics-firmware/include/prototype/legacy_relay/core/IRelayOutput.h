#pragma once

#include <cstdint>
#include "legacy_relay_config.h"

enum RelayState {
    RELAY_OFF = 0,
    RELAY_ON = 1
};

struct RelayOverrideState {
    bool active;
    uint32_t remaining_s;
    RelayState forced_state;
};

/**
 * @brief Pure interface for controlling physical or virtual relay output channels.
 */
class IRelayOutput {
public:
    virtual ~IRelayOutput() = default;

    virtual void initPins() = 0;
    virtual bool setRelay(uint8_t relay_id, RelayState state) = 0;
    virtual RelayState getRelayState(uint8_t relay_id) const = 0;
    virtual bool startManualOverride(uint8_t relay_id, RelayState forced_state, uint32_t duration_s) = 0;
    virtual bool cancelOverride(uint8_t relay_id) = 0;
    virtual bool isOverrideActive(uint8_t relay_id) const = 0;
    virtual void tickOverride(uint8_t relay_id) = 0;
    virtual bool forceRelayOffEmergency(uint8_t relay_id) = 0;
    virtual bool isFaultLatched(uint8_t relay_id) const = 0;
    virtual RelayOverrideState getOverrideState(uint8_t relay_id) const = 0;
};
