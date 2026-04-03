#pragma once

#include <string>
#include "esp_event.h"
#include "esp_sntp.h"

class WiFiManager
{
public:
    WiFiManager(); // Initialize flags here
    ~WiFiManager() = default;

    void init(const std::string &ssid, const std::string &password);
    bool isConnected();
    bool isSntpSynced();

    void startSNTP();

private:
    // This MUST be static to be used as a C callback
    static void eventHandler(void *arg,
                             esp_event_base_t event_base,
                             int32_t event_id,
                             void *event_data);

    // This MUST be static for the SNTP callback
    static void sntpSyncCallback(struct timeval *tv);

    // Use 'volatile' for variables changed in callbacks/ISRs
    volatile bool wifi_connected;
    volatile bool sntp_initialized;
};

extern WiFiManager wifi_manager; // Declare the global instance