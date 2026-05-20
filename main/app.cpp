#include "app.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_rom_uart.h"
#include "esp_wifi.h"
#include "ili9341.h"
#include "sdkconfig.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_ping.h"       // ← added for WiFi keep-alive
#include "ping/ping_sock.h" // ← added for WiFi keep-alive
#include "lwip/inet.h"      // ← added for WiFi keep-alive
#include "lwip/ip4_addr.h"  // ← added for WiFi keep-alive
#include <algorithm>        // For std::sort
#include "WiFiManager.h"

static const char *TAG = "APP";

// Configuration
#define SLEEP_TIMEOUT_MS 60000
#define QUEUE_SIZE 8
#define WAKE_GPIO GPIO_NUM_17
#define BACKLIGHT_GPIO GPIO_NUM_45
#define LCD_SLEEP_DELAY_MS 120

// WiFi keep-alive: ping the gateway every 30 s during light sleep.
// Must be shorter than the AP's idle-client timeout (usually 60–300 s).
#define WIFI_PING_INTERVAL_US (60ULL * 1000000ULL) // 30 seconds
#define WIFI_MAX_PING_FAILS 3                      // force reconnect after N misses

RTC_DATA_ATTR bool woke_from_touch = false;

// Ping-fail counter — survives light sleep, resets on deep-sleep reboot
RTC_DATA_ATTR int wifi_ping_fails = 0;

// ============================================================
// Power state machine
// ============================================================

enum class PowerState
{
    ACTIVE,
    SLEEP
};

static PowerState power_state = PowerState::ACTIVE;

// ============================================================
// Forward declaration for ISR
// ============================================================

static void IRAM_ATTR wakeGPIO_ISR(void *arg)
{
    Application *app = reinterpret_cast<Application *>(arg);
    app->wakeScreenFromISR();
}

// ============================================================
// Initialization
// ============================================================

void Application::initNVS()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "Erasing truncated NVS...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
}

void Application::initQueues()
{
    barcode_queue = xQueueCreate(QUEUE_SIZE, MAX_BARCODE_LEN);
    product_queue = xQueueCreate(QUEUE_SIZE, MAX_BARCODE_LEN);

    if (!barcode_queue || !product_queue)
    {
        ESP_LOGE(TAG, "Queue creation failed");
        abort();
    }
    ESP_LOGI(TAG, "Queues created");
}

void Application::initTasks()
{
    barcode_reader = new BarcodeReader(barcode_queue);
    ESP_ERROR_CHECK(barcode_reader->init());

    product_fetcher = new ProductFetcher(
        barcode_queue,
        product_queue,
        &product_cache);
    ESP_ERROR_CHECK(product_fetcher->start());

    ESP_LOGI(TAG, "Tasks started");
}

void Application::initHardware()
{
    // Backlight
    gpio_set_direction(BACKLIGHT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BACKLIGHT_GPIO, 1);

    // WAKE_GPIO for touch
    gpio_set_direction(WAKE_GPIO, GPIO_MODE_INPUT);
    gpio_set_intr_type(WAKE_GPIO, GPIO_INTR_NEGEDGE); // Wake on touch (falling edge)
    gpio_install_isr_service(0);
    gpio_isr_handler_add(WAKE_GPIO, wakeGPIO_ISR, this);

    // LVGL
    lvgl_manager.init();

    // WiFi
    wifi_manager.init(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);

    ESP_LOGI(TAG, "Hardware initialized");
}

// ============================================================
// WiFi keep-alive (silent — no peripherals touched)
// ============================================================

