#include "WiFiManager.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char *TAG = "WiFiManager";

WiFiManager wifi_manager; // Define the global instance

WiFiManager::WiFiManager()
    : wifi_connected(false),
      sntp_initialized(false)
{
    // Constructor body can be empty
}
// ============================================================
// Public API
// ============================================================

void WiFiManager::init(const std::string &ssid,
                       const std::string &password)
{
    // --- Network stack ---
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // --- WiFi init ---
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Set UK country (channels 1–13)
    wifi_country_t country = {
        .cc = "GB",
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_AUTO};
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));

    // Register events
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &WiFiManager::eventHandler,
        this));

    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &WiFiManager::eventHandler,
        this));

    // --- WiFi configuration ---
    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password.c_str(), sizeof(wifi_config.sta.password) - 1);

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = false;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Disable power save for stability
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGI(TAG, "WiFi initialization complete");
}

bool WiFiManager::isConnected()
{
    return wifi_connected;
}

// ============================================================
// Event Handler
// ============================================================

void WiFiManager::eventHandler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    // 1. Cast the 'arg' back to a WiFiManager pointer
    WiFiManager *self = static_cast<WiFiManager *>(arg);

    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            // 2. Use 'self->' to access the member variable
            self->wifi_connected = false;

            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_wifi_connect();
            break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        // 3. Use 'self->' here as well
        self->wifi_connected = true;

        if (!self->sntp_initialized)
        {
            self->startSNTP();
            self->sntp_initialized = true;
        }
    }
}

// ============================================================
// SNTP
// ============================================================

// Update these methods in your WiFiManager.cpp

void WiFiManager::startSNTP()
{
    // If already running, stop it to ensure a fresh socket/query cycle
    if (esp_sntp_enabled())
    {
        esp_sntp_stop();
    }

    ESP_LOGI(TAG, "Initializing SNTP...");

    setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");

    // Set to IMMED so the clock jumps to the correct time immediately
    // instead of slowly drifting (slewing) to it.
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_set_time_sync_notification_cb(sntpSyncCallback);
    esp_sntp_init();

    sntp_initialized = true;
    ESP_LOGI(TAG, "SNTP started");
}

bool WiFiManager::isSntpSynced()
{
    if (!sntp_initialized)
        return false;

    // Check 1: The IDF Sync Status bit
    bool synced = (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED);

    // Check 2: Sanity check the year (must be > 2024)
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year < (2025 - 1900))
    {
        return false;
    }

    return synced;
}

void WiFiManager::sntpSyncCallback(struct timeval *tv)
{
    ESP_LOGI(TAG, "SNTP synced, time: %lld", (long long)tv->tv_sec);
}
