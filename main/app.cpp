#include "app.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "ili9341.h"
#include "sdkconfig.h"
#include "esp_sleep.h"

static const char *TAG = "APP";

// Configuration
#define SLEEP_TIMEOUT_MS 60000
#define QUEUE_SIZE 8
#define WAKE_GPIO GPIO_NUM_17
#define BACKLIGHT_GPIO GPIO_NUM_45
#define LCD_SLEEP_DELAY_MS 120

// ============================================================
// Power state machine
// ============================================================

enum class PowerState
{
    ACTIVE,
    SCREEN_SLEEP,
    LIGHT_SLEEP
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
        &product_cache,
        &product_service);
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
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));

    ESP_LOGI(TAG, "Hardware initialized");
}

// ============================================================
// Screen Sleep / Wake + Light Sleep
// ============================================================

void Application::enterScreenSleep()
{
    if (power_state != PowerState::ACTIVE)
        return;

    ESP_LOGI(TAG, "Entering SCREEN_SLEEP...");
    power_state = PowerState::SCREEN_SLEEP;
    screen_sleeping = true;

    // Stop LVGL timers
    lv_timer_enable(false);

    // Blank screen
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_refr_now(NULL);

    // Backlight off
    gpio_set_level(BACKLIGHT_GPIO, 0);

    // LCD sleep
    ili9341_sleep_in();
    vTaskDelay(pdMS_TO_TICKS(LCD_SLEEP_DELAY_MS));

    // Stop barcode reader
    if (barcode_reader)
        barcode_reader->off();

    ESP_LOGI(TAG, "SCREEN_SLEEP entered");
}

void Application::enterLightSleep()
{
    if (power_state != PowerState::SCREEN_SLEEP)
        return;

    ESP_LOGI(TAG, "Entering LIGHT_SLEEP...");

    // Configure wake source: GPIO17 (RTC-capable) low level
    // ext0 wakeup: one RTC IO, level-sensitive
    ESP_ERROR_CHECK(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL));
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(WAKE_GPIO, 0)); // wake on low

    power_state = PowerState::LIGHT_SLEEP;

    // Enter light sleep; CPU stops here until wake
    esp_light_sleep_start();

    // Execution resumes here after wake
    ESP_LOGI(TAG, "Woke from LIGHT_SLEEP");
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

    ESP_LOGI(TAG, "Screen awake (ACTIVE)");
}

// ============================================================
// Main Loop
// ============================================================

void Application::mainLoop()
{
    mainTaskHandle = xTaskGetCurrentTaskHandle();

    while (true)
    {
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

        // Inactivity → SCREEN_SLEEP
        if (power_state == PowerState::ACTIVE)
        {
            uint32_t inactive = lv_disp_get_inactive_time(NULL);
            if (inactive > SLEEP_TIMEOUT_MS)
            {
                enterScreenSleep();
            }
        }

        // SCREEN_SLEEP → LIGHT_SLEEP
        if (power_state == PowerState::SCREEN_SLEEP)
        {
            enterLightSleep();
            // After this returns, we are in LIGHT_SLEEP state but already woke up.
            // wakeScreen() will be called on next loop iteration when wake_flag
            // or barcode activity is detected, or you can call it unconditionally
            // if you want immediate screen wake after CPU wake.
        }

        // ACTIVE → normal LVGL tick + light wait
        if (power_state == PowerState::ACTIVE)
        {
            lvgl_manager.tick();
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5)); // Wait for ISR or timeout
        }

        // LIGHT_SLEEP state:
        // - CPU has just resumed from esp_light_sleep_start()
        // - We wait for wake_flag / barcode / other logic to call wakeScreen()
        if (power_state == PowerState::LIGHT_SLEEP)
        {
            // Option A: always fully wake screen after CPU wake
            // So we just call wakeScreen() here.
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

    screen_sleeping = false;
    wake_flag = false;
    power_state = PowerState::ACTIVE;

    mainLoop();
}