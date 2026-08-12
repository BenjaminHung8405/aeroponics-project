#pragma once

#include <cstdint>
#include "config.h"
#include "nvs_backend.h"

#if defined(LEGACY_RELAY_SUPPORT)
#include "prototype/legacy_relay/legacy_relay_config.h"
#include "prototype/legacy_relay/core/IProfileRepository.h"
#endif

/**
 * @brief ESP-IDF implementation of NVS storage backend.
 * Manages persistent storage in NVS flash namespace 'aeroponics'.
 */
#if defined(LEGACY_RELAY_SUPPORT)
class NvsStorage : public IProfileRepository {
#else
class NvsStorage {
#endif
public:
    explicit NvsStorage(INvsBackend* backend = nullptr);
    virtual ~NvsStorage();

    /**
     * @brief Initialize NVS flash and open the storage namespace.
     * @return true if initialized successfully, false otherwise.
     */
    bool begin();

    /**
     * @brief Perform factory reset by erasing the 'aeroponics' NVS storage namespace.
     * @return true on successful erasure, false otherwise.
     */
    bool factoryReset()
#if defined(LEGACY_RELAY_SUPPORT)
    override
#endif
    ;

#if defined(LEGACY_RELAY_SUPPORT)
    bool loadProfile(uint8_t relay_id, RelayProfile &profile) override;
    bool saveProfile(uint8_t relay_id, const RelayProfile &profile) override;
    bool loadAllProfiles(RelayProfile profiles[TOTAL_RELAYS]) override;
#endif

private:
    INvsBackend* backend_;
    bool is_initialized_;
};
