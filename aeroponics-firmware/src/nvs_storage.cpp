#include "nvs_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <cstdio>

static const char *TAG = "NVS_STORAGE";
static const char *NVS_NAMESPACE = "aeroponics";

static inline bool isSprayValid(uint32_t seconds) {
    return (seconds >= MIN_SPRAY_DURATION_S && seconds <= MAX_SPRAY_DURATION_S);
}

static inline bool isCooldownValid(uint32_t seconds) {
    return (seconds >= MIN_COOLDOWN_DURATION_S && seconds <= MAX_COOLDOWN_DURATION_S);
}

static uint32_t resolveFieldValue(esp_err_t err, uint32_t val, uint32_t default_val,

                                   bool (*validator)(uint32_t),
                                   const char *name, uint32_t min_v, uint32_t max_v, uint8_t relay_id) {
    if (err == ESP_OK && validator(val)) {
        return val;
    }
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "Relay %u %s (%u) out of range [%u-%u], fallback to default (%u)",
                 relay_id, name, val, min_v, max_v, default_val);
    } else {
        ESP_LOGI(TAG, "Relay %u %s not found in NVS, using default (%u)", relay_id, name, default_val);
    }
    return default_val;
}

static bool validateProfile(uint8_t relay_id, const RelayProfile &profile) {
    if (!isSprayValid(profile.spray_day_s)) {
        ESP_LOGW(TAG, "Cannot save profile for relay %u: spray_day_s (%u) out of range [%u-%u]",
                 relay_id, profile.spray_day_s, MIN_SPRAY_DURATION_S, MAX_SPRAY_DURATION_S);
        return false;
    }
    if (!isCooldownValid(profile.cooldown_day_s)) {
        ESP_LOGW(TAG, "Cannot save profile for relay %u: cooldown_day_s (%u) out of range [%u-%u]",
                 relay_id, profile.cooldown_day_s, MIN_COOLDOWN_DURATION_S, MAX_COOLDOWN_DURATION_S);
        return false;
    }
    if (!isSprayValid(profile.spray_night_s)) {
        ESP_LOGW(TAG, "Cannot save profile for relay %u: spray_night_s (%u) out of range [%u-%u]",
                 relay_id, profile.spray_night_s, MIN_SPRAY_DURATION_S, MAX_SPRAY_DURATION_S);
        return false;
    }
    if (!isCooldownValid(profile.cooldown_night_s)) {
        ESP_LOGW(TAG, "Cannot save profile for relay %u: cooldown_night_s (%u) out of range [%u-%u]",
                 relay_id, profile.cooldown_night_s, MIN_COOLDOWN_DURATION_S, MAX_COOLDOWN_DURATION_S);
        return false;
    }
    return true;
}

NvsStorage::NvsStorage(bool is_mock) : is_mock_(is_mock), is_initialized_(false) {}

NvsStorage::~NvsStorage() {}

bool NvsStorage::begin() {
    if (is_mock_) {
        is_initialized_ = true;
        ESP_LOGI(TAG, "NVS storage initialized in MOCK mode (no flash hardware calls)");
        return true;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition corrupted or updated version found, erasing partition...");
        esp_err_t erase_err = nvs_flash_erase();
        if (erase_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase NVS flash partition: 0x%x", erase_err);
            is_initialized_ = false;
            return false;
        }
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS flash initialization failed: 0x%x", err);
        is_initialized_ = false;
        return false;
    }

    is_initialized_ = true;
    ESP_LOGI(TAG, "NVS storage initialized successfully");
    return true;
}