// Ping the default gateway. Returns true on success.
static bool pingGateway()
{
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK || ip_info.gw.addr == 0)
    {
        ESP_LOGW(TAG, "[Ping] No gateway available");
        return false;
    }

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr.u_addr.ip4.addr = ip_info.gw.addr;
    cfg.target_addr.type = IPADDR_TYPE_V4;
    cfg.count = 3;
    cfg.interval_ms = 200;
    cfg.timeout_ms = 1000;
    cfg.task_stack_size = 2048;
    cfg.task_prio = 2;

    SemaphoreHandle_t done = xSemaphoreCreateBinary();

    esp_ping_callbacks_t cbs = {};
    cbs.cb_args = &done;
    cbs.on_ping_end = [](esp_ping_handle_t hdl, void *arg)
    {
        xSemaphoreGive(*reinterpret_cast<SemaphoreHandle_t *>(arg));
    };

    esp_ping_handle_t ping;
    if (esp_ping_new_session(&cfg, &cbs, &ping) != ESP_OK)
    {
        ESP_LOGE(TAG, "[Ping] Failed to create session");
        vSemaphoreDelete(done);
        return false;
    }

    esp_ping_start(ping);
    xSemaphoreTake(done, pdMS_TO_TICKS(cfg.count * (cfg.interval_ms + cfg.timeout_ms) + 500));
    esp_ping_stop(ping);

    uint32_t received = 0;
    esp_ping_get_profile(ping, ESP_PING_PROF_REPLY, &received, sizeof(received));
    bool success = (received > 0);

    char gw_str[16];
    esp_ip4addr_ntoa((const esp_ip4_addr_t *)&ip_info.gw.addr, gw_str, sizeof(gw_str));
    ESP_LOGI(TAG, "[Ping] Gateway %s -> %s (%lu/%d replies)",
             gw_str, success ? "OK" : "FAIL", received, cfg.count);

    esp_ping_delete_session(ping);
    vSemaphoreDelete(done);
    return success;
}

// Silent WiFi maintenance — called on TIMER wakeups only.
// No screen, no backlight, no barcode reader is touched here.
// Reconnection uses esp_wifi_connect() directly — WiFiManager's event handler
// already auto-reconnects on WIFI_EVENT_STA_DISCONNECTED, so we just nudge it.
static void silentWifiPing()
{
    ESP_LOGI(TAG, "[WiFi-KeepAlive] Silent ping cycle");

    if (!wifi_manager.isConnected())
    {
        ESP_LOGW(TAG, "[WiFi-KeepAlive] Disconnected — reconnecting silently");
        esp_wifi_connect(); // event handler will set wifi_connected when IP is obtained
        vTaskDelay(pdMS_TO_TICKS(3000));
    }

    if (pingGateway())
    {
        wifi_ping_fails = 0;
    }
    else
    {
        wifi_ping_fails++;
        ESP_LOGW(TAG, "[WiFi-KeepAlive] Fail streak: %d / %d", wifi_ping_fails, WIFI_MAX_PING_FAILS);

        if (wifi_ping_fails >= WIFI_MAX_PING_FAILS)
        {
            ESP_LOGW(TAG, "[WiFi-KeepAlive] Force reconnect after %d failures", wifi_ping_fails);
            esp_wifi_disconnect(); // triggers WIFI_EVENT_STA_DISCONNECTED → auto-reconnect
            vTaskDelay(pdMS_TO_TICKS(3000));
            wifi_ping_fails = 0;
        }
    }
}

// ============================================================
// Sleep / Wake
// ============================================================

void Application::enterSleep()
{
    if (power_state != PowerState::ACTIVE)
        return;

    ESP_LOGI(TAG, "Entering SLEEP...");
    power_state = PowerState::SLEEP;
    screen_sleeping = true;

    // Stop LVGL timers
    lv_timer_enable(false);

    // Blank screen
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_refr_now(NULL);

    gpio_set_level(BACKLIGHT_GPIO, 0);
    ili9341_sleep_in();
    vTaskDelay(pdMS_TO_TICKS(LCD_SLEEP_DELAY_MS));

    if (barcode_reader)
        barcode_reader->off();

    // ── WiFi keep-alive light-sleep loop ─────────────────────────────────────
    // Two wakeup sources compete on every sleep iteration:
    //   • TIMER → silent WiFi ping, no peripherals touched, loop back to sleep
    //   • EXT1  → real touch event, break out for full wake
    // The loop lives here so the main loop never sees timer wakeups at all.
    // ─────────────────────────────────────────────────────────────────────────
    ESP_ERROR_CHECK(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL));

    // ── Keep RTC_PERIPH powered during light sleep so the UART
    //    maintains its register state (baud, pin mux, config, …).
    //    Without this, every wake requires a full UART re-init which
    //    is unreliable on ESP32-S3.                                       ──
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    while (true)
    {
        // ── UART: ensure all pending output is sent before CPU stops ─────
        uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(50));

        // Arm both wakeup sources before every sleep iteration
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(WIFI_PING_INTERVAL_US));
        ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup(1ULL << WAKE_GPIO, ESP_EXT1_WAKEUP_ANY_LOW)); // wake on low

        ESP_LOGI(TAG, "[Sleep] Light sleep — timer %llus or touch",
                 WIFI_PING_INTERVAL_US / 1000000ULL);

        // Enter light sleep; CPU stops here until wake
        esp_light_sleep_start();

        // ── UART recovery (belt-and-braces — the pd_config above should
        //    keep UART alive, but on some S3 revisions the registers can
        //    still glitch).                                               ──
        uart_flush_input(UART_NUM_0);

        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

        if (cause == ESP_SLEEP_WAKEUP_TIMER)
        {
            // Timer wakeup: ping WiFi silently, peripherals stay off, go back to sleep
            ESP_LOGI(TAG, "[Sleep] Timer wakeup — pinging WiFi (peripherals stay OFF)");
            silentWifiPing();
            continue;
        }

        // EXT1 (touch) or any other cause → exit loop for full peripheral wake
        ESP_LOGI(TAG, "[Sleep] Non-timer wakeup (cause=%d) — exiting sleep loop", cause);
        break;
    }

    ESP_LOGI(TAG, "Woke from SLEEP loop");
}

