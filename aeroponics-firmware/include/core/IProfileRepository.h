#pragma once

#include <cstdint>
#include "config.h"

struct RelayProfile {
    uint32_t spray_day_s;
    uint32_t cooldown_day_s;
    uint32_t spray_night_s;
    uint32_t cooldown_night_s;
};

/**
 * @brief Pure interface for persisting and loading relay schedule configuration profiles.
 */
class IProfileRepository {
public:
    virtual ~IProfileRepository() = default;

    virtual bool loadProfile(uint8_t relay_id, RelayProfile &profile) = 0;
    virtual bool saveProfile(uint8_t relay_id, const RelayProfile &profile) = 0;
    virtual bool loadAllProfiles(RelayProfile profiles[TOTAL_RELAYS]) = 0;
    virtual bool factoryReset() = 0;
};
