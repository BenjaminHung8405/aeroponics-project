#pragma once

#include "wifi_storage_types.h"
#include "nvs_storage.h"

constexpr char WIFI_NVS_NAMESPACE[] = "wifi_store";
constexpr char WIFI_NVS_BLOB_KEY[] = "wifi_blob";

class WifiStorageManager {
public:
    explicit WifiStorageManager(INvsBackend* backend = nullptr, const char* name_space = WIFI_NVS_NAMESPACE);
    virtual ~WifiStorageManager();

    bool begin();
    bool loadConfig(WifiConfigBlob& out_blob);
    bool saveConfig(const WifiConfigBlob& blob);
    bool addOrUpdateProfile(const char* ssid, const char* password, int8_t priority = 0);
    bool removeProfile(const char* ssid);
    bool clearAllProfiles();
    bool isProvisioned() const;

    const WifiConfigBlob& cachedBlob() const { return cached_blob_; }
    size_t profileCount() const { return cached_blob_.count; }

private:
    NvsStorage storage_;
    WifiConfigBlob cached_blob_;
    bool is_initialized_ = false;
    bool has_cached_blob_ = false;

    void populateFallbackCredentials();
};
