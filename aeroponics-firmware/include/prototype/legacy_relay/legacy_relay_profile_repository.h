#pragma once

#include <cstdint>
#include "core/IProfileRepository.h"
#include "nvs_backend.h"

/**
 * @brief Prototype repository adapter for reading/writing legacy 4-relay profiles via INvsBackend.
 * Kept isolated strictly in prototype/legacy_relay domain.
 */
class LegacyRelayProfileRepository : public IProfileRepository {
public:
    explicit LegacyRelayProfileRepository(INvsBackend* backend = nullptr);
    ~LegacyRelayProfileRepository() override = default;

    bool begin();
    bool loadProfile(uint8_t relay_id, RelayProfile &profile) override;
    bool saveProfile(uint8_t relay_id, const RelayProfile &profile) override;
    bool loadAllProfiles(RelayProfile profiles[TOTAL_RELAYS]) override;
    bool factoryReset() override;

private:
    INvsBackend* backend_;
    bool is_initialized_;
};
