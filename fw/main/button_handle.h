/**
 * @file button_handle.h
 * @brief BOOT button handler for WiFi reset and factory reset.
 *
 * On ESP32-C3, the BOOT button is on GPIO9 (active-low).
 *
 * Actions:
 *   Long press (≥3s)  — Clear NVS WiFi credentials and restart.
 *                        Next boot falls back to Kconfig defaults or AP mode.
 *   Short press       — Trigger target nRESET pulse (useful for re-flashing
 *                        the target MCU without touching cables).
 */
#ifndef _BUTTON_HANDLE_H_
#define _BUTTON_HANDLE_H_

/**
 * @brief Initialize the BOOT button and start the polling task.
 *
 * Call once from app_main() after WiFi and DAP are initialized.
 */
void button_handle_init(void);

#endif /* _BUTTON_HANDLE_H_ */
