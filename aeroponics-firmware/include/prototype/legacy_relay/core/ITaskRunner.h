#pragma once

#include <cstdint>
#include "legacy_relay_config.h"

class ScheduleManager;

struct RelayTaskContext {
    uint8_t relay_id;
    ScheduleManager* manager;
    uint32_t generation;
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

    /**
     * Consume a cooperative stop request in the owning relay task context.
     * The generation prevents a request for an older task instance from
     * stopping a replacement task for the same relay.
     */
    virtual bool consumeStopRequest(uint8_t relay_id, uint32_t generation) = 0;

    /** Wait until the task registered its watchdog and accepted/rejected startup. */
    virtual bool waitUntilStarted(uint8_t relay_id, uint32_t timeout_ms) = 0;

    /** Report whether WDT registration in the owning task context succeeded. */
    virtual void notifyStarted(uint8_t relay_id, uint32_t generation, bool succeeded) = 0;

    /**
     * Wait until the relay task can no longer access ScheduleManager.
     *
     * This is a callback-exit acknowledgement, not a kernel-task join: a
     * self-deleting FreeRTOS task may still execute vTaskDelete() after this
     * method returns. Callers may safely release ScheduleManager state once
     * acknowledged, but must not treat it as proof that FreeRTOS reclaimed the
     * task control block.
     */
    virtual bool waitUntilManagerCallbackExited(uint8_t relay_id, uint32_t timeout_ms) = 0;

    /** Mark the current relay task as no longer able to access ScheduleManager. */
    virtual void notifyManagerCallbackExited(uint8_t relay_id, uint32_t generation) = 0;

    /**
     * @brief Query whether the task may still access ScheduleManager.
     * @param relay_id Zero-based relay index [0..TOTAL_RELAYS-1].
     * @return true if task alive, false otherwise.
     */
    virtual bool isManagerCallbackActive(uint8_t relay_id) const = 0;
};
