#include "wifi_controller_task.h"

#include <cstring>
#include <cstdio>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <WiFi.h>
#include <esp_log.h>

namespace
{
    constexpr char TAG[] = "WIFI_CTRL";
    constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;
    constexpr uint32_t ROAMING_CHECK_INTERVAL_MS = 60000;
    constexpr int8_t ROAMING_RSSI_THRESHOLD = -80; // dBm
    constexpr int8_t ROAMING_HYSTERESIS_DBM = 15;  // dBm
    constexpr uint32_t RECURRENT_WARN_SUPPRESSION_MS = 60000;

    bool shouldLogAgain(uint32_t &last_log_ms, uint32_t now_ms)
    {
        if (last_log_ms == UINT32_MAX)
        {
            last_log_ms = now_ms;
            return true;
        }
        if (now_ms - last_log_ms < RECURRENT_WARN_SUPPRESSION_MS)
        {
            return false;
        }
        last_log_ms = now_ms;
        return true;
    }

    void core0WifiTaskEntry(void *pvParameters)
    {
        auto *self = static_cast<WifiControllerTask *>(pvParameters);
        ESP_LOGI(TAG, "Core 0 Wi-Fi controller started (core %d)", xPortGetCoreID());
        for (;;)
        {
            uint32_t now = millis();
            self->processIteration(now);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}
#endif

WifiControllerTask::WifiControllerTask() = default;
WifiControllerTask::~WifiControllerTask() = default;

bool WifiControllerTask::begin(WifiStorageManager *storage, HardwareButton *button)
{
    storage_ = storage;
    button_ = button;
    state_ = WifiEngineState::IDLE;
    return true;
}

void WifiControllerTask::startCore0Task()
{
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    xTaskCreatePinnedToCore(
        core0WifiTaskEntry,
        "wifi_ctrl_task",
        8192,
        this,
        2, // Low Priority
        nullptr,
        0 // Core 0 (Protocol Core)
    );
#endif
}

void WifiControllerTask::triggerPortalMode()
{
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    WiFi.scanDelete();
#endif
    state_ = WifiEngineState::PORTAL_ACTIVE;
    portal_.begin(storage_);
    if (button_)
    {
        button_->setLedState(LedState::PORTAL_YELLOW_BLINK);
    }
}

bool WifiControllerTask::isPortalActive() const
{
    return state_ == WifiEngineState::PORTAL_ACTIVE && portal_.isActive();
}

void WifiControllerTask::processIteration(uint32_t now_ms)
{
    if (button_)
    {
        button_->update(now_ms);
        if (button_->isLongPressDetected() && state_ != WifiEngineState::PORTAL_ACTIVE)
        {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
            ESP_LOGW(TAG, "Hardware Button long press (3s) detected! Triggering Farmer Portal Mode.");
#endif
            button_->resetLongPress();
            triggerPortalMode();
            return;
        }
    }

    switch (state_)
    {
    case WifiEngineState::IDLE:
        if (storage_ && storage_->isProvisioned())
        {
            startAsyncScan(now_ms);
        }
        else if (!initial_auto_portal_checked_)
        {
            initial_auto_portal_checked_ = true;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
            ESP_LOGW(TAG, "No Wi-Fi credentials found in NVS! Auto-launching Farmer Portal (AP: %s)...", PORTAL_AP_SSID);
#endif
            triggerPortalMode();
        }
        else
        {
            if (button_)
                button_->setLedState(LedState::OFFLINE_RED);
        }
        break;

    case WifiEngineState::SCANNING:
        handleScanningState(now_ms);
        break;

    case WifiEngineState::CONNECTING:
        handleConnectingState(now_ms);
        break;

    case WifiEngineState::CONNECTED:
        handleConnectedState(now_ms);
        break;

    case WifiEngineState::DISCONNECTED_WAIT:
        handleDisconnectedWait(now_ms);
        break;

    case WifiEngineState::PORTAL_ACTIVE:
        handlePortalState(now_ms);
        break;
    }
}

void WifiControllerTask::startAsyncScan(uint32_t now_ms)
{
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    // Do not scan hidden networks during auto-connect scan to complete fast and avoid 6s timeout
    int16_t scan_status = WiFi.scanNetworks(true /* async */, false /* show_hidden */);
    ESP_LOGD(TAG, "Async Wi-Fi scan requested (status=%d)", scan_status);
#endif
    state_ = WifiEngineState::SCANNING;
    last_scan_request_ms_ = now_ms;
}

void WifiControllerTask::handleScanningState(uint32_t now_ms)
{
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    int16_t scan_count = WiFi.scanComplete();
    if (scan_count == -1)
    {
        // Still scanning, non-blocking check
        if (now_ms - last_scan_request_ms_ > 10000)
        {
            if (shouldLogAgain(last_scan_timeout_log_ms_, now_ms))
            {
                ESP_LOGW(TAG, "Async scan timed out after 10s; retrying with backoff.");
            }
            WiFi.scanDelete();
            state_ = WifiEngineState::DISCONNECTED_WAIT;
            last_state_change_ms_ = now_ms;
        }
        return;
    }

    if (scan_count == -2)
    {
        if (shouldLogAgain(last_scan_failure_log_ms_, now_ms))
        {
            ESP_LOGE(TAG, "Async scan failed; retrying in backoff.");
        }
        state_ = WifiEngineState::DISCONNECTED_WAIT;
        last_state_change_ms_ = now_ms;
        return;
    }

    ESP_LOGD(TAG, "Async scan completed (%d networks found).", scan_count);
    const WifiConfigBlob &blob = storage_->cachedBlob();

    int best_profile_idx = -1;
    int32_t best_score = -9999;

    for (int i = 0; i < scan_count; ++i)
    {
        String ssid = WiFi.SSID(i);
        int32_t rssi = WiFi.RSSI(i);

        for (uint8_t p = 0; p < blob.count; ++p)
        {
            if (ssid.equals(blob.profiles[p].ssid))
            {
                // Priority score: priority * 100 + RSSI
                int32_t score = (static_cast<int32_t>(blob.profiles[p].priority) * 100) + rssi;
                if (score > best_score)
                {
                    best_score = score;
                    best_profile_idx = p;
                }
            }
        }
    }

    WiFi.scanDelete();

    if (best_profile_idx >= 0)
    {
        connectToProfile(blob.profiles[best_profile_idx], now_ms);
    }
    else
    {
        ESP_LOGW(TAG, "No matching saved Wi-Fi found; entering backoff wait.");
        if (button_)
            button_->setLedState(LedState::OFFLINE_RED);
        state_ = WifiEngineState::DISCONNECTED_WAIT;
        last_state_change_ms_ = now_ms;
    }
#else
    (void)now_ms;
    state_ = WifiEngineState::IDLE;
#endif
}

void WifiControllerTask::connectToProfile(const WifiProfile &profile, uint32_t now_ms)
{
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    ESP_LOGI(TAG, "Connecting to AP '%s'", profile.ssid);
    WiFi.begin(profile.ssid, profile.password);
    std::strncpy(current_ssid_, profile.ssid, sizeof(current_ssid_) - 1);
    current_ssid_[sizeof(current_ssid_) - 1] = '\0';
#endif
    state_ = WifiEngineState::CONNECTING;
    connect_start_ms_ = now_ms;
    last_state_change_ms_ = now_ms;
}

void WifiControllerTask::handleConnectingState(uint32_t now_ms)
{
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (WiFi.status() == WL_CONNECTED)
    {
        current_rssi_ = static_cast<int8_t>(WiFi.RSSI());
        ESP_LOGI(TAG, "Wi-Fi connected: IP=%s RSSI=%d dBm",
                 WiFi.localIP().toString().c_str(), current_rssi_);
        state_ = WifiEngineState::CONNECTED;
        last_state_change_ms_ = now_ms;
        backoff_duration_ms_ = 10000; // Reset backoff
        if (button_)
            button_->setLedState(LedState::CONNECTED_GREEN);
        return;
    }

    if (now_ms - connect_start_ms_ >= CONNECT_TIMEOUT_MS)
    {
        if (shouldLogAgain(last_connect_timeout_log_ms_, now_ms))
        {
            ESP_LOGW(TAG, "Connection to AP '%s' timed out; entering backoff wait.", current_ssid_);
        }
        WiFi.disconnect();
        state_ = WifiEngineState::DISCONNECTED_WAIT;
        last_state_change_ms_ = now_ms;
        if (button_)
            button_->setLedState(LedState::OFFLINE_RED);
    }
#else
    (void)now_ms;
    state_ = WifiEngineState::CONNECTED;
#endif
}

void WifiControllerTask::handleConnectedState(uint32_t now_ms)
{
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (WiFi.status() != WL_CONNECTED)
    {
        if (shouldLogAgain(last_connection_lost_log_ms_, now_ms))
        {
            ESP_LOGW(TAG, "Wi-Fi connection lost; entering backoff wait.");
        }
        state_ = WifiEngineState::DISCONNECTED_WAIT;
        last_state_change_ms_ = now_ms;
        if (button_)
            button_->setLedState(LedState::OFFLINE_RED);
        return;
    }

    current_rssi_ = static_cast<int8_t>(WiFi.RSSI());

    // Roaming check: if RSSI drops below threshold, trigger an async scan to see if another AP is stronger
    if (now_ms - last_scan_request_ms_ >= ROAMING_CHECK_INTERVAL_MS && current_rssi_ < ROAMING_RSSI_THRESHOLD)
    {
        if (shouldLogAgain(last_roaming_log_ms_, now_ms))
        {
            ESP_LOGI(TAG, "Weak signal (%d dBm); starting roaming scan.", current_rssi_);
        }
        startAsyncScan(now_ms);
    }
#else
    (void)now_ms;
#endif
}

void WifiControllerTask::handleDisconnectedWait(uint32_t now_ms)
{
    if (now_ms - last_state_change_ms_ >= backoff_duration_ms_)
    {
        // Increase backoff up to 60s
        if (backoff_duration_ms_ < 60000)
        {
            backoff_duration_ms_ += 10000;
        }
        startAsyncScan(now_ms);
    }
}

void WifiControllerTask::handlePortalState(uint32_t now_ms)
{
    portal_.loop(now_ms);

    if (portal_.hasNewCredentials())
    {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        ESP_LOGI(TAG, "Portal captured new credentials; reconnecting.");
#endif
        portal_.stop();
        state_ = WifiEngineState::IDLE;
        startAsyncScan(now_ms);
        return;
    }

    if (!portal_.isActive())
    {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        ESP_LOGI(TAG, "Portal closed; resuming background connection engine.");
#endif
        if (storage_ && storage_->isProvisioned())
        {
            state_ = WifiEngineState::IDLE;
            startAsyncScan(now_ms);
        }
        else
        {
            state_ = WifiEngineState::DISCONNECTED_WAIT;
            last_state_change_ms_ = now_ms;
            backoff_duration_ms_ = 30000;
            if (button_)
                button_->setLedState(LedState::OFFLINE_RED);
        }
    }
}
