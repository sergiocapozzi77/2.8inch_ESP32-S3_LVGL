#pragma once

#include <string>
#include "esp_event.h"

class WiFiManager
{
public:
    void init(const std::string &ssid, const std::string &password);
    static bool isConnected();
    void updateUI();

private:
    static volatile bool wifi_connected;
    static void eventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
};