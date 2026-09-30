#include "farmer_portal.h"

#include <cstdio>
#include <cstring>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_log.h>

namespace {
constexpr char TAG[] = "FARMER_PORTAL";
constexpr byte DNS_PORT = 53;

DNSServer dns_server;
WebServer web_server(80);

const char PORTAL_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Cài Đặt Wi-Fi Khí Canh</title>
<style>
  body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background: #f0fdf4; color: #166534; margin: 0; padding: 16px; }
  .card { max-width: 440px; margin: 10px auto; background: #fff; padding: 22px; border-radius: 16px; box-shadow: 0 4px 14px rgba(0,0,0,0.08); box-sizing: border-box; }
  h2 { text-align: center; color: #15803d; margin-top: 0; margin-bottom: 6px; font-size: 22px; }
  p { font-size: 14px; color: #4b5563; line-height: 1.4; text-align: center; margin-top: 0; margin-bottom: 16px; }
  .scan-bar { display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; margin-top: 4px; }
  .scan-title { font-weight: bold; font-size: 14px; color: #1f2937; }
  .btn-scan { background: #e0f2fe; color: #0369a1; border: 1px solid #bae6fd; padding: 6px 12px; border-radius: 8px; font-size: 13px; font-weight: 600; cursor: pointer; display: inline-flex; align-items: center; gap: 4px; }
  .btn-scan:active { background: #bae6fd; }
  .wifi-list { max-height: 175px; overflow-y: auto; border: 2px solid #e5e7eb; border-radius: 10px; margin-bottom: 16px; background: #f9fafb; -webkit-overflow-scrolling: touch; }
  .wifi-item { display: flex; justify-content: space-between; align-items: center; padding: 11px 14px; border-bottom: 1px solid #f3f4f6; cursor: pointer; transition: background 0.15s; }
  .wifi-item:active { background: #dcfce7; }
  .wifi-item:last-child { border-bottom: none; }
  .wifi-name { font-weight: 600; color: #1f2937; font-size: 14px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; max-width: 230px; }
  .wifi-meta { font-size: 13px; color: #6b7280; display: flex; align-items: center; gap: 6px; }
  .status-msg { padding: 14px; text-align: center; font-size: 13px; color: #6b7280; }
  label { display: block; font-weight: bold; margin-top: 14px; margin-bottom: 6px; font-size: 14px; color: #1f2937; }
  input[type="text"], input[type="password"] { width: 100%; padding: 12px 14px; font-size: 16px; border: 2px solid #bbf7d0; border-radius: 10px; box-sizing: border-box; outline: none; transition: border-color 0.2s; }
  input[type="text"]:focus, input[type="password"]:focus { border-color: #22c55e; }
  .checkbox-group { margin-top: 10px; display: flex; align-items: center; }
  .checkbox-group input { width: 20px; height: 20px; margin-right: 8px; cursor: pointer; }
  .btn { width: 100%; margin-top: 20px; padding: 15px; background: #16a34a; color: #fff; font-size: 17px; font-weight: bold; border: none; border-radius: 12px; cursor: pointer; box-shadow: 0 4px 6px rgba(22,163,74,0.25); }
  .btn:active { background: #15803d; }
  .footer { margin-top: 16px; text-align: center; font-size: 12px; color: #9ca3af; }
</style>
</head>
<body>
<div class="card">
  <h2>🌱 HỆ THỐNG KHÍ CANH</h2>
  <p>Chọn mạng từ danh sách quét hoặc nhập tên Wi-Fi tại vườn.</p>

  <div class="scan-bar">
    <span class="scan-title">Mạng khả dụng lân cận:</span>
    <button type="button" id="scanBtn" class="btn-scan" onclick="fetchScanResults()">🔄 Quét lại</button>
  </div>

  <div id="wifiList" class="wifi-list">
    <div class="status-msg">Đang dò tìm mạng Wi-Fi xung quanh...</div>
  </div>

  <form method="POST" action="/save" onsubmit="return handleFormSubmit()">
    <label for="ssid">Tên Wi-Fi (SSID):</label>
    <input type="text" id="ssid" name="ssid" required placeholder="Chạm vào danh sách hoặc nhập tên...">

    <label for="pass">Mật khẩu Wi-Fi:</label>
    <input type="password" id="pass" name="pass" placeholder="Nhập mật khẩu Wi-Fi...">

    <div class="checkbox-group">
      <input type="checkbox" id="show" onclick="togglePass()">
      <label for="show" style="margin:0; font-weight:normal; cursor:pointer;">Hiện mật khẩu</label>
    </div>

    <button type="submit" id="submitBtn" class="btn">LƯU & KẾT NỐI NGAY</button>
  </form>

  <div class="footer">Cổng cài đặt sẽ tự đóng sau 5 phút để bảo vệ hệ thống.</div>
</div>

<script>
function handleFormSubmit() {
  var ssid = document.getElementById("ssid").value.trim();
  if (!ssid) {
    alert("Vui lòng chọn hoặc nhập tên Wi-Fi!");
    return false;
  }
  var btn = document.getElementById("submitBtn");
  btn.disabled = true;
  btn.innerText = "⏳ Đang lưu & kết nối...";
  btn.style.backgroundColor = "#059669";
  btn.style.opacity = "0.8";
  return true;
}

function togglePass() {
  var x = document.getElementById("pass");
  x.type = (x.type === "password") ? "text" : "password";
}

function selectNetwork(name, isOpen) {
  var ssidInput = document.getElementById("ssid");
  var passInput = document.getElementById("pass");
  ssidInput.value = name;
  if (isOpen) {
    passInput.value = "";
    passInput.placeholder = "Mạng mở (không có mật khẩu)";
  } else {
    passInput.placeholder = "Nhập mật khẩu...";
    passInput.focus();
  }
}

function getSignalBadge(rssi) {
  if (rssi >= -65) return "🟢 📶";
  if (rssi >= -78) return "🟡 📶";
  return "🟠 📶";
}

function renderNetworks(list) {
  var container = document.getElementById("wifiList");
  if (!list || list.length === 0) {
    container.innerHTML = '<div class="status-msg">Không tìm thấy mạng khả dụng. Bạn hãy nhập tay tên mạng bên dưới.</div>';
    return;
  }

  var html = "";
  for (var i = 0; i < list.length; i++) {
    var net = list[i];
    var lockIcon = net.open ? "🌐" : "🔒";
    var signal = getSignalBadge(net.rssi);
    var safeName = net.ssid.replace(/"/g, '&quot;').replace(/'/g, '&#39;');
    var encName = encodeURIComponent(net.ssid);
    html += '<div class="wifi-item" onclick="selectNetwork(decodeURIComponent(\'' + encName + '\'), ' + (net.open ? "true" : "false") + ')">' +
            '  <span class="wifi-name">' + safeName + '</span>' +
            '  <span class="wifi-meta">' + lockIcon + ' ' + signal + '</span>' +
            '</div>';
  }
  container.innerHTML = html;
}

function fetchScanResults() {
  var btn = document.getElementById("scanBtn");
  var container = document.getElementById("wifiList");
  btn.disabled = true;
  btn.innerText = "⏳ Đang quét...";
  container.innerHTML = '<div class="status-msg">Đang dò tìm tín hiệu Wi-Fi...</div>';

  var retries = 0;
  function poll() {
    fetch("/scan")
      .then(function(res) { return res.json(); })
      .then(function(data) {
        if (data.status === "scanning") {
          retries++;
          if (retries < 20) {
            setTimeout(poll, 1000);
          } else {
            btn.disabled = false;
            btn.innerText = "🔄 Quét lại";
            container.innerHTML = '<div class="status-msg">Hết thời gian quét. Vui lòng thử lại hoặc nhập tên Wi-Fi thủ công.</div>';
          }
        } else if (Array.isArray(data)) {
          btn.disabled = false;
          btn.innerText = "🔄 Quét lại";
          renderNetworks(data);
        }
      })
      .catch(function(err) {
        // Tolerates transient packet drop during radio channel hopping
        retries++;
        if (retries < 20) {
          setTimeout(poll, 1200);
        } else {
          btn.disabled = false;
          btn.innerText = "🔄 Quét lại";
          container.innerHTML = '<div class="status-msg">Lỗi kết nối bộ quét. Hãy nhập tên Wi-Fi thủ công.</div>';
        }
      });
  }
  poll();
}

window.addEventListener("DOMContentLoaded", function() {
  fetchScanResults();
});
</script>
</body>
</html>
)rawhtml";

const char SUCCESS_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Đã Lưu Cài Đặt</title>
<style>
  body { font-family: sans-serif; background: #f0fdf4; color: #166534; padding: 30px; text-align: center; }
  .card { max-width: 400px; margin: 40px auto; background: #fff; padding: 30px; border-radius: 16px; box-shadow: 0 4px 12px rgba(0,0,0,0.1); }
  h2 { color: #16a34a; font-size: 26px; }
  p { font-size: 16px; color: #374151; line-height: 1.5; }
</style>
</head>
<body>
<div class="card">
  <h2>✅ ĐÃ LƯU THÀNH CÔNG!</h2>
  <p>Hệ thống đang lưu cấu hình Wi-Fi và tiến hành kết nối đến mạng của bạn.</p>
  <p>Bạn có thể đóng trang này. Đèn tín hiệu trên tủ điện sẽ chuyển sang <b>MÀU XANH</b> khi kết nối thành công.</p>
</div>
</body>
</html>
)rawhtml";

} // namespace
#endif

FarmerPortal::FarmerPortal() = default;
FarmerPortal::~FarmerPortal() {
    stop();
}

bool FarmerPortal::begin(WifiStorageManager* storage) {
    storage_ = storage;
    has_new_credentials_ = false;
    stop_pending_ = false;
    stop_requested_ms_ = 0;
    scan_state_ = PortalScanState::IDLE;
    last_scan_completed_ms_ = 0;
    std::snprintf(cached_scan_json_, sizeof(cached_scan_json_), "[]");

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    ESP_LOGI(TAG, "Starting Farmer Captive Portal on SoftAP: %s", PORTAL_AP_SSID);

    WiFi.scanDelete(); // Discard any background scan before setting AP mode

    IPAddress ap_ip(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);

    WiFi.mode(WIFI_AP_STA);
    WiFi.setTxPower(WIFI_POWER_15dBm);
    WiFi.softAPConfig(ap_ip, gateway, subnet);
    bool ap_ok = WiFi.softAP(PORTAL_AP_SSID);
    if (!ap_ok) {
        ESP_LOGE(TAG, "Failed to start SoftAP for Farmer Portal");
        return false;
    }

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, " SoftAP Started: %s (No Password / Open)", PORTAL_AP_SSID);
    ESP_LOGI(TAG, " Captive Portal URL: http://192.168.4.1");
    ESP_LOGI(TAG, "==================================================");

    dns_server.setErrorReplyCode(DNSReplyCode::NoError);
    dns_server.start(DNS_PORT, "*", ap_ip);

    registerWebRoutes();
    web_server.begin();

    // Defer initial scan until SoftAP is fully stabilized and client connects
    started_ms_ = millis();
#else
    started_ms_ = 0;
#endif

    is_active_ = true;
    return true;
}

void FarmerPortal::triggerScan(uint32_t now_ms) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (scan_state_ == PortalScanState::SCANNING) {
        return;
    }

    // Ensure SoftAP has stabilized before channel hopping
    if (started_ms_ > 0 && (now_ms - started_ms_ < PORTAL_AP_STABILIZE_DELAY_MS)) {
        ESP_LOGW(TAG, "SoftAP stabilizing, deferring Wi-Fi scan...");
        return;
    }

    // Clean up previous scan results before starting new scan
    WiFi.scanDelete();

    // 350ms/channel gives _scanTimeout = 350 * 20 = 7000ms. In AP+STA dual mode,
    // this completes 13 channels in ~5s without hitting premature timeout (-2),
    // and drastically reduces radio blackout for the connected client.
    int16_t status = WiFi.scanNetworks(true /* async */, false /* show_hidden */, false /* passive */, 350 /* max_ms_per_chan */);

    if (status == WIFI_SCAN_RUNNING || status >= 0) {
        scan_state_ = PortalScanState::SCANNING;
        scan_start_ms_ = now_ms;
        ESP_LOGI(TAG, "Triggered async Wi-Fi scan (status=%d)", status);
    } else {
        ESP_LOGW(TAG, "Wi-Fi scan failed to launch (status=%d)", status);
        scan_state_ = PortalScanState::IDLE;
    }
#else
    (void)now_ms;
#endif
}

void FarmerPortal::handleScanEndpoint() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    uint32_t now = millis();

    // Return cached results if still fresh
    if (scan_state_ == PortalScanState::READY && (now - last_scan_completed_ms_ < PORTAL_SCAN_CACHE_TTL_MS)) {
        web_server.send(200, "application/json", cached_scan_json_);
        return;
    }

    // Return scanning status if currently in progress
    if (scan_state_ == PortalScanState::SCANNING) {
        web_server.send(200, "application/json", "{\"status\":\"scanning\"}");
        return;
    }

    // Trigger new scan and return scanning status
    triggerScan(now);
    web_server.send(200, "application/json", "{\"status\":\"scanning\"}");
#endif
}

void FarmerPortal::updateScanEngine(uint32_t now_ms) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (scan_state_ != PortalScanState::SCANNING) {
        return;
    }

    int16_t scan_count = WiFi.scanComplete();
    if (scan_count == -1) {
        // Scanning in progress; check timeout
        if (now_ms - scan_start_ms_ >= PORTAL_SCAN_TIMEOUT_MS) {
            ESP_LOGW(TAG, "Portal async scan timed out (%u ms). Resetting to IDLE.", PORTAL_SCAN_TIMEOUT_MS);
            WiFi.scanDelete();
            scan_state_ = PortalScanState::IDLE;
        }
        return;
    }

    if (scan_count == -2) {
        ESP_LOGE(TAG, "Portal async scan encountered error (-2). Resetting to IDLE.");
        WiFi.scanDelete();
        scan_state_ = PortalScanState::IDLE;
        return;
    }

    // Scan complete (scan_count >= 0)
    ESP_LOGI(TAG, "Portal scan completed successfully. Raw BSSIDs found: %d", scan_count);
    size_t count = static_cast<size_t>(scan_count);
    if (count > MAX_RAW_SCAN_NETWORKS) count = MAX_RAW_SCAN_NETWORKS;

    std::memset(raw_networks_, 0, sizeof(raw_networks_));
    for (size_t i = 0; i < count; ++i) {
        String s = WiFi.SSID(i);
        std::strncpy(raw_networks_[i].ssid, s.c_str(), sizeof(raw_networks_[i].ssid) - 1);
        raw_networks_[i].ssid[sizeof(raw_networks_[i].ssid) - 1] = '\0';
        raw_networks_[i].rssi = static_cast<int8_t>(WiFi.RSSI(i));
        raw_networks_[i].is_open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    }

    std::memset(unique_networks_, 0, sizeof(unique_networks_));
    size_t unique_count = deduplicateAndSortScanResults(
        raw_networks_,
        count,
        unique_networks_,
        MAX_SCAN_NETWORKS
    );

    size_t written = serializeNetworksToJson(
        unique_networks_,
        unique_count,
        cached_scan_json_,
        sizeof(cached_scan_json_)
    );

    if (written == 0) {
        std::snprintf(cached_scan_json_, sizeof(cached_scan_json_), "[]");
    }

    WiFi.scanDelete();
    scan_state_ = PortalScanState::READY;
    last_scan_completed_ms_ = now_ms;
    ESP_LOGI(TAG, "Portal scan processed: %zu unique SSIDs ready in cache.", unique_count);
#else
    (void)now_ms;
#endif
}

void FarmerPortal::registerWebRoutes() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    if (routes_registered_) return;
    routes_registered_ = true;

    auto handle_root = []() {
        web_server.send(200, "text/html", PORTAL_HTML);
    };

    web_server.on("/", HTTP_GET, handle_root);
    web_server.on("/generate_204", HTTP_GET, handle_root);
    web_server.on("/hotspot-detect.html", HTTP_GET, handle_root);
    web_server.on("/ncsi.txt", HTTP_GET, handle_root);
    web_server.on("/canonical.html", HTTP_GET, handle_root);

    // API endpoint for async Wi-Fi network scanning
    web_server.on("/scan", HTTP_GET, [this]() {
        handleScanEndpoint();
    });

    web_server.on("/save", HTTP_POST, [this]() {
        if (!web_server.hasArg("ssid")) {
            web_server.send(400, "text/plain", "Thiếu tên SSID");
            return;
        }

        String ssid = web_server.arg("ssid");
        String pass = web_server.hasArg("pass") ? web_server.arg("pass") : "";

        ESP_LOGI(TAG, "Farmer submitted new Wi-Fi credentials for SSID: '%s'", ssid.c_str());
        if (storage_ != nullptr) {
            storage_->addOrUpdateProfile(ssid.c_str(), pass.c_str(), 10);
            has_new_credentials_ = true;
            stop_pending_ = true;
            stop_requested_ms_ = millis();
        }

        web_server.send(200, "text/html", SUCCESS_HTML);
    });

    web_server.onNotFound([]() {
        web_server.sendHeader("Location", "http://192.168.4.1/", true);
        web_server.send(302, "text/plain", "");
    });
#endif
}

void FarmerPortal::loop(uint32_t now_ms) {
    if (!is_active_) return;

    if (started_ms_ == 0) {
        started_ms_ = now_ms;
    }

    if (stop_pending_) {
        // Graceful delay: allow 1.5 seconds for the HTTP response to be completely
        // transmitted to the client's browser before shutting down the SoftAP radio.
        if (now_ms - stop_requested_ms_ >= 1500) {
            stop_pending_ = false;
            stop();
            return;
        }
    }

    if (now_ms - started_ms_ >= FARMER_PORTAL_TIMEOUT_MS) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        ESP_LOGW(TAG, "Farmer Portal safety timeout (5 minutes) expired. Stopping SoftAP.");
#endif
        stop();
        return;
    }

    // Auto-trigger background scan once SoftAP has stabilized (1s after start)
    // so scan results are ready in cache before the client even loads the webpage.
    if (scan_state_ == PortalScanState::IDLE && (now_ms - started_ms_ >= PORTAL_AP_STABILIZE_DELAY_MS) && last_scan_completed_ms_ == 0) {
        triggerScan(now_ms);
    }

    updateScanEngine(now_ms);

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    dns_server.processNextRequest();
    web_server.handleClient();
#endif
}

void FarmerPortal::stop() {
    if (!is_active_) return;
    stop_pending_ = false;
    stop_requested_ms_ = 0;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    ESP_LOGI(TAG, "Stopping Farmer Captive Portal, closing Web and DNS servers...");
    if (scan_state_ == PortalScanState::SCANNING) {
        WiFi.scanDelete();
    }
    scan_state_ = PortalScanState::IDLE;
    web_server.stop();
    dns_server.stop();
    WiFi.softAPdisconnect(true);
#endif
    is_active_ = false;
}
