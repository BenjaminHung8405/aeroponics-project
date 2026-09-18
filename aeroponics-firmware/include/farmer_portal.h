#pragma once

#include <cstdint>
#include <cstddef>
#include "wifi_storage_manager.h"

constexpr uint32_t FARMER_PORTAL_TIMEOUT_MS = 300000; // 5 minutes safety timeout
constexpr char PORTAL_AP_SSID[] = "KHI_CANH_CAI_DAT";

class FarmerPortal
{
public:
    FarmerPortal();
    ~FarmerPortal();

    bool begin(WifiStorageManager *storage);
    void loop(uint32_t now_ms);
    void stop();
    bool isActive() const { return is_active_; }
    bool hasNewCredentials() const { return has_new_credentials_; }

private:
    WifiStorageManager *storage_ = nullptr;
    bool is_active_ = false;
    bool has_new_credentials_ = false;
    bool routes_registered_ = false;
    uint32_t started_ms_ = 0;

    void registerWebRoutes();
};