// Called from ISR — sets a flag, safe for ISR
void Application::wakeScreenFromISR()
{
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    wake_flag = true; // Set a flag to wake in main loop
    vTaskNotifyGiveFromISR(mainTaskHandle, &higherPriorityTaskWoken);
    if (higherPriorityTaskWoken)
        portYIELD_FROM_ISR();
}

// Wake screen (called from main task)
void Application::wakeScreen()
{
    if (power_state == PowerState::ACTIVE)
        return;

    // Record start time
    int64_t start_time = esp_timer_get_time();

    ESP_LOGI(TAG, "Waking screen...");

    // LCD wake
    ili9341_sleep_out();
    vTaskDelay(pdMS_TO_TICKS(LCD_SLEEP_DELAY_MS));

    // Backlight on
    gpio_set_level(BACKLIGHT_GPIO, 1);

    // Restart barcode
    if (barcode_reader)
        barcode_reader->on();

    // Resume LVGL timers
    lv_timer_enable(true);

    // Reset inactivity
    lv_disp_trig_activity(NULL);

    power_state = PowerState::ACTIVE;
    screen_sleeping = false;

    // Record end time and calculate elapsed time
    int64_t end_time = esp_timer_get_time();
    int elapsed_time_ms = (end_time - start_time) / 1000; // Convert microseconds to milliseconds

    // Display elapsed time using updateStatusLabel
    LVGLManager::updateStatusLabel("Hello!");

    ESP_LOGI(TAG, "Screen awake (ACTIVE)");

    LVGLManager::hideExpiredPanel();

    // ── WiFi: after light sleep the TCP/IP stack may be stale even
    //    though WiFi association is maintained. Force a clean reconnect
    //    so HTTP/TLS connections get a fresh TCP state.               ──
    if (wifi_manager.isConnected())
    {
        ESP_LOGI(TAG, "Refreshing WiFi for clean TCP/IP state");
        //  esp_wifi_disconnect(); // triggers WIFI_EVENT_STA_DISCONNECTED → auto-reconnect
        // int timeout = 60;
        // while (!wifi_manager.isConnected() && timeout-- > 0)
        // {
        //     vTaskDelay(pdMS_TO_TICKS(500));
        // }
        // if (wifi_manager.isConnected())
        //     ESP_LOGI(TAG, "WiFi refreshed OK");
        // else
        //     ESP_LOGE(TAG, "WiFi refresh timed out — will retry in task");
    }

    // Signal the persistent fetch task to re-fetch products
    // Only trigger if previous fetch completed (avoids stacking notifications)
    if (fetchTaskHandle != NULL && expiring_products_fetched_)
    {
        expiring_products_fetched_ = false;
        xTaskNotifyGive(fetchTaskHandle);
    }
}

// ============================================================
// Deep Sleep Configuration
// ============================================================

static bool isNightTime()
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    int hour = timeinfo.tm_hour;
    return (hour >= 23 || hour < 8); // Between 11 PM and 8 AM
}

