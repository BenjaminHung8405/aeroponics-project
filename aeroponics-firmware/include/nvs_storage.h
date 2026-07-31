#pragma once

#include <cstdint>
#include "config.h"

/**
 * @brief Configuration profile for an individual aeroponics relay channel.
 */
struct RelayProfile {
    uint32_t spray_day_s;
    uint32_t cooldown_day_s;
    uint32_t spray_night_s;
    uint32_t cooldown_night_s;
};

/**
 * @brief Repository interface for Non-Volatile Storage (NVS) operations.
 * Manages persistent storage of relay profiles and handle ESP-IDF NVS flash.
 */
class NvsStorage {
public:
    explicit NvsStorage(bool is_mock = false);
    ~NvsStorage();

    void setMockMode(bool enable) { is_mock_ = enable; }
    bool isMockMode() const { return is_mock_; }

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
    bool loadProfile(uint8_t relay_id, RelayProfile &profile);

    /**
     * @brief Validate and persist a relay profile to NVS.
     * @param relay_id Zero-based index of relay [0..TOTAL_RELAYS-1].
     * @param profile The profile values to persist.
     * @return true if saved and committed successfully, false otherwise.
     */
    bool saveProfile(uint8_t relay_id, const RelayProfile &profile);

    /**
     * @brief Load configuration profiles for all relays into an array.
     * @param profiles Array of size TOTAL_RELAYS to receive the profiles.
     * @return true if all profiles loaded successfully, false on invalid parameter.
     */
    bool loadAllProfiles(RelayProfile profiles[TOTAL_RELAYS]);

    /**
     * @brief Perform factory reset by erasing the NVS storage namespace.
     * @return true on successful erasure, false otherwise.
     */
    bool factoryReset();

private:
    bool is_mock_;
    bool is_initialized_;
};

