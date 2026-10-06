// Station changes run on the app task. IDF callbacks only post link events.
#include "port.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "lwip/inet.h"

namespace hgp {
namespace {
uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
}

void Wifi::begin(NvsStorage& storage) {
  storage_ = &storage;
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  esp_netif_create_default_wifi_ap();
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  // Credentials remain in our NVS store until a phone setup succeeds.
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &Wifi::on_event, this));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &Wifi::on_event, this));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  wifi_country_t country = {};
  std::memcpy(country.cc, "IN", 2);
  country.schan = 1;
  country.nchan = 13;
  country.policy = WIFI_COUNTRY_POLICY_MANUAL;
  ESP_ERROR_CHECK(esp_wifi_set_country(&country));
  // Keep the radio fully awake.  This matches the known-good StickS3 port and
  // avoids AP-specific disconnects while the station is being brought up.
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
  ESP_ERROR_CHECK(esp_wifi_start());
}

bool Wifi::join(const char* ssid, const char* password) {
  configured_ = false;
  retry_at_ = 0;
  const esp_err_t disconnect_err = esp_wifi_disconnect();
  if (disconnect_err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(500));
  const size_t ssid_size = std::strlen(ssid), pass_size = std::strlen(password);
  ESP_LOGI("hg.wifi", "configuring station (%u-byte SSID, %u-byte credential)",
           static_cast<unsigned>(ssid_size), static_cast<unsigned>(pass_size));
  if (!ssid_size || ssid_size > 32 || pass_size > 64) {
    ESP_LOGE("hg.wifi", "saved Wi-Fi setting length is invalid");
    events::post(EventType::NetDown, "Wi-Fi not configured", 20);
    return false;
  }
  wifi_config_t cfg = {};
  std::memcpy(cfg.sta.ssid, ssid, ssid_size);
  std::memcpy(cfg.sta.password, password, pass_size);
  // A password does not imply WPA2 specifically: modern access points may be
  // WPA2/WPA3 transition or WPA3-only. Let the driver select any compatible
  // personal-network mode and use the password when the AP requests it.
  cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
  cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  cfg.sta.pmf_cfg.capable = true;

  wifi_scan_config_t scan = {};
  scan.show_hidden = true;
  scan.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  bool scan_completed = false;
  bool target_visible = false;
  const esp_err_t scan_err = esp_wifi_scan_start(&scan, true);
  if (scan_err == ESP_OK) {
    scan_completed = true;
    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    std::vector<wifi_ap_record_t> records(count);
    if (count && esp_wifi_scan_get_ap_records(&count, records.data()) == ESP_OK) {
      const wifi_ap_record_t* strongest = nullptr;
      for (const auto& record : records) {
        const size_t record_ssid_size = strnlen(reinterpret_cast<const char*>(record.ssid),
                                                sizeof(record.ssid));
        if (record_ssid_size == ssid_size && std::memcmp(record.ssid, ssid, ssid_size) == 0 &&
            (!strongest || record.rssi > strongest->rssi)) strongest = &record;
      }
      if (strongest) {
        target_visible = true;
        std::memcpy(cfg.sta.bssid, strongest->bssid, sizeof(cfg.sta.bssid));
        cfg.sta.bssid_set = true;
        cfg.sta.channel = strongest->primary;
        cfg.sta.scan_method = WIFI_FAST_SCAN;
        ESP_LOGI("hg.wifi", "configured network visible on channel %u at %d dBm", strongest->primary,
                 strongest->rssi);
      } else {
        ESP_LOGW("hg.wifi", "scan saw %u access points; configured network was absent", count);
      }
    } else {
      ESP_LOGW("hg.wifi", "scan saw no access points");
    }
  } else {
    ESP_LOGW("hg.wifi", "Wi-Fi scan failed: %s", esp_err_to_name(scan_err));
  }
  // Reconfiguration can race the asynchronous disconnect above.  Wait for the
  // driver to finish tearing down the old association instead of silently
  // abandoning the new credentials.
  esp_err_t err = ESP_OK;
  for (int attempt = 0; attempt < 30; ++attempt) {
    err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_ERR_WIFI_STATE) break;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  if (err != ESP_OK) {
    ESP_LOGE("hg.wifi", "set config failed: %s", esp_err_to_name(err));
    retry_at_ = now_ms() + 3000;
    return !scan_completed || target_visible;
  }
  configured_ = true;
  events::post(EventType::NetDown, "Joining Wi-Fi", 13);
  ESP_LOGI("hg.wifi", "starting station connection");
  const esp_err_t connect_err = esp_wifi_connect();
  if (connect_err != ESP_OK) {
    ESP_LOGW("hg.wifi", "connect start failed: %s", esp_err_to_name(connect_err));
    retry_at_ = now_ms() + 3000;
  }
  return !scan_completed || target_visible;
}