static bool isWakeTime()
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    int hour = timeinfo.tm_hour;
    return (hour >= 8 && hour < 23); // Between 8 AM and 11 PM
}

void Application::enterDeepSleep()
{
    ESP_LOGI(TAG, "Entering DEEP_SLEEP...");

    // Configure wake source: WAKE_GPIO (RTC-capable) low level
    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup(1ULL << WAKE_GPIO, ESP_EXT1_WAKEUP_ANY_LOW)); // wake on low

    // Configure timer wakeup for 8 AM
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_hour >= 23 || timeinfo.tm_hour < 8)
    {
        struct tm wake_time = timeinfo;
        wake_time.tm_hour = 8;
        wake_time.tm_min = 0;
        wake_time.tm_sec = 0;

        if (timeinfo.tm_hour >= 23)
        {
            wake_time.tm_mday += 1; // Set to next day
        }

        time_t wake_timestamp = mktime(&wake_time);
        int64_t wake_delay_us = (wake_timestamp - now) * 1000000LL;

        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(wake_delay_us));
    }

    // Enter deep sleep
    esp_deep_sleep_start();
}

// ============================================================
// Main Loop
// ============================================================

void Application::mainLoop()
{
    mainTaskHandle = xTaskGetCurrentTaskHandle();

    while (true)
    {
        // Check if it's night time and enter deep sleep
        // Only enter deep sleep if:
        // - It is night time
        // - AND we did NOT wake from touch
        if (isNightTime() && !woke_from_touch)
        {
            enterDeepSleep();
        }

        if (isWakeTime())
        {
            woke_from_touch = false;
        }

        // Barcode activity → wake
        if (uxQueueMessagesWaiting(barcode_queue) > 0)
        {
            wakeScreen();
        }

        // Wake on touch ISR flag
        if (wake_flag)
        {
            wake_flag = false;
            wakeScreen();
        }

        // Inactivity → SLEEP (only if expiring products have been fetched)
        if (power_state == PowerState::ACTIVE && expiring_products_fetched_)
        {
            uint32_t inactive = lv_disp_get_inactive_time(NULL);
            if (inactive > SLEEP_TIMEOUT_MS)
            {
                enterSleep();
                wakeScreen();
            }
        }

        // ACTIVE → normal LVGL tick + light wait
        if (power_state == PowerState::ACTIVE)
        {
            lvgl_manager.tick();
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5)); // Wait for ISR or timeout
        }

        // SLEEP state:
        // - CPU has just resumed from esp_light_sleep_start()
        // - Fully wake screen after CPU wake
        if (power_state == PowerState::SLEEP)
        {
            wakeScreen();
        }
    }
}

// ============================================================
// Run
// ============================================================

void Application::run()
{
    ESP_LOGI(TAG, "Booting application...");

    initNVS();
    ESP_ERROR_CHECK(product_cache.init());
    initHardware();
    initQueues();
    initTasks();

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_EXT1)
    {
        ESP_LOGI(TAG, "Woke from deep sleep due to touch");
        woke_from_touch = true;
    }

    screen_sleeping = false;
    wake_flag = false;
    power_state = PowerState::ACTIVE;

    // Persistent fetch task — loops forever, wakes on notification
    // Using a larger stack since mbedTLS/TLS needs significant heap for SSL context
    expiring_products_fetched_ = false;
    xTaskCreate(Application::fetchExpiringProductsAndUpdateCacheTask, "FetchExpiringProducts", 10192, this, 5, &fetchTaskHandle);
    // Trigger the first fetch immediately
    if (fetchTaskHandle != NULL)
        xTaskNotifyGive(fetchTaskHandle);

    mainLoop();
}

