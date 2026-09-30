#pragma once

#include <cstdint>
#include <cstddef>
#include "wifi_storage_manager.h"
#include "wifi_scan_helper.h"
#include "config.h"

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
    bool hasNewCredentials() const { return has_new_credentials_ && !stop_pending_; }
    void consumeNewCredentials() { has_new_credentials_ = false; }

    void triggerScan(uint32_t now_ms);
    PortalScanState getScanState() const { return scan_state_; }

private:
    WifiStorageManager *storage_ = nullptr;
    bool is_active_ = false;
    bool has_new_credentials_ = false;
    bool stop_pending_ = false;
    uint32_t stop_requested_ms_ = 0;
    bool routes_registered_ = false;
    uint32_t started_ms_ = 0;

    PortalScanState scan_state_ = PortalScanState::IDLE;
    uint32_t scan_start_ms_ = 0;
    uint32_t last_scan_completed_ms_ = 0;
    char cached_scan_json_[PORTAL_SCAN_JSON_BUFFER_SIZE] = "[]";

    DiscoveredNetwork raw_networks_[MAX_RAW_SCAN_NETWORKS] = {};
    DiscoveredNetwork unique_networks_[MAX_SCAN_NETWORKS] = {};

    void registerWebRoutes();
    void handleScanEndpoint();
    void updateScanEngine(uint32_t now_ms);
};