void Wifi::reconfigure() {
  joining_ = false;
  candidate_ = {};
  const std::string ssid = storage_->get("wifi_ssid").value_or(CONFIG_HG_DEFAULT_WIFI_SSID);
  const std::string pass = storage_->get("wifi_pass").value_or(CONFIG_HG_DEFAULT_WIFI_PASSWORD);
  const bool target_visible = join(ssid.c_str(), pass.c_str());
  auto_setup_ = (!target_visible || ssid.empty()) && !auto_setup_tried_;
}

void Wifi::disconnected() {
  wait_disconnect_ = false;
  if (configured_) retry_at_ = now_ms() + 3000;
}

void Wifi::connected(hg::App& app) {
  retry_at_ = 0;
  if (!joining_ || wait_disconnect_) return;
  wifi_ap_record_t ap = {};
  if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK ||
      std::strncmp(reinterpret_cast<const char*>(ap.ssid), candidate_.ssid, sizeof(ap.ssid)) != 0) return;
  storage_->set("wifi_ssid", candidate_.ssid);
  storage_->set("wifi_pass", candidate_.password);
  app.console(std::string("set server ") + candidate_.server);
  candidate_ = {};
  joining_ = false;
  setup_status("connected");
  close_at_ = now_ms() + 10000;
  esp_netif_ip_info_t station{};
  auto* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (sta && esp_netif_get_ip_info(sta, &station) == ESP_OK &&
      (station.ip.addr & station.netmask.addr) == (inet_addr("192.168.4.1") & station.netmask.addr))
    app.close_wifi_setup();
}

void Wifi::provision(const hg::WifiCredentials& credentials) {
  if (!http_ || joining_) return;
  candidate_ = credentials;
  wifi_ap_record_t ap = {};
  wait_disconnect_ = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
  joining_ = true;
  trial_at_ = now_ms();
  setup_status("joining");
  join(candidate_.ssid, candidate_.password);
}

void Wifi::tick(hg::App& app, uint32_t now) {
  if (auto_setup_) {
    auto_setup_ = false;
    auto_setup_tried_ = true;
    app.start_wifi_setup();
  }
  if (joining_ && now - trial_at_ >= 30000) {
    reconfigure();
    setup_status("failed");
    std::lock_guard<std::mutex> lock(setup_mutex_);
    accepting_setup_ = true;
  }
  if (http_ && (static_cast<int32_t>(now - setup_until_) >= 0 ||
                (close_at_ && static_cast<int32_t>(now - close_at_) >= 0))) app.close_wifi_setup();
  if (configured_ && retry_at_ && static_cast<int32_t>(now - retry_at_) >= 0) {
    retry_at_ = 0;
    if (esp_wifi_connect() != ESP_OK) retry_at_ = now + 3000;
  }
}

void Wifi::on_event(void*, const char* base, int32_t id, void* data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    // app_main starts the saved-network join after all drivers and the app are
    // ready. Doing it from this early callback can lose the one-shot event.
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    auto* info = static_cast<wifi_event_sta_disconnected_t*>(data);
    char detail[48];
    std::snprintf(detail, sizeof(detail), "Wi-Fi lost (reason %d)", info ? info->reason : 0);
    events::post(EventType::WifiDisconnected, detail, std::strlen(detail));
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const auto* got = static_cast<ip_event_got_ip_t*>(data);
    char detail[32];
    std::snprintf(detail, sizeof(detail), IPSTR, IP2STR(&got->ip_info.ip));
    ESP_LOGI("hg.wifi", "connected, ip %s", detail);
    events::post(EventType::NetUp, detail, std::strlen(detail));
  }
}

}  // namespace hgp
