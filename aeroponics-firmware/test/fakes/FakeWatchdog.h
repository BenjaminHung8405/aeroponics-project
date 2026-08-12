#pragma once

#include "core/IWatchdog.h"
#include "config.h"

constexpr uint8_t MAX_WATCHDOG_CHANNELS = 8;

class FakeWatchdog : public IWatchdog {
public:
    FakeWatchdog() : main_task_registered_(true), main_task_reset_count_(0) {
        for (uint8_t i = 0; i < MAX_WATCHDOG_CHANNELS; ++i) {
            registered_[i] = false;
            reset_count_[i] = 0;
            fail_registration_[i] = false;
        }
    }

    void setRegistrationFailure(uint8_t channel_id, bool fail) {
        if (channel_id < MAX_WATCHDOG_CHANNELS) fail_registration_[channel_id] = fail;
    }

    bool registerWatchdog(uint8_t channel_id) override {
        if (channel_id >= MAX_WATCHDOG_CHANNELS || fail_registration_[channel_id]) return false;
        registered_[channel_id] = true;
        return true;
    }

    bool resetWatchdog(uint8_t channel_id) override {
        if (channel_id >= MAX_WATCHDOG_CHANNELS || !registered_[channel_id]) return false;
        reset_count_[channel_id]++;
        return true;
    }

    bool deregisterWatchdog(uint8_t channel_id) override {
        if (channel_id >= MAX_WATCHDOG_CHANNELS) return false;
        registered_[channel_id] = false;
        return true;
    }

    bool isRegistered(uint8_t channel_id) const {
        if (channel_id >= MAX_WATCHDOG_CHANNELS) return false;
        return registered_[channel_id];
    }

    uint32_t getResetCount(uint8_t channel_id) const {
        if (channel_id >= MAX_WATCHDOG_CHANNELS) return 0;
        return reset_count_[channel_id];
    }

    bool resetMainTaskWatchdog() {
        if (!main_task_registered_) return false;
        ++main_task_reset_count_;
        return true;
    }

    bool isMainTaskRegistered() const { return main_task_registered_; }
    uint32_t getMainTaskResetCount() const { return main_task_reset_count_; }

private:
    bool registered_[MAX_WATCHDOG_CHANNELS];
    uint32_t reset_count_[MAX_WATCHDOG_CHANNELS];
    bool fail_registration_[MAX_WATCHDOG_CHANNELS];
    bool main_task_registered_;
    uint32_t main_task_reset_count_;
};