void Application::fetchExpiringProducts()
{
    ESP_LOGI(TAG, "Fetching expiring products from Appwrite...");

    LVGLManager::updateStatusLabel("Fetching expiring products...");

    int result = 0;
    // Fetch products expiring today or tomorrow
    auto products = productService.getExpiringProducts(result);
    if (result < 0)
    {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "Failed to fetch expiring products\n\n"
                 "Free heap: %lu bytes\nDMA heap: %lu bytes\n\n"
                 "The device may need a restart if this persists.",
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_DMA));
        ESP_LOGE(TAG, "%s", buf);
        LVGLManager::updateStatusLabel("Failed to fetch expiring products");
        LVGLManager::showErrorPanel(buf);
        return;
    }
    if (products.empty())
    {
        ESP_LOGI(TAG, "No products expiring soon");
        LVGLManager::updateStatusLabel("No products expiring soon");
        return;
    }

    // Sort products by expiry date (oldest first)
    std::sort(products.begin(), products.end(), [](const auto &a, const auto &b)
              { return a.expiry < b.expiry; });

    // Get today's and tomorrow's dates
    time_t now;
    time(&now);
    struct tm today_tm;
    localtime_r(&now, &today_tm);

    // Get today's and tomorrow's dates — use YYYY-MM-DD to match expiry_date
    char today_str[12], tomorrow_str[12];
    strftime(today_str, sizeof(today_str), "%Y-%m-%d", &today_tm); // "2026-03-09"

    today_tm.tm_mday += 1;
    mktime(&today_tm);
    strftime(tomorrow_str, sizeof(tomorrow_str), "%Y-%m-%d", &today_tm); // "2026-03-10"

    // Build the text for the expired_lbl
    std::string expiredProductsText = "";
    for (const auto &product : products)
    {
        std::string expiry_date = product.expiry.substr(0, 10); // Extract date part only

        // Replace expiry date with "Today" or "Tomorrow" if applicable
        std::string formatted_date;
        if (expiry_date == today_str)
        {
            formatted_date = "Today";
        }
        else if (expiry_date == tomorrow_str)
        {
            formatted_date = "Tomorrow";
        }
        else
        {
            struct tm expiry_tm = {};
            // ✅ Parse the full date format that expiry_date actually contains
            strptime(expiry_date.c_str(), "%Y-%m-%d", &expiry_tm);

            char formatted_date_buf[12];
            strftime(formatted_date_buf, sizeof(formatted_date_buf), "%d-%b", &expiry_tm); // "10-Mar"
            formatted_date = formatted_date_buf;
        }

        expiredProductsText += product.name + " - " + formatted_date + "\n";
        ESP_LOGI(TAG, "Expiring Product: %s, Expiry: %s",
                 product.name.c_str(), formatted_date.c_str());
    }

    // Use LVGLManager to update the label
    LVGLManager::updateExpiredProductsLabel(expiredProductsText);
    LVGLManager::updateStatusLabel("Expiring products updated");
}

// Persistent task — loops forever, triggered by notification from wakeScreen()
void Application::fetchExpiringProductsAndUpdateCacheTask(void *param)
{
    Application *self = (Application *)param;

    while (true)
    {
        // Wait for signal from wakeScreen() or the initial trigger from run()
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Wait for WiFi with a hard timeout
        int wifi_timeout = 20;
        while (!wifi_manager.isConnected() && wifi_timeout-- > 0)
        {
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        // After light sleep, the lwIP TCP/IP thread needs time to
        // process internal timers and recover its state before any
        // connect() call.  Otherwise lwIP may abort the socket with
        // ECONNABORTED ("Software caused connection abort").
        vTaskDelay(pdMS_TO_TICKS(1500));

        if (wifi_manager.isConnected())
        {
            // Trigger a fresh SNTP sync attempt if we just woke up
            wifi_manager.startSNTP();

            int sntp_timeout = 20;
            while (!wifi_manager.isSntpSynced() && sntp_timeout-- > 0)
            {
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }

        // Even if SNTP fails, we proceed so the UI doesn't stay "stuck"
        self->fetchExpiringProducts();
        vTaskDelay(pdMS_TO_TICKS(500));
        self->updateProductsCache();

        self->expiring_products_fetched_ = true;
    }
}

void Application::updateProductsCache()
{
    // Fetch barcodes from Appwrite table
    auto barcodes = productService.getBarcodes();

    // Update product cache with fetched barcodes
    for (auto &product : barcodes)
    {
        ProductCacheItem cachedProduct;
        auto it = product_cache.get(product.barcode, cachedProduct);
        if (!it)
        {
            continue;
        }

        cachedProduct.name = product.name;
        cachedProduct.category = product.category;
        product_cache.add(cachedProduct);
        ESP_LOGI(TAG, "Updated product cache: Barcode=%s, Name=%s, Category=%s",
                 cachedProduct.barcode.c_str(), cachedProduct.name.c_str(), cachedProduct.category.c_str());
    }
}