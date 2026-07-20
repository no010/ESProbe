#include "sdkconfig.h"

#include <string.h>
#include <stdint.h>
#include <sys/param.h>

#include "main/wifi_configuration.h"
#include "main/uart_bridge.h"
#include "main/led_status.h"
#include "main/wifi_config_store.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/ip4_addr.h"

static const char *TAG = "WIFI";

static EventGroupHandle_t wifi_event_group;
static int ssid_index = 0;
static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;

static int s_sta_retry_count = 0;
static bool s_ap_mode_active = false;

// Persistent buffers for NVS-loaded credentials. Must outlive wifi_init()
// because wifi_list[] pointers reference them.
static char s_nvs_ssid[WIFI_SSID_MAX_LEN];
static char s_nvs_pass[WIFI_PASS_MAX_LEN];

const int IPV4_GOTIP_BIT = BIT0;
const int AP_READY_BIT = BIT1;
#ifdef CONFIG_EXAMPLE_IPV6
const int IPV6_GOTIP_BIT = BIT2;
#endif

static void ssid_change(void);
static void start_ap_mode(void);

bool wifi_is_ap_mode(void)
{
    return s_ap_mode_active;
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        led_status_set(LED_STATE_WIFI_CONNECTING);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
#ifdef CONFIG_EXAMPLE_IPV6
        esp_netif_create_ip6_linklocal(sta_netif);
#endif
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_sta_retry_count = 0;
        led_status_set(LED_STATE_WIFI_CONNECTED);
        xEventGroupSetBits(wifi_event_group, IPV4_GOTIP_BIT);
        os_printf("SYSTEM EVENT STA GOT IP : " IPSTR "\r\n", IP2STR(&event->ip_info.ip));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        led_status_set(LED_STATE_WIFI_CONNECTING);
        os_printf("Disconnect reason : %d\r\n", (int)event->reason);

#ifdef CONFIG_IDF_TARGET_ESP8266
        if (event->reason == WIFI_REASON_BASIC_RATE_NOT_SUPPORT) {
            esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
        }
#endif
        s_sta_retry_count++;
        os_printf("STA retry %d/%d\r\n", s_sta_retry_count, AP_FALLBACK_MAX_RETRIES);

#if (USE_AP_FALLBACK == 1)
        if (s_sta_retry_count >= AP_FALLBACK_MAX_RETRIES) {
            ESP_LOGW(TAG, "STA failed %d times, switching to AP mode", s_sta_retry_count);
            xEventGroupClearBits(wifi_event_group, IPV4_GOTIP_BIT);
#if (USE_UART_BRIDGE == 1)
            uart_bridge_close();
#endif
            start_ap_mode();
            return;
        }
#endif
        ssid_change();
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, IPV4_GOTIP_BIT);
#ifdef CONFIG_EXAMPLE_IPV6
        xEventGroupClearBits(wifi_event_group, IPV6_GOTIP_BIT);
#endif

#if (USE_UART_BRIDGE == 1)
        uart_bridge_close();
#endif
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "AP mode started");
        led_status_set(LED_STATE_AP_MODE);
        xEventGroupSetBits(wifi_event_group, AP_READY_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_GOT_IP6) {
#ifdef CONFIG_EXAMPLE_IPV6
        ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
        xEventGroupSetBits(wifi_event_group, IPV6_GOTIP_BIT);
        os_printf("SYSTEM_EVENT_STA_GOT_IP6\r\n");
        os_printf("IPv6: %s\r\n", ip6addr_ntoa(&event->ip6_info.ip));
#endif
    }
}

static void ssid_change(void)
{
    if (ssid_index > WIFI_LIST_SIZE - 1) {
        ssid_index = 0;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "",
            .password = "",
        },
    };

    strcpy((char *)wifi_config.sta.ssid, wifi_list[ssid_index].ssid);
    strcpy((char *)wifi_config.sta.password, wifi_list[ssid_index].password);
    ssid_index++;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
}

