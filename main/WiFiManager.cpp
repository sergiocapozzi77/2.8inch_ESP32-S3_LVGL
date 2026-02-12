#include "esp_wifi.h"
#include "esp_event.h"
#include <string>
#include <cstring>
#include "esp_log.h"
#include "esp_sntp.h"

#include "WiFiManager.h"

static const char *TAG = "WiFiManager";

// Static member initialization
volatile bool WiFiManager::wifi_connected = false;

// ============================================================
// WiFiManager Implementation
// ============================================================
void WiFiManager::init(const std::string &ssid, const std::string &password)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WiFiManager::eventHandler, this));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &WiFiManager::eventHandler, this));

    wifi_config_t wifi_config = {};
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    strncpy((char *)wifi_config.sta.ssid, ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password.c_str(), sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi init complete");
}

void WiFiManager::eventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Connecting to WiFi...");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW(TAG, "WiFi disconnected. Reconnecting...");
        wifi_connected = false;
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;

        ESP_LOGI(TAG, "Setting timezone and initializing SNTP...");
        // 1. Set the timezone (e.g., for London/GMT)
        setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
        tzset();

        // 2. Initialize SNTP
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
        ESP_LOGI(TAG, "SNTP initialized");
    }
}

bool WiFiManager::isConnected()
{
    return wifi_connected;
}
