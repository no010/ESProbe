/**
 * @file wifi_config_store.h
 * @brief NVS-backed WiFi credential storage with Kconfig fallback.
 *
 * On boot, wifi_config_store_load() checks NVS for saved SSID/password.
 * If found, they override the compile-time Kconfig defaults. If NVS is
 * empty or read fails, the Kconfig defaults (CONFIG_WIFI_SSID_1 etc.)
 * are used, preserving backward compatibility.
 *
 * wifi_config_store_save() persists new credentials to NVS so subsequent
 * boots use them without reflashing.
 *
 * wifi_config_store_clear() erases stored credentials, forcing fallback
 * to Kconfig defaults on next boot. Used by the BOOT button long-press
 * "factory reset" action.
 */
#ifndef _WIFI_CONFIG_STORE_H_
#define _WIFI_CONFIG_STORE_H_

#include <stdbool.h>

#define WIFI_SSID_MAX_LEN  32
#define WIFI_PASS_MAX_LEN  64

/**
 * @brief Load WiFi credentials from NVS into the provided buffers.
 *
 * If NVS has no stored credentials, copies the Kconfig default
 * (CONFIG_WIFI_SSID_1 / CONFIG_WIFI_PASS_1) instead.
 *
 * @param ssid_out   Buffer of at least WIFI_SSID_MAX_LEN bytes.
 * @param pass_out   Buffer of at least WIFI_PASS_MAX_LEN bytes.
 * @return true if credentials came from NVS, false if from Kconfig defaults.
 */
bool wifi_config_store_load(char *ssid_out, char *pass_out);

/**
 * @brief Save WiFi credentials to NVS.
 *
 * @param ssid SSID string (max 31 chars + NUL).
 * @param pass Password string (max 63 chars + NUL). Empty string for open AP.
 * @return 0 on success, -1 on failure.
 */
int wifi_config_store_save(const char *ssid, const char *pass);

/**
 * @brief Erase stored WiFi credentials from NVS.
 *
 * Next boot will fall back to Kconfig defaults.
 *
 * @return 0 on success, -1 on failure.
 */
int wifi_config_store_clear(void);

#endif /* _WIFI_CONFIG_STORE_H_ */
