#pragma once

#include <cstdint>
#include <cstddef>
#include "wifi_storage_manager.h"
#include "wifi_scan_helper.h"

constexpr uint32_t FARMER_PORTAL_TIMEOUT_MS = 300000; // 5 minutes safety timeout
constexpr uint32_t PORTAL_SCAN_CACHE_TTL_MS = 20000;  // 20 seconds scan cache TTL
constexpr uint32_t PORTAL_SCAN_TIMEOUT_MS = 10000;    // 10 seconds async scan timeout
constexpr uint32_t PORTAL_AP_STABILIZE_DELAY_MS = 1000; // 1 second stabilization delay before scanning
constexpr char PORTAL_AP_SSID[] = "KHI_CANH_CAI_DAT";

enum class PortalScanState : uint8_t {
    IDLE = 0,
    SCANNING,
    READY
};

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

    void triggerScan(uint32_t now_ms);
    PortalScanState getScanState() const { return scan_state_; }

private:
    static constexpr size_t MAX_RAW_SCAN_NETWORKS = 32;

    WifiStorageManager *storage_ = nullptr;
    bool is_active_ = false;
    bool has_new_credentials_ = false;
    bool routes_registered_ = false;
    uint32_t started_ms_ = 0;

    PortalScanState scan_state_ = PortalScanState::IDLE;
    uint32_t scan_start_ms_ = 0;
    uint32_t last_scan_completed_ms_ = 0;
    char cached_scan_json_[2048] = "[]";

    DiscoveredNetwork raw_networks_[MAX_RAW_SCAN_NETWORKS] = {};
    DiscoveredNetwork unique_networks_[MAX_SCAN_NETWORKS] = {};

    void registerWebRoutes();
    void handleScanEndpoint();
    void updateScanEngine(uint32_t now_ms);
};
