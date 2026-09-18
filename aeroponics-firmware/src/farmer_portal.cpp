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
  body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background: #f0fdf4; color: #166534; margin: 0; padding: 20px; }
  .card { max-width: 420px; margin: 20px auto; background: #fff; padding: 24px; border-radius: 16px; box-shadow: 0 4px 12px rgba(0,0,0,0.1); }
  h2 { text-align: center; color: #15803d; margin-top: 0; font-size: 24px; }
  p { font-size: 15px; color: #374151; line-height: 1.4; text-align: center; }
  label { display: block; font-weight: bold; margin-top: 16px; margin-bottom: 6px; font-size: 16px; color: #1f2937; }
  input[type="text"], input[type="password"] { width: 100%; padding: 14px; font-size: 16px; border: 2px solid #bbf7d0; border-radius: 10px; box-sizing: border-box; outline: none; }
  input[type="text"]:focus, input[type="password"]:focus { border-color: #22c55e; }
  .checkbox-group { margin-top: 10px; display: flex; align-items: center; }
  .checkbox-group input { width: 20px; height: 20px; margin-right: 8px; }
  .btn { width: 100%; margin-top: 24px; padding: 16px; background: #16a34a; color: #fff; font-size: 18px; font-weight: bold; border: none; border-radius: 12px; cursor: pointer; box-shadow: 0 4px 6px rgba(22,163,74,0.3); }
  .btn:active { background: #15803d; }
  .footer { margin-top: 20px; text-align: center; font-size: 13px; color: #6b7280; }
</style>
</head>
<body>
<div class="card">
  <h2>🌱 HỆ THỐNG KHÍ CANH</h2>
  <p>Vui lòng chọn hoặc nhập tên mạng Wi-Fi tại vườn để kết nối thiết bị.</p>
  <form method="POST" action="/save">
    <label for="ssid">Tên Wi-Fi (SSID):</label>
    <input type="text" id="ssid" name="ssid" required placeholder="Nhập tên mạng Wi-Fi...">
    <label for="pass">Mật khẩu Wi-Fi:</label>
    <input type="password" id="pass" name="pass" placeholder="Nhập mật khẩu (nếu có)...">
    <div class="checkbox-group">
      <input type="checkbox" id="show" onclick="togglePass()">
      <label for="show" style="margin:0; font-weight:normal; cursor:pointer;">Hiện mật khẩu</label>
    </div>
    <button type="submit" class="btn">LƯU & KẾT NỐI NGAY</button>
  </form>
  <div class="footer">Cổng cài đặt sẽ tự đóng sau 5 phút để bảo vệ hệ thống.</div>
</div>
<script>
function togglePass() {
  var x = document.getElementById("pass");
  x.type = (x.type === "password") ? "text" : "password";
}
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

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    ESP_LOGI(TAG, "Starting Farmer Captive Portal on SoftAP: %s", PORTAL_AP_SSID);

    IPAddress ap_ip(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);

    WiFi.mode(WIFI_AP_STA);
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
#endif

    is_active_ = true;
    started_ms_ = 0; // will be initialized on first loop
    return true;
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

    if (now_ms - started_ms_ >= FARMER_PORTAL_TIMEOUT_MS) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
        ESP_LOGW(TAG, "Farmer Portal safety timeout (5 minutes) expired. Stopping SoftAP.");
#endif
        stop();
        return;
    }

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    dns_server.processNextRequest();
    web_server.handleClient();
#endif
}

void FarmerPortal::stop() {
    if (!is_active_) return;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    ESP_LOGI(TAG, "Stopping Farmer Captive Portal, closing Web and DNS servers...");
    web_server.stop();
    dns_server.stop();
    WiFi.softAPdisconnect(true);
#endif
    is_active_ = false;
}
