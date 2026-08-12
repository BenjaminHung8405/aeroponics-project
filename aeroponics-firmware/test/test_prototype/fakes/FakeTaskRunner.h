#pragma once

#include "core/ITaskRunner.h"

class FakeTaskRunner : public ITaskRunner {
public:
    FakeTaskRunner() {
        for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
            start_task_results_[i] = true;
            wait_until_started_results_[i] = true;
            wait_until_manager_callback_exited_results_[i] = true;
            last_wait_until_started_timeout_ms_ = 0;
            manager_callback_active_[i] = true;
            stop_requested_[i] = false;
        }
    }

    void setStartTaskResult(uint8_t relay_id, bool result) {
        if (relay_id < TOTAL_RELAYS) start_task_results_[relay_id] = result;
    }

    void setWaitUntilStartedResult(uint8_t relay_id, bool result) {
        if (relay_id < TOTAL_RELAYS) wait_until_started_results_[relay_id] = result;
    }

    void setWaitUntilManagerCallbackExitedResult(uint8_t relay_id, bool result) {
        if (relay_id < TOTAL_RELAYS) wait_until_manager_callback_exited_results_[relay_id] = result;
    }

    uint32_t lastWaitUntilStartedTimeoutMs() const {
        return last_wait_until_started_timeout_ms_;
    }

    bool startTask(uint8_t relay_id, const RelayTaskContext& context) override {
        (void)context;
        if (relay_id >= TOTAL_RELAYS) return false;
        return start_task_results_[relay_id];
    }

    bool requestStop(uint8_t relay_id) override {
        if (relay_id >= TOTAL_RELAYS) return false;
        stop_requested_[relay_id] = true;
        return true;
    }

    bool consumeStopRequest(uint8_t relay_id, uint32_t generation) override {
        (void)generation;
        if (relay_id >= TOTAL_RELAYS) return false;
        bool result = stop_requested_[relay_id];
        stop_requested_[relay_id] = false;
        return result;
    }

    bool waitUntilStarted(uint8_t relay_id, uint32_t timeout_ms) override {
        if (relay_id >= TOTAL_RELAYS) return false;
        last_wait_until_started_timeout_ms_ = timeout_ms;
        return wait_until_started_results_[relay_id];
    }

    void notifyStarted(uint8_t relay_id, uint32_t generation, bool succeeded) override {
        (void)relay_id;
        (void)generation;
        (void)succeeded;
    }

    bool waitUntilManagerCallbackExited(uint8_t relay_id, uint32_t timeout_ms) override {
        (void)timeout_ms;
        if (relay_id >= TOTAL_RELAYS) return false;
        if (wait_until_manager_callback_exited_results_[relay_id]) {
            manager_callback_active_[relay_id] = false;
            return true;
        }
        return false;
    }

    void notifyManagerCallbackExited(uint8_t relay_id, uint32_t generation) override {
        (void)generation;
        if (relay_id < TOTAL_RELAYS) {
            manager_callback_active_[relay_id] = false;
        }
    }

    bool isManagerCallbackActive(uint8_t relay_id) const override {
        if (relay_id >= TOTAL_RELAYS) return false;
        return manager_callback_active_[relay_id];
    }

private:
    bool start_task_results_[TOTAL_RELAYS];
    bool wait_until_started_results_[TOTAL_RELAYS];
    bool wait_until_manager_callback_exited_results_[TOTAL_RELAYS];
    uint32_t last_wait_until_started_timeout_ms_;
    bool manager_callback_active_[TOTAL_RELAYS];
    bool stop_requested_[TOTAL_RELAYS];
};
