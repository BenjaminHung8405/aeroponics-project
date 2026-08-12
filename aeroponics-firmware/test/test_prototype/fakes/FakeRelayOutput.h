#pragma once

#include "core/IRelayOutput.h"

class FakeRelayOutput : public IRelayOutput {
public:
    FakeRelayOutput() {
        for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
            states_[i] = RELAY_OFF;
            overrides_[i] = RelayOverrideState{ false, 0, RELAY_OFF };
            fault_latched_[i] = false;
            fail_scheduled_apply_[i] = false;
            override_expiry_transitions_[i] = 0;
        }
    }

    void initPins() override {
        for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
            states_[i] = RELAY_OFF;
            overrides_[i] = RelayOverrideState{ false, 0, RELAY_OFF };
            fault_latched_[i] = false;
            fail_scheduled_apply_[i] = false;
            override_expiry_transitions_[i] = 0;
        }
    }

    void setFailScheduledApply(uint8_t relay_id, bool fail) {
        if (relay_id < TOTAL_RELAYS) {
            fail_scheduled_apply_[relay_id] = fail;
        }
    }

    bool setRelay(uint8_t relay_id, RelayState state) override {
        if (relay_id >= TOTAL_RELAYS || fault_latched_[relay_id] || fail_scheduled_apply_[relay_id]) {
            return false;
        }
        states_[relay_id] = state;
        return true;
    }

    RelayState getRelayState(uint8_t relay_id) const override {
        if (relay_id >= TOTAL_RELAYS || fault_latched_[relay_id]) {
            return RELAY_OFF;
        }
        return states_[relay_id];
    }

    bool startManualOverride(uint8_t relay_id, RelayState forced_state, uint32_t duration_s) override {
        if (relay_id >= TOTAL_RELAYS || fault_latched_[relay_id]) {
            return false;
        }
        if (duration_s < MIN_OVERRIDE_DURATION_S || duration_s > MAX_OVERRIDE_DURATION_S) {
            return false;
        }
        overrides_[relay_id].active = true;
        overrides_[relay_id].remaining_s = duration_s;
        overrides_[relay_id].forced_state = forced_state;
        states_[relay_id] = forced_state;
        return true;
    }

    bool cancelOverride(uint8_t relay_id) override {
        if (relay_id >= TOTAL_RELAYS) {
            return false;
        }
        overrides_[relay_id].active = false;
        overrides_[relay_id].remaining_s = 0;
        return true;
    }

    bool isOverrideActive(uint8_t relay_id) const override {
        if (relay_id >= TOTAL_RELAYS) {
            return false;
        }
        return overrides_[relay_id].active;
    }

    void tickOverride(uint8_t relay_id) override {
        if (relay_id >= TOTAL_RELAYS) {
            return;
        }
        if (overrides_[relay_id].active) {
            if (overrides_[relay_id].remaining_s > 0) {
                overrides_[relay_id].remaining_s--;
            }
            if (overrides_[relay_id].remaining_s == 0) {
                overrides_[relay_id].active = false;
                ++override_expiry_transitions_[relay_id];
            }
        }
    }

    bool forceRelayOffEmergency(uint8_t relay_id) override {
        if (relay_id >= TOTAL_RELAYS) {
            return false;
        }
        fault_latched_[relay_id] = true;
        states_[relay_id] = RELAY_OFF;
        overrides_[relay_id].active = false;
        return true;
    }

    bool isFaultLatched(uint8_t relay_id) const override {
        if (relay_id >= TOTAL_RELAYS) {
            return true;
        }
        return fault_latched_[relay_id];
    }

    RelayOverrideState getOverrideState(uint8_t relay_id) const override {
        if (relay_id >= TOTAL_RELAYS) {
            return RelayOverrideState{ false, 0, RELAY_OFF };
        }
        return overrides_[relay_id];
    }

    uint32_t getOverrideExpiryTransitionCount(uint8_t relay_id) const {
        return relay_id < TOTAL_RELAYS ? override_expiry_transitions_[relay_id] : 0;
    }

private:
    RelayState states_[TOTAL_RELAYS];
    RelayOverrideState overrides_[TOTAL_RELAYS];
    bool fault_latched_[TOTAL_RELAYS];
    bool fail_scheduled_apply_[TOTAL_RELAYS];
    uint32_t override_expiry_transitions_[TOTAL_RELAYS];
};
