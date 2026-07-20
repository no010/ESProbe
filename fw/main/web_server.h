/**
 * @file web_server.h
 * @brief HTTP server providing WiFi config portal, status endpoint and OTA.
 *
 * Runs on port 80 in both STA and AP-fallback modes. Endpoints:
 *   GET  /         — HTML config portal (WiFi form + status + OTA upload)
 *   POST /save     — Save WiFi credentials to NVS, then restart
 *   GET  /status   — JSON: {ip, rssi, uptime, fw_version, mode, ...}
 *   POST /ota      — Receive a firmware .bin and flash it via esp_ota, restart
 *
 * Requires the "two_ota" partition table (set in sdkconfig.defaults) for the
 * OTA endpoint to have a second app slot to write into.
 */
#ifndef _WEB_SERVER_H_
#define _WEB_SERVER_H_

/**
 * @brief Start the HTTP configuration/OTA server.
 *
 * Call once from app_main() after wifi_init(). Safe to call in either STA
 * or AP-fallback mode; esp_http_server binds to all interfaces.
 */
void web_server_start(void);

#endif /* _WEB_SERVER_H_ */
