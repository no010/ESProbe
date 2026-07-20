/**
 * @file led_status.c
 * @brief LED status indicator implementation with FreeRTOS blink task.
 */
#include "sdkconfig.h"

#include <stdint.h>
#include <stdatomic.h>

#include "main/led_status.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "components/DAP/include/gpio_op.h"

#ifdef CONFIG_IDF_TARGET_ESP32C3
    #define PIN_LED_STATUS 0
#elif defined CONFIG_IDF_TARGET_ESP32S3
    #define PIN_LED_STATUS 4
#elif defined CONFIG_IDF_TARGET_ESP32
    #define PIN_LED_STATUS 27
#elif defined CONFIG_IDF_TARGET_ESP8266
    #define PIN_LED_STATUS 15
#else
    #error unknown hardware
#endif

static const char *TAG = "LED";

// Atomic state so any task can call led_status_set() safely.
static atomic_int s_led_state = LED_STATE_BOOTING;

// ESProbe LED is active-low: GPIO low = LED on, GPIO high = LED off.
#define LED_ON()  GPIO_SET_LEVEL_LOW(PIN_LED_STATUS)
#define LED_OFF() GPIO_SET_LEVEL_HIGH(PIN_LED_STATUS)

void led_status_set(led_state_t state)
{
    atomic_store(&s_led_state, state);
}

static void led_blink_task(void *arg)
{
    led_state_t prev_state = LED_STATE_BOOTING;
    led_state_t cur_state;
    int phase = 0;

    // Solid-on guard: when we enter WIFI_CONNECTED, turn on immediately.
    bool solid_on = false;

    while (1) {
        cur_state = (led_state_t)atomic_load(&s_led_state);

        if (cur_state != prev_state) {
            phase = 0;
            solid_on = false;
            ESP_LOGI(TAG, "state: %d", (int)cur_state);
            prev_state = cur_state;
        }

        switch (cur_state) {
        case LED_STATE_BOOTING:
            // Slow blink: 500ms on / 500ms off
            if (phase % 2 == 0) { LED_ON();  vTaskDelay(pdMS_TO_TICKS(500)); }
            else                { LED_OFF(); vTaskDelay(pdMS_TO_TICKS(500)); }
            phase++;
            break;

        case LED_STATE_WIFI_CONNECTING:
            // Fast blink: 100ms on / 100ms off
            if (phase % 2 == 0) { LED_ON();  vTaskDelay(pdMS_TO_TICKS(100)); }
            else                { LED_OFF(); vTaskDelay(pdMS_TO_TICKS(100)); }
            phase++;
            break;

        case LED_STATE_WIFI_CONNECTED:
            // Solid on
            if (!solid_on) { LED_ON(); solid_on = true; }
            vTaskDelay(pdMS_TO_TICKS(500));
            break;

        case LED_STATE_AP_MODE:
            // Double blink: 100ms on / 100ms off / 100ms on / 700ms off
            switch (phase) {
            case 0: LED_ON();  vTaskDelay(pdMS_TO_TICKS(100)); break;
            case 1: LED_OFF(); vTaskDelay(pdMS_TO_TICKS(100)); break;
            case 2: LED_ON();  vTaskDelay(pdMS_TO_TICKS(100)); break;
            default: LED_OFF(); vTaskDelay(pdMS_TO_TICKS(700)); break;
            }
            phase = (phase + 1) % 4;
            break;

        case LED_STATE_ERROR:
            // Very fast blink: 50ms on / 50ms off
            if (phase % 2 == 0) { LED_ON();  vTaskDelay(pdMS_TO_TICKS(50)); }
            else                { LED_OFF(); vTaskDelay(pdMS_TO_TICKS(50)); }
            phase++;
            break;

        default:
            LED_OFF();
            vTaskDelay(pdMS_TO_TICKS(1000));
            break;
        }
    }
}

void led_status_init(void)
{
    GPIO_FUNCTION_SET(PIN_LED_STATUS);
    GPIO_SET_DIRECTION_NORMAL_OUT(PIN_LED_STATUS);
    LED_OFF();

    atomic_store(&s_led_state, LED_STATE_BOOTING);

    xTaskCreate(led_blink_task, "led_status", 2048, NULL, 1, NULL);
    ESP_LOGI(TAG, "LED status task started on GPIO%d", PIN_LED_STATUS);
}
