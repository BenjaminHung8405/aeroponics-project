#pragma once

#include <cstdint>
#include "config.h"

class ScheduleManager;

struct RelayTaskContext {
    uint8_t relay_id;
    ScheduleManager* manager;
};

/**
 * @brief Abstract interface for spawning and managing relay task execution lifecycle.
 * Decouples core ScheduleManager from platform task infrastructure (FreeRTOS vs Host/Fake).
 */
class ITaskRunner {
public:
    virtual ~ITaskRunner() = default;

    /**
     * @brief Spawn task for specified relay channel.
     * @param relay_id Zero-based relay index [0..TOTAL_RELAYS-1].
     * @return true if task created successfully, false otherwise.
     */
    virtual bool startTask(uint8_t relay_id, const RelayTaskContext& context) = 0;

    /**
     * @brief Request cooperative task stop. This call does not imply task exit.
     * @param relay_id Zero-based relay index [0..TOTAL_RELAYS-1].
     */
    virtual bool requestStop(uint8_t relay_id) = 0;

    /** Wait until the task registered its watchdog and accepted/rejected startup. */
    virtual bool waitUntilStarted(uint8_t relay_id, uint32_t timeout_ms) = 0;

    /** Report whether WDT registration in the owning task context succeeded. */
    virtual void notifyStarted(uint8_t relay_id, bool succeeded) = 0;

    /** @brief Wait until the task has exited its execution context. */
    virtual bool waitUntilStopped(uint8_t relay_id, uint32_t timeout_ms) = 0;

    /** Mark the current relay task as fully exited from the manager callback. */
    virtual void notifyStopped(uint8_t relay_id) = 0;

    /**
     * @brief Query whether task is running / active.
     * @param relay_id Zero-based relay index [0..TOTAL_RELAYS-1].
     * @return true if task alive, false otherwise.
     */
    virtual bool isTaskAlive(uint8_t relay_id) const = 0;
};
