#pragma once

#include <string>
#include "esp_event.h"

class WiFiManager
{
public:
    void init(const std::string &ssid, const std::string &password);

private:
    static void eventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
};