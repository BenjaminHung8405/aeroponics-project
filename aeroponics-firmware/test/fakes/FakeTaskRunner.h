#pragma once

#include "core/ITaskRunner.h"

class FakeTaskRunner : public ITaskRunner {
public:
    FakeTaskRunner() {
        for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
            task_alive_[i] = false;
            fail_at_relay_[i] = false;
        }
    }

    void setFailAtRelay(uint8_t relay_id, bool fail) {
        if (relay_id < TOTAL_RELAYS) {
            fail_at_relay_[relay_id] = fail;
        }
    }

    bool startTask(uint8_t relay_id, void (*task_func)(uint8_t relay_id, void* arg), void* arg) override {
        if (relay_id >= TOTAL_RELAYS || fail_at_relay_[relay_id]) {
            return false;
        }
        task_alive_[relay_id] = true;
        return true;
    }

    void stopTask(uint8_t relay_id) override {
        if (relay_id < TOTAL_RELAYS) {
            task_alive_[relay_id] = false;
        }
    }

    bool isTaskAlive(uint8_t relay_id) const override {
        if (relay_id >= TOTAL_RELAYS) {
            return false;
        }
        return task_alive_[relay_id];
    }

private:
    bool task_alive_[TOTAL_RELAYS];
    bool fail_at_relay_[TOTAL_RELAYS];
};
