#include "wifi_storage_manager.h"
#include "config.h"

#include <cstring>
#include <cstdio>

WifiStorageManager::WifiStorageManager(INvsBackend* backend, const char* name_space)
    : storage_(backend, name_space ? name_space : WIFI_NVS_NAMESPACE),
      is_initialized_(false),
      has_cached_blob_(false) {
}

WifiStorageManager::~WifiStorageManager() = default;

bool WifiStorageManager::begin() {
    is_initialized_ = storage_.begin();
    if (is_initialized_) {
        WifiConfigBlob loaded;
        if (loadConfig(loaded)) {
            cached_blob_ = loaded;
            has_cached_blob_ = true;
        } else {
            populateFallbackCredentials();
        }
    } else {
        populateFallbackCredentials();
    }
    return is_initialized_;
}

void WifiStorageManager::populateFallbackCredentials() {
    cached_blob_ = WifiConfigBlob{};
    if (WIFI_SSID[0] != '\0' && std::strcmp(WIFI_SSID, "CHANGE_ME") != 0) {
        WifiProfile& p = cached_blob_.profiles[0];
        std::strncpy(p.ssid, WIFI_SSID, sizeof(p.ssid) - 1);
        p.ssid[sizeof(p.ssid) - 1] = '\0';
        std::strncpy(p.password, WIFI_PASS, sizeof(p.password) - 1);
        p.password[sizeof(p.password) - 1] = '\0';
        p.priority = 0;
        p.last_rssi = -127;
        p.last_success_epoch = 0;
        cached_blob_.count = 1;
        cached_blob_.crc32 = wifi_crypto::computeBlobCrc(cached_blob_);
        has_cached_blob_ = true;
    }
}

bool WifiStorageManager::loadConfig(WifiConfigBlob& out_blob) {
    if (!is_initialized_) {
        if (has_cached_blob_) {
            out_blob = cached_blob_;
            return true;
        }
        return false;
    }

    WifiConfigBlob temp;
    size_t len = sizeof(temp);
    if (!storage_.getBlob(WIFI_NVS_BLOB_KEY, &temp, &len)) {
        return false;
    }

    if (len != sizeof(temp) || !wifi_crypto::verifyBlobCrc(temp)) {
        return false;
    }

    cached_blob_ = temp;
    has_cached_blob_ = true;
    out_blob = temp;
    return true;
}

bool WifiStorageManager::saveConfig(const WifiConfigBlob& blob) {
    WifiConfigBlob to_save = blob;
    to_save.magic = WIFI_BLOB_MAGIC;
    to_save.version = WIFI_BLOB_VERSION;
    to_save.crc32 = wifi_crypto::computeBlobCrc(to_save);

    // Dirty-check: if cached matches exactly, avoid unnecessary flash writes
    if (has_cached_blob_ && cached_blob_.crc32 == to_save.crc32 &&
        std::memcmp(&cached_blob_, &to_save, sizeof(WifiConfigBlob)) == 0) {
        return true;
    }

    if (!is_initialized_) {
        cached_blob_ = to_save;
        has_cached_blob_ = true;
        return false;
    }

    bool ok = storage_.setBlob(WIFI_NVS_BLOB_KEY, &to_save, sizeof(to_save));
    if (ok) {
        cached_blob_ = to_save;
        has_cached_blob_ = true;
    }
    return ok;
}

bool WifiStorageManager::addOrUpdateProfile(const char* ssid, const char* password, int8_t priority) {
    if (ssid == nullptr || ssid[0] == '\0') {
        return false;
    }

    WifiConfigBlob blob = cached_blob_;
    int existing_idx = -1;
    for (uint8_t i = 0; i < blob.count; ++i) {
        if (std::strcmp(blob.profiles[i].ssid, ssid) == 0) {
            existing_idx = static_cast<int>(i);
            break;
        }
    }

    WifiProfile target{};
    std::strncpy(target.ssid, ssid, sizeof(target.ssid) - 1);
    target.ssid[sizeof(target.ssid) - 1] = '\0';
    if (password != nullptr) {
        std::strncpy(target.password, password, sizeof(target.password) - 1);
        target.password[sizeof(target.password) - 1] = '\0';
    } else {
        target.password[0] = '\0';
    }
    target.priority = priority;
    target.last_rssi = -127;
    target.last_success_epoch = 0;

    if (existing_idx >= 0) {
        // Shift existing profiles down so the newly updated profile moves to index 0 (MRU)
        for (int i = existing_idx; i > 0; --i) {
            blob.profiles[i] = blob.profiles[i - 1];
        }
        blob.profiles[0] = target;
    } else {
        if (blob.count < MAX_SAVED_WIFI) {
            for (int i = static_cast<int>(blob.count); i > 0; --i) {
                blob.profiles[i] = blob.profiles[i - 1];
            }
            blob.profiles[0] = target;
            blob.count++;
        } else {
            // Evict lowest priority profile
            int lowest_idx = 0;
            int8_t lowest_prio = blob.profiles[0].priority;
            for (uint8_t i = 1; i < blob.count; ++i) {
                if (blob.profiles[i].priority < lowest_prio) {
                    lowest_prio = blob.profiles[i].priority;
                    lowest_idx = static_cast<int>(i);
                }
            }
            for (int i = lowest_idx; i > 0; --i) {
                blob.profiles[i] = blob.profiles[i - 1];
            }
            blob.profiles[0] = target;
        }
    }

    return saveConfig(blob);
}

bool WifiStorageManager::removeProfile(const char* ssid) {
    if (ssid == nullptr || ssid[0] == '\0' || cached_blob_.count == 0) {
        return false;
    }

    WifiConfigBlob blob = cached_blob_;
    int found_idx = -1;
    for (uint8_t i = 0; i < blob.count; ++i) {
        if (std::strcmp(blob.profiles[i].ssid, ssid) == 0) {
            found_idx = static_cast<int>(i);
            break;
        }
    }

    if (found_idx < 0) {
        return false;
    }

    for (uint8_t i = found_idx; i + 1 < blob.count; ++i) {
        blob.profiles[i] = blob.profiles[i + 1];
    }
    std::memset(&blob.profiles[blob.count - 1], 0, sizeof(WifiProfile));
    blob.count--;

    return saveConfig(blob);
}

bool WifiStorageManager::clearAllProfiles() {
    WifiConfigBlob empty_blob{};
    empty_blob.count = 0;
    return saveConfig(empty_blob);
}

bool WifiStorageManager::isProvisioned() const {
    return cached_blob_.count > 0 && cached_blob_.profiles[0].ssid[0] != '\0';
}
