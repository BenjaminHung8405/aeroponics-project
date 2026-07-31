#pragma once

#include "core/ITaskRunner.h"
#include "schedule_manager.h"

class FakeTaskRunner : public ITaskRunner {
public:
    FakeTaskRunner() {
        for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
            task_alive_[i] = false;
            fail_at_relay_[i] = false;
            stop_requested_[i] = false;
            immediate_exit_[i] = false;
            hold_callback_on_stop_[i] = false;
            contexts_[i] = RelayTaskContext{ i, nullptr, 0 };
        }
    }

    void setFailAtRelay(uint8_t relay_id, bool fail) {
        if (relay_id < TOTAL_RELAYS) {
            fail_at_relay_[relay_id] = fail;
        }
    }

    void setExitImmediatelyDuringStart(uint8_t relay_id, bool enabled) {
        if (relay_id < TOTAL_RELAYS) {
            immediate_exit_[relay_id] = enabled;
        }
    }

    void setHoldCallbackOnStop(uint8_t relay_id, bool enabled) {
        if (relay_id < TOTAL_RELAYS) {
            hold_callback_on_stop_[relay_id] = enabled;
        }
    }

    bool startTask(uint8_t relay_id, const RelayTaskContext& context) override {
        (void)context;
        if (relay_id >= TOTAL_RELAYS || fail_at_relay_[relay_id]) {
            return false;
        }
        task_alive_[relay_id] = true;
        contexts_[relay_id] = context;
        context.manager->runRelayTask(relay_id, context.generation);
        if (immediate_exit_[relay_id]) {
            // Reproduces a callback that exits before startTask() returns.
            notifyManagerCallbackExited(relay_id, context.generation);
        }
        return true;
    }

    bool requestStop(uint8_t relay_id) override {
        if (relay_id < TOTAL_RELAYS) {
            stop_requested_[relay_id] = true;
            if (!hold_callback_on_stop_[relay_id] && contexts_[relay_id].manager != nullptr) {
                contexts_[relay_id].manager->runRelayTask(relay_id, contexts_[relay_id].generation);
            }
            return true;
        }
        return false;
    }

    bool waitUntilStarted(uint8_t relay_id, uint32_t timeout_ms) override {
        (void)timeout_ms;
        return relay_id < TOTAL_RELAYS && task_alive_[relay_id];
    }

    void notifyStarted(uint8_t relay_id, uint32_t generation, bool succeeded) override {
        (void)relay_id;
        (void)generation;
        (void)succeeded;
    }

    bool waitUntilManagerCallbackExited(uint8_t relay_id, uint32_t timeout_ms) override {
        (void)timeout_ms;
        return relay_id < TOTAL_RELAYS && !task_alive_[relay_id];
    }

    void notifyManagerCallbackExited(uint8_t relay_id, uint32_t generation) override {
        (void)generation;
        if (relay_id < TOTAL_RELAYS) {
            task_alive_[relay_id] = false;
        }
    }

    bool isManagerCallbackActive(uint8_t relay_id) const override {
        if (relay_id >= TOTAL_RELAYS) {
            return false;
        }
        return task_alive_[relay_id];
    }

    bool wasStopRequested(uint8_t relay_id) const {
        return relay_id < TOTAL_RELAYS && stop_requested_[relay_id];
    }

private:
    bool task_alive_[TOTAL_RELAYS];
    bool fail_at_relay_[TOTAL_RELAYS];
    bool stop_requested_[TOTAL_RELAYS];
    bool immediate_exit_[TOTAL_RELAYS];
    bool hold_callback_on_stop_[TOTAL_RELAYS];
    RelayTaskContext contexts_[TOTAL_RELAYS];
};
