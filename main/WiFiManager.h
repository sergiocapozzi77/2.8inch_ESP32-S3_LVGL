#pragma once

#include <string>
#include "esp_event.h"

class WiFiManager
{
public:
    WiFiManager() = default;
    ~WiFiManager() = default;

    void init(const std::string &ssid, const std::string &password);
    static bool isConnected();
    void waitForConnection();

    static bool isSntpSynced();

private:
    static volatile bool sntp_synced;
    static void sntpSyncCallback(struct timeval *tv);
    static void eventHandler(void *arg,
                             esp_event_base_t event_base,
                             int32_t event_id,
                             void *event_data);

    static void startSNTP();

    static volatile bool wifi_connected;
    static bool sntp_initialized;
};
