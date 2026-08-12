#include "nvs_storage.h"

#include <cstdio>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#define NVS_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#define NVS_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#define NVS_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#else
#define NVS_LOGE(...) do {} while (false)
#define NVS_LOGI(...) do {} while (false)
#define NVS_LOGW(...) do {} while (false)
#endif

namespace {

constexpr char TAG[] = "NVS_STORAGE";
constexpr char NVS_NAMESPACE[] = "aeroponics";

#if defined(LEGACY_RELAY_SUPPORT)
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
                       uint32_t& resolved_value, const char* name, uint32_t min_value,
                       uint32_t max_value, uint8_t relay_id) {
    if (backend.isOk(result) && validator(value)) {
        resolved_value = value;
        return true;
    }
    if (backend.isOk(result)) {
        NVS_LOGW("Relay %u %s (%u) out of range [%u-%u], fallback to default (%u)",
                 relay_id, name, value, min_value, max_value, default_value);
        resolved_value = default_value;
        return true;
    }
    if (backend.isNotFound(result)) {
        NVS_LOGI("Relay %u %s not found in NVS, using default (%u)", relay_id, name, default_value);
        resolved_value = default_value;
        return true;
    }

    NVS_LOGE("Failed to read relay %u %s from NVS: %s (%ld). Using safe default (%u)",
             relay_id, name, backend.errorName(result), static_cast<long>(result), default_value);
    resolved_value = default_value;
    return false;
}

bool validateProfile(uint8_t relay_id, const RelayProfile& profile) {
    if (!isSprayValid(profile.spray_day_s) || !isSprayValid(profile.spray_night_s) ||
        !isCooldownValid(profile.cooldown_day_s) || !isCooldownValid(profile.cooldown_night_s)) {
        NVS_LOGW("Cannot save profile for relay %u: profile contains out-of-range duration", relay_id);
        return false;
    }
    return true;
}

void makeKeys(uint8_t relay_id, char (&key_sd)[16], char (&key_cd)[16],
              char (&key_sn)[16], char (&key_cn)[16]) {
    snprintf(key_sd, sizeof(key_sd), "sd_%u", relay_id);
    snprintf(key_cd, sizeof(key_cd), "cd_%u", relay_id);
    snprintf(key_sn, sizeof(key_sn), "sn_%u", relay_id);
    snprintf(key_cn, sizeof(key_cn), "cn_%u", relay_id);
}
#endif

#if defined(ESP_PLATFORM) || defined(ARDUINO)
class EspIdfNvsBackend final : public INvsBackend {
public:
    Result flashInit() override { return nvs_flash_init(); }
    Result flashErase() override { return nvs_flash_erase(); }
    bool isOk(Result result) const override { return result == ESP_OK; }
    bool isNotFound(Result result) const override { return result == ESP_ERR_NVS_NOT_FOUND; }
    bool requiresFlashErase(Result result) const override {
        return result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND;
    }
    const char* errorName(Result result) const override { return esp_err_to_name(result); }

    Result open(const char* name_space, bool read_only, Handle& handle) override {
        nvs_handle_t native_handle = 0;
        const esp_err_t result = nvs_open(name_space, read_only ? NVS_READONLY : NVS_READWRITE, &native_handle);
        handle = static_cast<Handle>(native_handle);
        return result;
    }
    Result getU32(Handle handle, const char* key, uint32_t& value) override {
        return nvs_get_u32(static_cast<nvs_handle_t>(handle), key, &value);
    }
    Result setU32(Handle handle, const char* key, uint32_t value) override {
        return nvs_set_u32(static_cast<nvs_handle_t>(handle), key, value);
    }
    Result commit(Handle handle) override { return nvs_commit(static_cast<nvs_handle_t>(handle)); }
    Result eraseAll(Handle handle) override { return nvs_erase_all(static_cast<nvs_handle_t>(handle)); }
    void close(Handle handle) override { nvs_close(static_cast<nvs_handle_t>(handle)); }
};

EspIdfNvsBackend default_backend;
#endif

} // namespace

NvsStorage::NvsStorage(INvsBackend* backend) : backend_(backend), is_initialized_(false) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (backend_ == nullptr) backend_ = &default_backend;
#endif
}

NvsStorage::~NvsStorage() = default;

bool NvsStorage::begin() {
    if (backend_ == nullptr) return false;

    INvsBackend::Result result = backend_->flashInit();
    if (backend_->requiresFlashErase(result)) {
        NVS_LOGW("NVS partition requires erase before initialization");
        if (!backend_->isOk(backend_->flashErase())) {
            is_initialized_ = false;
            return false;
        }
        result = backend_->flashInit();
    }

    is_initialized_ = backend_->isOk(result);
    if (is_initialized_) NVS_LOGI("NVS storage initialized successfully");
    return is_initialized_;
}

