/**
 * @file wifi_configuration.h
 * @brief Fill in your wifi configuration information here.
 * @version 0.1
 * @date 2020-01-22
 *
 * @copyright Copyright (c) 2020
 *
 */
#ifndef __WIFI_CONFIGURATION__
#define __WIFI_CONFIGURATION__


static struct {
    const char *ssid;
    const char *password;
} wifi_list[] __attribute__((unused)) = {
    {.ssid = CONFIG_WIFI_SSID_1, .password = CONFIG_WIFI_PASS_1},
    {.ssid = CONFIG_WIFI_SSID_2, .password = CONFIG_WIFI_PASS_2},
};

#define WIFI_LIST_SIZE (sizeof(wifi_list) / sizeof(wifi_list[0]))

#define USE_MDNS       1
// Use the address "dap.local" to access the device
#define MDNS_HOSTNAME "dap"
#define MDNS_INSTANCE "DAP mDNS"
//

// #define USE_STATIC_IP 1
// If you don't want to specify the ip configuration, then ignore the following items.
// #define DAP_IP_ADDRESS 192, 168, 137, 123
// #define DAP_IP_GATEWAY 192, 168, 137, 1
// #define DAP_IP_NETMASK 255, 255, 255, 0
//

#define USE_OTA              0

// --- WiFi AP Fallback ---
// When STA connection fails after MAX_RETRIES attempts, the device
// switches to AP mode so the host can connect directly.
// Configurable via menuconfig (AP Fallback menu).
#ifdef CONFIG_USE_AP_FALLBACK
#define USE_AP_FALLBACK      1
#else
#define USE_AP_FALLBACK      0
#endif
#ifdef CONFIG_AP_FALLBACK_MAX_RETRIES
#define AP_FALLBACK_MAX_RETRIES  CONFIG_AP_FALLBACK_MAX_RETRIES
#else
#define AP_FALLBACK_MAX_RETRIES  10
#endif
#define AP_SSID_PREFIX       CONFIG_AP_SSID_PREFIX
#define AP_PASSWORD          CONFIG_AP_PASSWORD
#define AP_CHANNEL           1

#define USE_UART_BRIDGE      1
#define UART_BRIDGE_PORT     1234
#define UART_BRIDGE_BAUDRATE 115200
//

// HTTP config portal + status + OTA server on port 80.
#define USE_WEB_SERVER       1

// DO NOT CHANGE
#define USE_TCP_NETCONN 0

#define PORT                3240
#define CONFIG_EXAMPLE_IPV4 1
#define USE_KCP             0
#define MTU_SIZE            1500
//

#if (USE_TCP_NETCONN == 1 && USE_KCP == 1)
#error Can not use KCP and TCP at the same time!
#endif

#if (USE_KCP == 1)
#warning KCP is a very experimental feature, and it should not be used under any circumstances. Please make sure what you are doing. Related usbip version: https://github.com/windowsair/usbip-win
#endif


#include <stdio.h>
#include <stdarg.h>

static inline int os_printf(const char *__restrict __fmt, ...) {
    va_list args;
    va_start(args, __fmt);
    int ret = vprintf(__fmt, args);
    va_end(args);
    return ret;
}

#endif