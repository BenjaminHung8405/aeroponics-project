#pragma once

#include <cstdint>
#include "config.h"
#include "core/IProfileRepository.h"

/**
 * @brief ESP-IDF implementation of IProfileRepository for Non-Volatile Storage (NVS).
 * Manages persistent storage of relay profiles in NVS flash namespace 'aeroponics'.
 */
class NvsStorage : public IProfileRepository {
public:
    NvsStorage();
    ~NvsStorage() override;

    /**
     * @brief Initialize NVS flash and open the storage namespace.
     * @return true if initialized successfully, false otherwise.
     */
    bool begin();

    /**
     * @brief Load a specific relay profile from NVS with range validation & fallback defaults.
     * @param relay_id Zero-based index of relay [0..TOTAL_RELAYS-1].
     * @param profile Output reference to store the loaded or default profile.
     * @return true on success or safe fallback, false on invalid parameter.
     */
    bool loadProfile(uint8_t relay_id, RelayProfile &profile) override;

    /**
     * @brief Validate and persist a relay profile to NVS.
     * @param relay_id Zero-based index of relay [0..TOTAL_RELAYS-1].
     * @param profile The profile values to persist.
     * @return true if saved and committed successfully, false otherwise.
     */
    bool saveProfile(uint8_t relay_id, const RelayProfile &profile) override;

    /**
     * @brief Load configuration profiles for all relays into an array.
     * @param profiles Array of size TOTAL_RELAYS to receive the profiles.
     * @return true if all profiles loaded successfully, false on invalid parameter.
     */
    bool loadAllProfiles(RelayProfile profiles[TOTAL_RELAYS]) override;

    /**
     * @brief Perform factory reset by erasing the 'aeroponics' NVS storage namespace.
     * Rule: Erases only the aeroponics namespace using nvs_erase_all(handle).
     * @return true on successful erasure, false otherwise.
     */
    bool factoryReset() override;

private:
    bool is_initialized_;
};
