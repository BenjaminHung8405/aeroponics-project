#include "prototype/legacy_relay/legacy_relay_profile_repository.h"
#include <cstdio>
#include <cstring>

namespace {

constexpr char NVS_NAMESPACE[] = "aeroponics";

bool isSprayValid(uint32_t seconds) {
    return seconds >= MIN_SPRAY_DURATION_S && seconds <= MAX_SPRAY_DURATION_S;
}

bool isCooldownValid(uint32_t seconds) {
    return seconds >= MIN_COOLDOWN_DURATION_S && seconds <= MAX_COOLDOWN_DURATION_S;
}

RelayProfile defaultProfile() {
    return RelayProfile{DEFAULT_SPRAY_DAY_S, DEFAULT_COOLDOWN_DAY_S,
                        DEFAULT_SPRAY_NIGHT_S, DEFAULT_COOLDOWN_NIGHT_S};
}

bool resolveFieldValue(INvsBackend& backend, INvsBackend::Result result, uint32_t value,
                       uint32_t default_value, bool (*validator)(uint32_t),
                       uint32_t& resolved_value) {
    if (backend.isOk(result) && validator(value)) {
        resolved_value = value;
        return true;
    }
    if (backend.isOk(result) || backend.isNotFound(result)) {
        resolved_value = default_value;
        return true;
    }
    resolved_value = default_value;
    return false;
}

bool validateProfile(const RelayProfile& profile) {
    return isSprayValid(profile.spray_day_s) && isSprayValid(profile.spray_night_s) &&
           isCooldownValid(profile.cooldown_day_s) && isCooldownValid(profile.cooldown_night_s);
}

void makeKeys(uint8_t relay_id, char (&key_sd)[16], char (&key_cd)[16],
              char (&key_sn)[16], char (&key_cn)[16]) {
    std::snprintf(key_sd, sizeof(key_sd), "sd_%u", relay_id);
    std::snprintf(key_cd, sizeof(key_cd), "cd_%u", relay_id);
    std::snprintf(key_sn, sizeof(key_sn), "sn_%u", relay_id);
    std::snprintf(key_cn, sizeof(key_cn), "cn_%u", relay_id);
}

} // namespace

LegacyRelayProfileRepository::LegacyRelayProfileRepository(INvsBackend* backend)
    : backend_(backend), is_initialized_(false) {}

bool LegacyRelayProfileRepository::begin() {
    if (backend_ == nullptr) return false;
    INvsBackend::Result res = backend_->flashInit();
    if (backend_->requiresFlashErase(res)) {
        if (!backend_->isOk(backend_->flashErase())) return false;
        res = backend_->flashInit();
    }
    is_initialized_ = backend_->isOk(res);
    return is_initialized_;
}

bool LegacyRelayProfileRepository::factoryReset() {
    if (!is_initialized_ || backend_ == nullptr) return false;
    INvsBackend::Handle handle = 0;
    if (!backend_->isOk(backend_->open(NVS_NAMESPACE, false, handle))) return false;
    bool ok = backend_->isOk(backend_->eraseAll(handle)) && backend_->isOk(backend_->commit(handle));
    backend_->close(handle);
    return ok;
}

bool LegacyRelayProfileRepository::loadProfile(uint8_t relay_id, RelayProfile& profile) {
    profile = defaultProfile();
    if (!is_initialized_ || relay_id >= TOTAL_RELAYS || backend_ == nullptr) return false;

    INvsBackend::Handle handle = 0;
    if (!backend_->isOk(backend_->open(NVS_NAMESPACE, true, handle))) return false;

    char key_sd[16], key_cd[16], key_sn[16], key_cn[16];
    makeKeys(relay_id, key_sd, key_cd, key_sn, key_cn);

    uint32_t val_sd = 0, val_cd = 0, val_sn = 0, val_cn = 0;
    const auto res_sd = backend_->getU32(handle, key_sd, val_sd);
    const auto res_cd = backend_->getU32(handle, key_cd, val_cd);
    const auto res_sn = backend_->getU32(handle, key_sn, val_sn);
    const auto res_cn = backend_->getU32(handle, key_cn, val_cn);
    backend_->close(handle);

    bool ok = true;
    ok &= resolveFieldValue(*backend_, res_sd, val_sd, DEFAULT_SPRAY_DAY_S, isSprayValid, profile.spray_day_s);
    ok &= resolveFieldValue(*backend_, res_cd, val_cd, DEFAULT_COOLDOWN_DAY_S, isCooldownValid, profile.cooldown_day_s);
    ok &= resolveFieldValue(*backend_, res_sn, val_sn, DEFAULT_SPRAY_NIGHT_S, isSprayValid, profile.spray_night_s);
    ok &= resolveFieldValue(*backend_, res_cn, val_cn, DEFAULT_COOLDOWN_NIGHT_S, isCooldownValid, profile.cooldown_night_s);
    return ok;
}

bool LegacyRelayProfileRepository::saveProfile(uint8_t relay_id, const RelayProfile& profile) {
    if (!is_initialized_ || relay_id >= TOTAL_RELAYS || backend_ == nullptr || !validateProfile(profile)) return false;

    INvsBackend::Handle handle = 0;
    if (!backend_->isOk(backend_->open(NVS_NAMESPACE, false, handle))) return false;

    char key_sd[16], key_cd[16], key_sn[16], key_cn[16];
    makeKeys(relay_id, key_sd, key_cd, key_sn, key_cn);

    bool ok = backend_->isOk(backend_->setU32(handle, key_sd, profile.spray_day_s)) &&
              backend_->isOk(backend_->setU32(handle, key_cd, profile.cooldown_day_s)) &&
              backend_->isOk(backend_->setU32(handle, key_sn, profile.spray_night_s)) &&
              backend_->isOk(backend_->setU32(handle, key_cn, profile.cooldown_night_s)) &&
              backend_->isOk(backend_->commit(handle));

    backend_->close(handle);
    return ok;
}

bool LegacyRelayProfileRepository::loadAllProfiles(RelayProfile profiles[TOTAL_RELAYS]) {
    if (profiles == nullptr) return false;
    bool all_ok = true;
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        if (!loadProfile(i, profiles[i])) all_ok = false;
    }
    return all_ok;
}
