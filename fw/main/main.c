/* ESProbe Wireless DAPLink Firmware
   Based on wireless-esp8266-dap, ported to ESP-IDF 5.5.x
*/
#include <string.h>
#include <stdint.h>
#include <sys/param.h>

#include "sdkconfig.h"
#include "main/tcp_server.h"
#include "main/uart_bridge.h"
#include "main/timer.h"
#include "main/wifi_configuration.h"
#include "main/wifi_handle.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "mdns.h"

extern void DAP_Setup(void);
extern void DAP_Thread(void *argument);

TaskHandle_t kDAPTaskHandle = NULL;

static const char *MDNS_TAG = "server_common";

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define DAP_TASK_AFFINITY 1
#else
#define DAP_TASK_AFFINITY 0
#endif

void mdns_setup() {
    int ret;
    ret = mdns_init();
    if (ret != ESP_OK) {
        ESP_LOGW(MDNS_TAG, "mDNS initialize failed:%d", ret);
        return;
    }

    ret = mdns_hostname_set(MDNS_HOSTNAME);
    if (ret != ESP_OK) {
        ESP_LOGW(MDNS_TAG, "mDNS set hostname failed:%d", ret);
        return;
    }
    ESP_LOGI(MDNS_TAG, "mDNS hostname set to: [%s]", MDNS_HOSTNAME);

    ret = mdns_instance_name_set(MDNS_INSTANCE);
    if (ret != ESP_OK) {
        ESP_LOGW(MDNS_TAG, "mDNS set instance name failed:%d", ret);
        return;
    }
    ESP_LOGI(MDNS_TAG, "mDNS instance name set to: [%s]", MDNS_INSTANCE);
}

void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init());

#if (USE_UART_BRIDGE == 1)
    uart_bridge_init();
#endif
    wifi_init();
    DAP_Setup();
    timer_init();

#if (USE_MDNS == 1)
    mdns_setup();
#endif

    // BSD socket TCP server for USBIP
    xTaskCreatePinnedToCore(tcp_server_task, "tcp_server", 4096, NULL, 14, NULL,
                            DAP_TASK_AFFINITY);

    // DAP handle task
    xTaskCreatePinnedToCore(DAP_Thread, "DAP_Task", 2048, NULL, 10, &kDAPTaskHandle,
                            DAP_TASK_AFFINITY);

#if defined CONFIG_IDF_TARGET_ESP8266
    #define UART_BRIDGE_TASK_STACK_SIZE 1024
#else
    #define UART_BRIDGE_TASK_STACK_SIZE 2048
#endif

#if (USE_UART_BRIDGE == 1)
    xTaskCreate(uart_bridge_task, "uart_server", UART_BRIDGE_TASK_STACK_SIZE, NULL, 2, NULL);
#endif
}
