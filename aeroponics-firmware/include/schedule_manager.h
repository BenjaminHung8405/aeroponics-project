#pragma once

#include <atomic>
#include <cstdint>
#include "config.h"
#include "core/IProfileRepository.h"
#include "core/IClock.h"
#include "core/IRelayOutput.h"
#include "core/IWatchdog.h"
#include "core/ITaskRunner.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

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
 * @brief Lifecycle states for ScheduleManager tasks execution.
 */
enum class ScheduleLifecycleState {
    NOT_STARTED,
    STARTING,
    RUNNING,
    FAULTED
};

/**
 * @brief Schedule Manager orchestrating relay cycles.
 * Uses Dependency Injection pattern (IProfileRepository, IClock, IRelayOutput, IWatchdog, ITaskRunner).
 */
class ScheduleManager {
public:
    ScheduleManager();
    ~ScheduleManager();

    /**
     * @brief Initialize schedule manager, inject dependencies, and prepare operational resources.
     * @param nvs Pointer to IProfileRepository instance.
     * @param rtc Pointer to IClock instance.
     * @param relay Pointer to IRelayOutput instance.
     * @param wdt Relay-task watchdog adapter (required).
     * @param task_runner Relay-task lifecycle adapter (required).
     * @return true if mandatory dependencies are non-null.
     */
    bool begin(IProfileRepository* nvs, IClock* rtc, IRelayOutput* relay, IWatchdog* wdt, ITaskRunner* task_runner);

    /**
     * @brief Create and start tasks (1 per relay channel).
     * @return true if all 4 tasks created successfully, false otherwise.
     */
    bool startAllTasks();

    /**
     * @brief Thread-safe update of relay configuration profile in RAM and repository.
     * @param relay_id Zero-based relay index [0..TOTAL_RELAYS-1].
     * @param profile Target configuration profile values.
     * @return true if profile updated and persisted, false on error.
     */
    bool updateProfile(uint8_t relay_id, const RelayProfile &profile);

    /**
     * @brief Retrieve thread-safe runtime snapshot for a specific relay.
     * Returns true if state acquired safely, false on mutex timeout or invalid ID.
     */
    bool getRuntimeStateSafely(uint8_t relay_id, RelayRuntimeState &out_state) const;

    /**
     * @brief Retrieve thread-safe runtime snapshot for a specific relay (returns value).
     */
    RelayRuntimeState getRuntimeState(uint8_t relay_id) const;

    /**
     * @brief Step deterministic state machine for a relay channel by 1 second.
     * Can be invoked from host unit tests or task loops.
     * @param relay_id Zero-based relay index [0..TOTAL_RELAYS-1].
     * @return true on success, false if relay output or state update failed.
     */
    bool stepRelayPhase(uint8_t relay_id);

    /**
     * @brief Query whether task WDT is registered for relay channel.
     */
    bool isTaskWdtRegistered(uint8_t relay_id) const;

    /**
     * @brief Query whether task handle is active.
     */
    bool isTaskAlive(uint8_t relay_id) const;

    /**
     * @brief Retrieve current task lifecycle state.
     */
    ScheduleLifecycleState getLifecycleState() const;

    /** Entry point invoked by the typed context owned by ITaskRunner. */
    void runRelayTask(uint8_t relay_id);
    bool initializeRelayTask(uint8_t relay_id);

private:
    IProfileRepository* nvs_;
    IClock* rtc_;
    IRelayOutput* relay_;
    IWatchdog* wdt_;
    ITaskRunner* task_runner_;

    RelayProfile profiles_[TOTAL_RELAYS];
    RelayRuntimeState runtime_states_[TOTAL_RELAYS];
    bool is_initialized_;
    ScheduleLifecycleState lifecycle_state_;

    std::atomic<bool> wdt_registered_[TOTAL_RELAYS];
    std::atomic<bool> stop_requested_[TOTAL_RELAYS];

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    SemaphoreHandle_t profile_mutex_;
    SemaphoreHandle_t state_mutex_;

    void relayTaskLoop(uint8_t relay_id);
#endif

    bool registerTaskWdt(uint8_t relay_id);
    bool resetTaskWdt(uint8_t relay_id);
    bool deregisterTaskWdt(uint8_t relay_id);
    void handleTaskTermination(uint8_t relay_id, const char* reason);
    /**
     * Relay tasks deregister their own WDT subscription immediately before
     * returning. This routine only requests and joins those tasks; it never
     * calls the watchdog adapter from the caller/main-task context.
     */
    void performRollback(uint8_t created_count);

    void loadInitialProfiles(RelayProfile profile_snapshot[TOTAL_RELAYS]);
    bool fetchProfileSafely(uint8_t relay_id, RelayProfile &out_profile);
    bool updateRuntimePhaseState(uint8_t relay_id, SchedulePhase phase, uint32_t remaining_s, const RelayProfile* profile = nullptr, const bool* is_night = nullptr);
    bool loadStepSnapshot(uint8_t relay_id, RelayProfile& profile, RelayRuntimeState& state, bool& night_mode);
    bool processOverrideTick(uint8_t relay_id, const RelayProfile& profile, const RelayRuntimeState& state, bool night_mode);
    bool processScheduledTick(uint8_t relay_id, RelayProfile& profile, RelayRuntimeState& state, bool night_mode);
    bool applyScheduledRelayState(uint8_t relay_id, SchedulePhase phase);
    bool failRelaySafely(uint8_t relay_id, const char* reason);
};
