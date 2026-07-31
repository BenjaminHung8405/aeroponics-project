#pragma once

#include "core/IWatchdog.h"
#include "config.h"

class FakeWatchdog : public IWatchdog {
public:
    FakeWatchdog() {
        for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
            registered_[i] = false;
            reset_count_[i] = 0;
        }
    }

    bool registerWatchdog(uint8_t relay_id) override {
        if (relay_id >= TOTAL_RELAYS) return false;
        registered_[relay_id] = true;
        return true;
    }

    bool resetWatchdog(uint8_t relay_id) override {
        if (relay_id >= TOTAL_RELAYS || !registered_[relay_id]) return false;
        reset_count_[relay_id]++;
        return true;
    }

    bool deregisterWatchdog(uint8_t relay_id) override {
        if (relay_id >= TOTAL_RELAYS) return false;
        registered_[relay_id] = false;
        return true;
    }

    bool isRegistered(uint8_t relay_id) const {
        if (relay_id >= TOTAL_RELAYS) return false;
        return registered_[relay_id];
    }

    uint32_t getResetCount(uint8_t relay_id) const {
        if (relay_id >= TOTAL_RELAYS) return 0;
        return reset_count_[relay_id];
    }

private:
    bool registered_[TOTAL_RELAYS];
    uint32_t reset_count_[TOTAL_RELAYS];
};