bool NvsStorage::loadProfile(uint8_t relay_id, RelayProfile &profile) {
    if (relay_id >= TOTAL_RELAYS) {
        ESP_LOGE(TAG, "Invalid relay_id: %u", relay_id);
        return false;
    }

    if (is_mock_) {
        profile = RelayProfile{ DEFAULT_SPRAY_DAY_S, DEFAULT_COOLDOWN_DAY_S, DEFAULT_SPRAY_NIGHT_S, DEFAULT_COOLDOWN_NIGHT_S };
        return true;
    }

    if (!is_initialized_) {
        ESP_LOGE(TAG, "NvsStorage not initialized, returning default profile for relay %u", relay_id);
        profile = RelayProfile{ DEFAULT_SPRAY_DAY_S, DEFAULT_COOLDOWN_DAY_S, DEFAULT_SPRAY_NIGHT_S, DEFAULT_COOLDOWN_NIGHT_S };
        return false;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not open NVS namespace '%s' (err=0x%x). Using defaults for relay %u", NVS_NAMESPACE, err, relay_id);
        profile = RelayProfile{ DEFAULT_SPRAY_DAY_S, DEFAULT_COOLDOWN_DAY_S, DEFAULT_SPRAY_NIGHT_S, DEFAULT_COOLDOWN_NIGHT_S };
        return true;
    }

    char key_sd[16], key_cd[16], key_sn[16], key_cn[16];
    snprintf(key_sd, sizeof(key_sd), "sd_%u", relay_id);
    snprintf(key_cd, sizeof(key_cd), "cd_%u", relay_id);
    snprintf(key_sn, sizeof(key_sn), "sn_%u", relay_id);
    snprintf(key_cn, sizeof(key_cn), "cn_%u", relay_id);

    uint32_t sd = 0, cd = 0, sn = 0, cn = 0;
    esp_err_t e_sd = nvs_get_u32(handle, key_sd, &sd);
    esp_err_t e_cd = nvs_get_u32(handle, key_cd, &cd);
    esp_err_t e_sn = nvs_get_u32(handle, key_sn, &sn);
    esp_err_t e_cn = nvs_get_u32(handle, key_cn, &cn);
    nvs_close(handle);

    // Rule S1-NVS-03: Validate range BEFORE using NVS values
    profile.spray_day_s = resolveFieldValue(e_sd, sd, DEFAULT_SPRAY_DAY_S, isSprayValid, "spray_day_s", MIN_SPRAY_DURATION_S, MAX_SPRAY_DURATION_S, relay_id);
    profile.cooldown_day_s = resolveFieldValue(e_cd, cd, DEFAULT_COOLDOWN_DAY_S, isCooldownValid, "cooldown_day_s", MIN_COOLDOWN_DURATION_S, MAX_COOLDOWN_DURATION_S, relay_id);
    profile.spray_night_s = resolveFieldValue(e_sn, sn, DEFAULT_SPRAY_NIGHT_S, isSprayValid, "spray_night_s", MIN_SPRAY_DURATION_S, MAX_SPRAY_DURATION_S, relay_id);
    profile.cooldown_night_s = resolveFieldValue(e_cn, cn, DEFAULT_COOLDOWN_NIGHT_S, isCooldownValid, "cooldown_night_s", MIN_COOLDOWN_DURATION_S, MAX_COOLDOWN_DURATION_S, relay_id);

    return true;
}

bool NvsStorage::saveProfile(uint8_t relay_id, const RelayProfile &profile) {
    if (relay_id >= TOTAL_RELAYS) {
        ESP_LOGE(TAG, "Cannot saveProfile: invalid relay_id (%u)", relay_id);
        return false;
    }

    if (is_mock_) {
        return validateProfile(relay_id, profile);
    }

    if (!is_initialized_) {
        ESP_LOGE(TAG, "Cannot saveProfile: NvsStorage not initialized");
        return false;
    }

    if (!validateProfile(relay_id, profile)) {
        return false;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s' for writing: 0x%x", NVS_NAMESPACE, err);
        return false;
    }

    char key_sd[16], key_cd[16], key_sn[16], key_cn[16];
    snprintf(key_sd, sizeof(key_sd), "sd_%u", relay_id);
    snprintf(key_cd, sizeof(key_cd), "cd_%u", relay_id);
    snprintf(key_sn, sizeof(key_sn), "sn_%u", relay_id);
    snprintf(key_cn, sizeof(key_cn), "cn_%u", relay_id);

    esp_err_t e1 = nvs_set_u32(handle, key_sd, profile.spray_day_s);
    esp_err_t e2 = nvs_set_u32(handle, key_cd, profile.cooldown_day_s);
    esp_err_t e3 = nvs_set_u32(handle, key_sn, profile.spray_night_s);
    esp_err_t e4 = nvs_set_u32(handle, key_cn, profile.cooldown_night_s);

    if (e1 != ESP_OK || e2 != ESP_OK || e3 != ESP_OK || e4 != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write all keys to NVS for relay %u", relay_id);
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS changes for relay %u: 0x%x", relay_id, err);
        return false;
    }

    ESP_LOGI(TAG, "Successfully persisted profile for relay %u to NVS", relay_id);
    return true;
}


bool NvsStorage::loadAllProfiles(RelayProfile profiles[TOTAL_RELAYS]) {
    if (profiles == nullptr) {
        ESP_LOGE(TAG, "Null pointer passed to loadAllProfiles");
        return false;
    }

    bool all_success = true;
    for (uint8_t i = 0; i < TOTAL_RELAYS; ++i) {
        if (!loadProfile(i, profiles[i])) {
            all_success = false;
        }
    }
    return all_success;
}

bool NvsStorage::factoryReset() {
    if (is_mock_) {
        ESP_LOGI(TAG, "NVS factory reset executed in MOCK mode");
        return true;
    }

    if (!is_initialized_) {
        ESP_LOGE(TAG, "Cannot factoryReset: NVS storage is not initialized");
        return false;
    }

    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase NVS partition during factory reset: 0x%x", err);
        return false;
    }

    err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to re-initialize NVS after erase: 0x%x", err);
        return false;
    }

    ESP_LOGI(TAG, "NVS Storage factory reset completed successfully!");
    return true;
}
