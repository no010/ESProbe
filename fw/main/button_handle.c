/**
 * @file button_handle.c
 * @brief BOOT button handler implementation.
 */
#include "sdkconfig.h"

#include <stdint.h>

#include "main/button_handle.h"
#include "main/wifi_config_store.h"
#include "main/led_status.h"
#include "main/wifi_configuration.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"

#include "components/DAP/include/gpio_op.h"
#include "components/DAP/config/DAP_config.h"

static const char *TAG = "BTN";

#ifdef CONFIG_IDF_TARGET_ESP32C3
    #define PIN_BOOT_BUTTON 9
#elif defined CONFIG_IDF_TARGET_ESP32S3
    #define PIN_BOOT_BUTTON 0
#elif defined CONFIG_IDF_TARGET_ESP32
    #define PIN_BOOT_BUTTON 0
#else
    #define PIN_BOOT_BUTTON 0
#endif

// Long-press threshold in milliseconds.
#define LONG_PRESS_MS 3000
// Polling interval.
#define POLL_INTERVAL_MS 20
// nRESET pulse duration.
#define RESET_PULSE_MS 50

static void button_task(void *arg)
{
    int press_duration = 0;
    bool was_pressed = false;

    while (1) {
        // BOOT button is active-low: 0 = pressed, 1 = released.
        int level = gpio_get_level(PIN_BOOT_BUTTON);
        bool pressed = (level == 0);

        if (pressed) {
            press_duration += POLL_INTERVAL_MS;

            // At 2 seconds into a long press, signal AP/error mode visually.
            if (press_duration == 2000) {
                led_status_set(LED_STATE_ERROR);
            }
        } else {
            if (was_pressed) {
                if (press_duration >= LONG_PRESS_MS) {
                    // Long press: clear NVS WiFi creds and restart.
                    ESP_LOGW(TAG, "Long press detected — clearing WiFi creds and restarting");
                    wifi_config_store_clear();
                    vTaskDelay(pdMS_TO_TICKS(200));
                    esp_restart();
                } else if (press_duration > 50) {
                    // Short press: pulse target nRESET.
                    ESP_LOGI(TAG, "Short press — pulsing target nRESET");
#if defined(PIN_nRESET) && PIN_nRESET != _
                    GPIO_SET_LEVEL_LOW(PIN_nRESET);
                    vTaskDelay(pdMS_TO_TICKS(RESET_PULSE_MS));
                    GPIO_SET_LEVEL_HIGH(PIN_nRESET);
#endif
                    // Restore LED state based on WiFi status.
                    // The WiFi event handler will set the correct state.
                }
            }
            press_duration = 0;
        }

        was_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void button_handle_init(void)
{
    // Configure BOOT button as input with pull-up.
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_BOOT_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return;
    }

    // Configure nRESET pin as open-drain output for the short-press pulse.
    // NOTE: On ESP32-C3, PIN_nRESET (IO2) shares the same physical pin as
    // PIN_TDO. Since JTAG is disabled (DAP_JTAG=0), TDO is unused and the
    // collision is harmless. During an active SWD debug session the DAP
    // layer also drives this pin — avoid pressing the button mid-debug.
#if defined(PIN_nRESET) && PIN_nRESET != _
    gpio_config_t rst_conf = {
        .pin_bit_mask = (1ULL << PIN_nRESET),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&rst_conf);
    if (err == ESP_OK) {
        gpio_set_level(PIN_nRESET, 1);  // idle high (released)
    } else {
        ESP_LOGW(TAG, "nRESET pin config failed: %s", esp_err_to_name(err));
    }
#endif

    xTaskCreate(button_task, "button", 2048, NULL, 2, NULL);
    ESP_LOGI(TAG, "Button handler started on GPIO%d", PIN_BOOT_BUTTON);
}