#if (USE_AP_FALLBACK == 1)
static void start_ap_mode(void)
{
    if (s_ap_mode_active) {
        return;  // Already in AP mode — avoid double invocation.
    }

    // Stop STA and switch to AP.
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT) {
        ESP_LOGE(TAG, "esp_wifi_disconnect failed: %s", esp_err_to_name(err));
    }

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode(AP) failed: %s", esp_err_to_name(err));
        return;
    }

    // Generate AP SSID from MAC address: "ESProbe-XXXX"
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ap_ssid[32];
    snprintf(ap_ssid, sizeof(ap_ssid), "%s-%02X%02X",
             AP_SSID_PREFIX, mac[4], mac[5]);

    wifi_config_t ap_config = {
        .ap = {
            .ssid = "",
            .ssid_len = 0,
            .channel = AP_CHANNEL,
            .password = AP_PASSWORD,
            .max_connection = 2,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    // Copy the generated SSID into the config, bounded and NUL-terminated.
    size_t ssid_len = strlen(ap_ssid);
    if (ssid_len >= sizeof(ap_config.ap.ssid)) {
        ssid_len = sizeof(ap_config.ap.ssid) - 1;
    }
    memcpy(ap_config.ap.ssid, ap_ssid, ssid_len);
    ap_config.ap.ssid[ssid_len] = '\0';
    ap_config.ap.ssid_len = ssid_len;

    // Set auth mode based on whether password is set.
    if (strlen(AP_PASSWORD) > 0) {
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start(AP) failed: %s", esp_err_to_name(err));
        return;  // AP failed — do not set s_ap_mode_active or AP_READY_BIT.
    }

    s_ap_mode_active = true;

    // Set AP IP to 192.168.4.1 (ESP-IDF default for AP).
    ESP_LOGI(TAG, "AP mode: SSID=%s, IP=192.168.4.1", ap_ssid);
    os_printf("AP_MODE SSID=%s IP=192.168.4.1\r\n", ap_ssid);
}
#endif

static void wait_for_ip(void)
{
#ifdef CONFIG_EXAMPLE_IPV6
    uint32_t bits = IPV4_GOTIP_BIT | IPV6_GOTIP_BIT | AP_READY_BIT;
#else
    uint32_t bits = IPV4_GOTIP_BIT | AP_READY_BIT;
#endif

    os_printf("Waiting for AP connection...\r\n");
    // Wait up to 60 seconds for either STA IP or AP mode.
    EventBits_t result = xEventGroupWaitBits(wifi_event_group, bits,
                                              false, false, pdMS_TO_TICKS(60000));
    if (result & IPV4_GOTIP_BIT) {
        os_printf("Connected to AP (STA mode)\r\n");
    } else if (result & AP_READY_BIT) {
        os_printf("Running in AP fallback mode\r\n");
    } else {
#if (USE_AP_FALLBACK == 1)
        ESP_LOGW(TAG, "Timeout waiting for IP, forcing AP mode");
        start_ap_mode();
        xEventGroupWaitBits(wifi_event_group, AP_READY_BIT, false, false, portMAX_DELAY);
#else
        os_printf("Still waiting for IP...\r\n");
        xEventGroupWaitBits(wifi_event_group, IPV4_GOTIP_BIT, false, true, portMAX_DELAY);
        os_printf("Connected to AP\r\n");
#endif
    }
}

void wifi_init(void)
{
    // LED status is initialized in app_main(); just set state here.
    led_status_set(LED_STATE_BOOTING);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

#if (USE_AP_FALLBACK == 1)
    ap_netif = esp_netif_create_default_wifi_ap();
#endif

#if (USE_STATIC_IP == 1)
    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(sta_netif));

    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = ESP_IP4TOADDR(192, 168, 137, 123);
    ip_info.gw.addr = ESP_IP4TOADDR(192, 168, 137, 1);
    ip_info.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);

    ESP_ERROR_CHECK(esp_netif_set_ip_info(sta_netif, &ip_info));
#endif // (USE_STATIC_IP == 1)

    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));
#ifdef CONFIG_EXAMPLE_IPV6
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_GOT_IP6,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));
#endif

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    // Load WiFi credentials from NVS; fall back to Kconfig defaults.
    bool from_nvs = wifi_config_store_load(s_nvs_ssid, s_nvs_pass);

    if (from_nvs) {
        // Override the first entry in wifi_list with NVS credentials.
        // The second entry remains as Kconfig fallback.
        wifi_list[0].ssid = s_nvs_ssid;
        wifi_list[0].password = s_nvs_pass;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ssid_change();
    led_status_set(LED_STATE_WIFI_CONNECTING);
    ESP_ERROR_CHECK(esp_wifi_start());

    wait_for_ip();
}
