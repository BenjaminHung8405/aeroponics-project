#pragma once

#include "core/IProfileRepository.h"

class FakeProfileRepository : public IProfileRepository {
public:
    FakeProfileRepository() {
        for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
            profiles_[i] = RelayProfile{ DEFAULT_SPRAY_DAY_S, DEFAULT_COOLDOWN_DAY_S, DEFAULT_SPRAY_NIGHT_S, DEFAULT_COOLDOWN_NIGHT_S };
        }
    }

    bool loadProfile(uint8_t relay_id, RelayProfile &profile) override {
        if (relay_id >= TOTAL_RELAYS) return false;
        profile = profiles_[relay_id];
        return true;
    }

    bool saveProfile(uint8_t relay_id, const RelayProfile &profile) override {
        if (relay_id >= TOTAL_RELAYS) return false;
        // Range validation according to S1-NVS-03
        if (profile.spray_day_s < MIN_SPRAY_DURATION_S || profile.spray_day_s > MAX_SPRAY_DURATION_S) return false;
        if (profile.cooldown_day_s < MIN_COOLDOWN_DURATION_S || profile.cooldown_day_s > MAX_COOLDOWN_DURATION_S) return false;
        if (profile.spray_night_s < MIN_SPRAY_DURATION_S || profile.spray_night_s > MAX_SPRAY_DURATION_S) return false;
        if (profile.cooldown_night_s < MIN_COOLDOWN_DURATION_S || profile.cooldown_night_s > MAX_COOLDOWN_DURATION_S) return false;

        profiles_[relay_id] = profile;
        return true;
    }

    bool loadAllProfiles(RelayProfile profiles[TOTAL_RELAYS]) override {
        if (profiles == nullptr) return false;
        for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
            profiles[i] = profiles_[i];
        }
        return true;
    }

    bool factoryReset() override {
        for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
            profiles_[i] = RelayProfile{ DEFAULT_SPRAY_DAY_S, DEFAULT_COOLDOWN_DAY_S, DEFAULT_SPRAY_NIGHT_S, DEFAULT_COOLDOWN_NIGHT_S };
        }
        return true;
    }

private:
    RelayProfile profiles_[TOTAL_RELAYS];
};