bool NvsStorage::factoryReset() {
    if (!is_initialized_ || backend_ == nullptr) {
        NVS_LOGE("Factory reset failed: NVS storage not initialized");
        return false;
    }

    INvsBackend::Handle handle = 0;
    INvsBackend::Result result = backend_->open(NVS_NAMESPACE, false, handle);
    if (!backend_->isOk(result)) {
        NVS_LOGE("Factory reset failed: Unable to open namespace '%s': %s (%ld)",
                 NVS_NAMESPACE, backend_->errorName(result), static_cast<long>(result));
        return false;
    }

    result = backend_->eraseAll(handle);
    if (!backend_->isOk(result)) {
        NVS_LOGE("Factory reset failed: Erase all returned %s (%ld)",
                 backend_->errorName(result), static_cast<long>(result));
        backend_->close(handle);
        return false;
    }

    result = backend_->commit(handle);
    backend_->close(handle);
    if (!backend_->isOk(result)) {
        NVS_LOGE("Factory reset failed: Commit returned %s (%ld)",
                 backend_->errorName(result), static_cast<long>(result));
        return false;
    }

    NVS_LOGI("Factory reset successfully erased namespace '%s'", NVS_NAMESPACE);
    return true;
}

#if defined(LEGACY_RELAY_SUPPORT)
bool NvsStorage::loadProfile(uint8_t relay_id, RelayProfile& profile) {
    profile = defaultProfile();
    if (!is_initialized_ || relay_id >= TOTAL_RELAYS || backend_ == nullptr) return false;

    INvsBackend::Handle handle = 0;
    INvsBackend::Result result = backend_->open(NVS_NAMESPACE, true, handle);
    if (!backend_->isOk(result)) return false;

    char key_sd[16], key_cd[16], key_sn[16], key_cn[16];
    makeKeys(relay_id, key_sd, key_cd, key_sn, key_cn);

    uint32_t val_sd = 0, val_cd = 0, val_sn = 0, val_cn = 0;
    const INvsBackend::Result res_sd = backend_->getU32(handle, key_sd, val_sd);
    const INvsBackend::Result res_cd = backend_->getU32(handle, key_cd, val_cd);
    const INvsBackend::Result res_sn = backend_->getU32(handle, key_sn, val_sn);
    const INvsBackend::Result res_cn = backend_->getU32(handle, key_cn, val_cn);
    backend_->close(handle);

    bool ok = true;
    ok &= resolveFieldValue(*backend_, res_sd, val_sd, DEFAULT_SPRAY_DAY_S, isSprayValid,
                            profile.spray_day_s, "spray_day_s", MIN_SPRAY_DURATION_S,
                            MAX_SPRAY_DURATION_S, relay_id);
    ok &= resolveFieldValue(*backend_, res_cd, val_cd, DEFAULT_COOLDOWN_DAY_S, isCooldownValid,
                            profile.cooldown_day_s, "cooldown_day_s", MIN_COOLDOWN_DURATION_S,
                            MAX_COOLDOWN_DURATION_S, relay_id);
    ok &= resolveFieldValue(*backend_, res_sn, val_sn, DEFAULT_SPRAY_NIGHT_S, isSprayValid,
                            profile.spray_night_s, "spray_night_s", MIN_SPRAY_DURATION_S,
                            MAX_SPRAY_DURATION_S, relay_id);
    ok &= resolveFieldValue(*backend_, res_cn, val_cn, DEFAULT_COOLDOWN_NIGHT_S, isCooldownValid,
                            profile.cooldown_night_s, "cooldown_night_s", MIN_COOLDOWN_DURATION_S,
                            MAX_COOLDOWN_DURATION_S, relay_id);
    return ok;
}

bool NvsStorage::saveProfile(uint8_t relay_id, const RelayProfile& profile) {
    if (!is_initialized_ || relay_id >= TOTAL_RELAYS || backend_ == nullptr || !validateProfile(relay_id, profile)) return false;

    INvsBackend::Handle handle = 0;
    INvsBackend::Result result = backend_->open(NVS_NAMESPACE, false, handle);
    if (!backend_->isOk(result)) return false;

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

bool NvsStorage::loadAllProfiles(RelayProfile profiles[TOTAL_RELAYS]) {
    if (profiles == nullptr) return false;
    bool all_ok = true;
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        if (!loadProfile(i, profiles[i])) all_ok = false;
    }
    return all_ok;
}
#endif
