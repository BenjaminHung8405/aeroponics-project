#pragma once

#include <cstdint>
#include <cstddef>
#include "wifi_storage_manager.h"
#include "farmer_portal.h"
#include "hardware_button.h"

enum class WifiEngineState : uint8_t
{
    IDLE = 0,
    SCANNING,
    CONNECTING,
    CONNECTED,
    DISCONNECTED_WAIT,
    PORTAL_ACTIVE
};

class WifiControllerTask
{
public:
    WifiControllerTask();
    ~WifiControllerTask();

    bool begin(WifiStorageManager *storage, HardwareButton *button = nullptr);
    void startCore0Task();

    bool isConnected() const { return state_ == WifiEngineState::CONNECTED; }
    WifiEngineState getState() const { return state_; }
    int8_t getCurrentRssi() const { return current_rssi_; }
    const char *getCurrentSsid() const { return current_ssid_; }

    void triggerPortalMode();
    bool isPortalActive() const;

    // Called inside the FreeRTOS task or polled loop
    void processIteration(uint32_t now_ms);

private:
    WifiStorageManager *storage_ = nullptr;
    HardwareButton *button_ = nullptr;
    FarmerPortal portal_;

    WifiEngineState state_ = WifiEngineState::IDLE;
    char current_ssid_[WIFI_MAX_SSID_LEN + 1] = {};
    int8_t current_rssi_ = -127;

    uint32_t last_scan_request_ms_ = 0;
    uint32_t connect_start_ms_ = 0;
    uint32_t last_state_change_ms_ = 0;
    uint32_t backoff_duration_ms_ = 10000; // start with 10s backoff
    bool initial_auto_portal_checked_ = false;

    uint32_t last_scan_timeout_log_ms_ = UINT32_MAX;
    uint32_t last_scan_failure_log_ms_ = UINT32_MAX;
    uint32_t last_connect_timeout_log_ms_ = UINT32_MAX;
    uint32_t last_connection_lost_log_ms_ = UINT32_MAX;
    uint32_t last_roaming_log_ms_ = UINT32_MAX;

    void handleScanningState(uint32_t now_ms);
    void handleConnectingState(uint32_t now_ms);
    void handleConnectedState(uint32_t now_ms);
    void handleDisconnectedWait(uint32_t now_ms);
    void handlePortalState(uint32_t now_ms);

    void startAsyncScan(uint32_t now_ms);
    void connectToProfile(const WifiProfile &profile, uint32_t now_ms);
};
