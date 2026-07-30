#pragma once

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "config.h"
#include "nvs_storage.h"
#include "rtc_manager.h"
#include "relay_controller.h"

/**
 * @brief Represents current operational phase of a relay cycle.
 */
enum SchedulePhase {
    PHASE_SPRAYING,
    PHASE_COOLING_DOWN
};

/**
 * @brief Thread-safe snapshot of a relay's active runtime state.
 * Read-only Plain Old Data struct returned by value to prevent external mutation.
 */
struct RelayRuntimeState {
    SchedulePhase phase;
    uint32_t phase_remaining_s;
    RelayProfile current_profile;
    bool is_night_mode;
};

/**
 * @brief Schedule Manager orchestrating FreeRTOS background tasks for relay cycles.
 * Uses Dependency Injection pattern (NvsStorage, RtcManager, RelayController).
 */
class ScheduleManager {
public:
    ScheduleManager();
    ~ScheduleManager();

    /**
     * @brief Initialize schedule manager, inject dependencies, and create mutexes.
     * @param nvs Pointer to initialized NvsStorage instance.
     * @param rtc Pointer to initialized RtcManager instance.
     * @param relay Pointer to initialized RelayController instance.
     * @return true if dependencies are non-null and mutexes created successfully.
     */
    bool begin(NvsStorage* nvs, RtcManager* rtc, RelayController* relay);

    /**
     * @brief Create and pin 4 FreeRTOS tasks (1 per relay channel) on CORE_1.
     * @return true if all 4 tasks created successfully, false otherwise.
     */
    bool startAllTasks();

    /**
     * @brief Thread-safe update of relay configuration profile in RAM and NVS.
     * @param relay_id Zero-based relay index [0..TOTAL_RELAYS-1].
     * @param profile Target configuration profile values.
     * @return true if profile updated and persisted, false on error.
     */
    bool updateProfile(uint8_t relay_id, const RelayProfile &profile);

    /**
     * @brief Retrieve thread-safe runtime snapshot for a specific relay.
     * @param relay_id Zero-based relay index [0..TOTAL_RELAYS-1].
     * @return RelayRuntimeState snapshot by value.
     */
    RelayRuntimeState getRuntimeState(uint8_t relay_id) const;

private:
    NvsStorage* nvs_;
    RtcManager* rtc_;
    RelayController* relay_;

    RelayProfile profiles_[TOTAL_RELAYS];
    RelayRuntimeState runtime_states_[TOTAL_RELAYS];
    TaskHandle_t task_handles_[TOTAL_RELAYS];
    SemaphoreHandle_t profile_mutex_;
    SemaphoreHandle_t state_mutex_;
    bool is_initialized_;

    static void relayTaskWrapper(void* parameter);
    void relayTaskLoop(uint8_t relay_id);
};
