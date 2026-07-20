/**
 * @file led_status.h
 * @brief LED status indicator with blink patterns for different system states.
 *
 * States and patterns (on ESP32-C3, LED on GPIO0, active-low):
 *   BOOTING         — slow blink (500ms on / 500ms off)
 *   WIFI_CONNECTING — fast blink (100ms on / 100ms off)
 *   WIFI_CONNECTED  — solid on
 *   AP_MODE         — double blink (100ms on / 100ms off / 100ms on / 700ms off)
 *   ERROR           — very fast blink (50ms on / 50ms off)
 */
#ifndef _LED_STATUS_H_
#define _LED_STATUS_H_

#include <stdbool.h>

typedef enum {
    LED_STATE_BOOTING = 0,
    LED_STATE_WIFI_CONNECTING,
    LED_STATE_WIFI_CONNECTED,
    LED_STATE_AP_MODE,
    LED_STATE_ERROR,
} led_state_t;

/**
 * @brief Initialize the LED GPIO and start the blink task.
 *
 * Call once from app_main() after WiFi/DAP init.
 * The LED starts in BOOTING state.
 */
void led_status_init(void);

/**
 * @brief Set the current LED state.
 *
 * Thread-safe; can be called from any task or event handler.
 *
 * @param state New LED state.
 */
void led_status_set(led_state_t state);

#endif /* _LED_STATUS_H_ */
