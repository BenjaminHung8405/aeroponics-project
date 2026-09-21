#include "wifi_controller_task.h"

#include <cstring>
#include <cstdio>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <WiFi.h>
#include <esp_log.h>

namespace
{
    constexpr char TAG[] = "WIFI_CTRL";
    constexpr uint32_t CONNECT_TIMEOUT_MS = WIFI_CONNECT_ATTEMPT_TIMEOUT_MS;
    constexpr uint32_t ROAMING_CHECK_INTERVAL_MS = WIFI_ROAMING_CHECK_INTERVAL_MS;
    constexpr int8_t ROAMING_RSSI_THRESHOLD = WIFI_ROAMING_RSSI_THRESHOLD;
    constexpr int8_t ROAMING_HYSTERESIS_DBM = WIFI_ROAMING_HYSTERESIS_DBM;
    constexpr uint32_t RECURRENT_WARN_SUPPRESSION_MS = WIFI_RECURRENT_WARN_SUPPRESSION_MS;

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
            vTaskDelay(pdMS_TO_TICKS(WIFI_TASK_TICK_DELAY_MS));
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
        WIFI_TASK_NAME,
        WIFI_TASK_STACK_SIZE,
        this,
        WIFI_TASK_PRIORITY,
        nullptr,
        WIFI_TASK_CORE
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
            // RC-D Fix: When credentials are in NVS, connect directly — no scan needed.
            // WiFi.begin(ssid, pass) sends targeted probe requests to the saved SSID and
            // completes in 1–3 seconds. Scanning all 13 channels first is an architectural
            // antipattern that causes a deadlock: the Arduino ESP32 2.0.x WiFiScan layer
            // hard-codes _scanTimeout = max_ms_per_chan(300) * 20 = 6000ms, but the ESP32-S3
            // radio needs 6.5–8.5s in AP+STA mode. Result: scanComplete() always returns -2
            // (WIFI_SCAN_FAILED), handleScanningState() enters DISCONNECTED_WAIT, and
            // WiFi.begin() is never called. Scan is now reserved for Portal UI and roaming.
            const WifiConfigBlob &blob = storage_->cachedBlob();
            if (blob.count > 0)
            {
                connectDirectly(blob.profiles[0], now_ms);
            }
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
    // Small stabilization delay: lets the STA mode fully initialize before triggering
    // a scan. Without this, WiFi.scanNetworks() called immediately after mode() can
    // race against the internal driver event loop and return -2 (WIFI_SCAN_FAILED).
    // Note: setTxPower() was previously here as an inrush-current guard, but in
    // Arduino ESP32 2.0.x (IDF 4.4) it causes an internal radio reset that makes
    // every subsequent scan fail. The boot crash was fixed by RC-2 (Core separation)
    // and RC-3 (250ms boot yield); the TX power cap is no longer needed.
    vTaskDelay(pdMS_TO_TICKS(50));
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
        consecutive_no_match_scans_ = 0; // Reset counter on successful match
        ESP_LOGI(TAG, "Found saved network; attempting auto-reconnect...");
        connectToProfile(blob.profiles[best_profile_idx], now_ms);
    }
    else
    {
        consecutive_no_match_scans_++;
        if (scan_count > 0)
        {
            ESP_LOGW(TAG, "No matching saved Wi-Fi found (%d networks scanned, %u/max no-match scans); "
                          "retrying with backoff...",
                     scan_count, (unsigned)consecutive_no_match_scans_);
        }
        else
        {
            ESP_LOGW(TAG, "No networks found in scan; retrying...");
        }
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

void WifiControllerTask::connectDirectly(const WifiProfile &profile, uint32_t now_ms)
{
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    // RC-D Fix: Connect directly to a known AP without prior channel scan.
    // In ESP-IDF/Arduino, WiFi.begin(ssid, pass) issues a targeted Probe Request
    // only on the saved SSID and completes WPA2 association in 1–3 seconds under
    // normal conditions — much faster than a full 13-channel passive scan.
    WiFi.mode(WIFI_STA);
    vTaskDelay(pdMS_TO_TICKS(50)); // Let STA mode driver fully initialize
    ESP_LOGI(TAG, "Direct connect to AP '%s' (no scan; %u previous attempts).",
             profile.ssid, (unsigned)failed_reconnect_attempts_);
    WiFi.begin(profile.ssid, profile.password);
    std::strncpy(current_ssid_, profile.ssid, sizeof(current_ssid_) - 1);
    current_ssid_[sizeof(current_ssid_) - 1] = '\0';
#else
    (void)profile;
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
        // Mark first successful connection (used to gate first-connect-only logic).
        has_ever_connected_ = true;
        current_rssi_ = static_cast<int8_t>(WiFi.RSSI());
        ESP_LOGI(TAG, "Wi-Fi connected: IP=%s RSSI=%d dBm",
                 WiFi.localIP().toString().c_str(), current_rssi_);
        state_ = WifiEngineState::CONNECTED;
        last_state_change_ms_ = now_ms;
        backoff_duration_ms_ = 10000;   // Reset backoff on successful connection
        failed_reconnect_attempts_ = 0; // Reset failure counter on success
        consecutive_no_match_scans_ = 0;
        if (button_)
            button_->setLedState(LedState::CONNECTED_GREEN);
        return;
    }

    if (now_ms - connect_start_ms_ >= CONNECT_TIMEOUT_MS)
    {
        // RC-D Fix: Track attempts here so handleDisconnectedWait() can decide
        // whether to retry with connectDirectly() or fall back to a full scan.
        failed_reconnect_attempts_++;
        ESP_LOGW(TAG, "Connection to AP '%s' timed out (attempt %u/%u); retrying...",
                 current_ssid_, (unsigned)failed_reconnect_attempts_, (unsigned)MAX_FAILED_ATTEMPTS);
        WiFi.disconnect();
        // Short flat backoff for direct retry; exponential kicks in after MAX_FAILED_ATTEMPTS.
        backoff_duration_ms_ = 5000;
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
            ESP_LOGW(TAG, "Wi-Fi connection lost; attempting direct reconnect...");
        }
        if (button_)
            button_->setLedState(LedState::OFFLINE_RED);

        // RC-D Fix: Reconnect directly to the last known AP — no scan needed.
        // We already have valid credentials; re-scanning 13 channels first adds
        // 6+ seconds of dead time and hits the Arduino scan timeout trap (-2).
        if (storage_ && storage_->isProvisioned())
        {
            const WifiConfigBlob &blob = storage_->cachedBlob();
            if (blob.count > 0)
            {
                connectDirectly(blob.profiles[0], now_ms);
            }
            else
            {
                state_ = WifiEngineState::DISCONNECTED_WAIT;
                last_state_change_ms_ = now_ms;
                backoff_duration_ms_ = 5000;
            }
        }
        else
        {
            // No saved credentials; wait before opening portal
            state_ = WifiEngineState::DISCONNECTED_WAIT;
            last_state_change_ms_ = now_ms;
            backoff_duration_ms_ = 10000;
        }
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
        if (storage_ && storage_->isProvisioned())
        {
            // RC-D Fix: Graduated retry strategy.
            // Phase 1 (< MAX_FAILED_ATTEMPTS): Direct connect — fast, no scan, avoids 6s timeout trap.
            // Phase 2 (≥ MAX_FAILED_ATTEMPTS): Fall back to scan-then-connect to verify AP
            //   is on air and check if credentials need updating (triggers portal after MAX_NO_MATCH_SCANS).
            if (failed_reconnect_attempts_ < MAX_FAILED_ATTEMPTS)
            {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
                ESP_LOGI(TAG, "Direct reconnect attempt %u/%u (backoff %u ms).",
                         (unsigned)(failed_reconnect_attempts_ + 1),
                         (unsigned)MAX_FAILED_ATTEMPTS,
                         (unsigned)backoff_duration_ms_);
#endif
                const WifiConfigBlob &blob = storage_->cachedBlob();
                if (blob.count > 0)
                {
                    connectDirectly(blob.profiles[0], now_ms);
                    return;
                }
            }
            else
            {
                // Exhausted direct retries → scan to verify AP visibility.
                // If AP is simply not broadcasting (powered off etc.), consecutive_no_match_scans_
                // will accumulate and eventually open the portal for reconfiguration.
                if (consecutive_no_match_scans_ >= MAX_NO_MATCH_SCANS)
                {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
                    ESP_LOGW(TAG, "Cannot reach saved AP after %u direct attempts + %u scan misses; "
                                  "opening captive portal for reconfiguration...",
                             (unsigned)failed_reconnect_attempts_,
                             (unsigned)consecutive_no_match_scans_);
#endif
                    failed_reconnect_attempts_ = 0;
                    consecutive_no_match_scans_ = 0;
                    triggerPortalMode();
                    return;
                }
#if defined(ESP_PLATFORM) || defined(ARDUINO)
                ESP_LOGI(TAG, "Falling back to scan-then-connect after %u failed direct attempts.",
                         (unsigned)failed_reconnect_attempts_);
#endif
                // Increase exponential backoff only when entering scan phase
                if (backoff_duration_ms_ < 60000)
                    backoff_duration_ms_ += 10000;
                startAsyncScan(now_ms);
                return;
            }
        }

        // Increase exponential backoff for non-provisioned or no-blob case
        if (backoff_duration_ms_ < 60000)
            backoff_duration_ms_ += 10000;
        last_state_change_ms_ = now_ms;
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
        // RC-C Fix: Reset any latched long-press to prevent portal re-triggering.
        if (button_)
            button_->resetLongPress();
        // RC-D Fix: Reset all counters so IDLE will call connectDirectly() immediately.
        // Do NOT call startAsyncScan() here — that hits the 6s scan timeout trap.
        // The IDLE state picks up the new profile from storage on the next tick.
        failed_reconnect_attempts_ = 0;
        consecutive_no_match_scans_ = 0;
        backoff_duration_ms_ = 10000;
        state_ = WifiEngineState::IDLE;

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
