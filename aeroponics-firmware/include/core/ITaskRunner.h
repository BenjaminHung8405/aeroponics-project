#pragma once

#include <cstdint>
#include "config.h"

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
     * @param task_func Entry function signature: void (*func)(uint8_t relay_id, void* arg).
     * @param arg Parameter passed to task entry function.
     * @return true if task created successfully, false otherwise.
     */
    virtual bool startTask(uint8_t relay_id, void (*task_func)(uint8_t relay_id, void* arg), void* arg) = 0;

    /**
     * @brief Request task stop and cleanup resources.
     * @param relay_id Zero-based relay index [0..TOTAL_RELAYS-1].
     */
    virtual void stopTask(uint8_t relay_id) = 0;

    /**
     * @brief Query whether task is running / active.
     * @param relay_id Zero-based relay index [0..TOTAL_RELAYS-1].
     * @return true if task alive, false otherwise.
     */
    virtual bool isTaskAlive(uint8_t relay_id) const = 0;
};
