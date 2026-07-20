/**
 * @file wifi_config_store.c
 * @brief NVS-backed WiFi credential storage implementation.
 */
#include "sdkconfig.h"

#include <string.h>

#include "main/wifi_config_store.h"
#include "main/wifi_configuration.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "WIFI_NVS";

#define NVS_NAMESPACE "wifi_creds"
#define NVS_KEY_SSID  "ssid"
#define NVS_KEY_PASS  "pass"

bool wifi_config_store_load(char *ssid_out, char *pass_out)
{
    nvs_handle_t h;
    esp_err_t err;
    bool from_nvs = false;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        // NVS namespace doesn't exist yet — use Kconfig defaults.
        strncpy(ssid_out, CONFIG_WIFI_SSID_1, WIFI_SSID_MAX_LEN - 1);
        ssid_out[WIFI_SSID_MAX_LEN - 1] = '\0';
        strncpy(pass_out, CONFIG_WIFI_PASS_1, WIFI_PASS_MAX_LEN - 1);
        pass_out[WIFI_PASS_MAX_LEN - 1] = '\0';
        ESP_LOGI(TAG, "No NVS creds, using Kconfig default: SSID=%s", ssid_out);
        return false;
    }

    size_t ssid_len = WIFI_SSID_MAX_LEN;
    size_t pass_len = WIFI_PASS_MAX_LEN;

    err = nvs_get_str(h, NVS_KEY_SSID, ssid_out, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(h, NVS_KEY_PASS, pass_out, &pass_len);
        if (err == ESP_OK) {
            from_nvs = true;
            ESP_LOGI(TAG, "Loaded creds from NVS: SSID=%s", ssid_out);
        } else {
            // SSID found but password missing — use Kconfig pass as fallback.
            strncpy(pass_out, CONFIG_WIFI_PASS_1, WIFI_PASS_MAX_LEN - 1);
            pass_out[WIFI_PASS_MAX_LEN - 1] = '\0';
            ESP_LOGW(TAG, "NVS SSID found but password missing, using Kconfig pass");
            from_nvs = true;
        }
    } else {
        // No NVS entry — fall back to Kconfig defaults.
        strncpy(ssid_out, CONFIG_WIFI_SSID_1, WIFI_SSID_MAX_LEN - 1);
        ssid_out[WIFI_SSID_MAX_LEN - 1] = '\0';
        strncpy(pass_out, CONFIG_WIFI_PASS_1, WIFI_PASS_MAX_LEN - 1);
        pass_out[WIFI_PASS_MAX_LEN - 1] = '\0';
        ESP_LOGI(TAG, "No NVS entry, using Kconfig default: SSID=%s", ssid_out);
    }

    nvs_close(h);
    return from_nvs;
}

int wifi_config_store_save(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return -1;
    }

    err = nvs_set_str(h, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str(ssid) failed: %s", esp_err_to_name(err));
        nvs_close(h);
        return -1;
    }

    err = nvs_set_str(h, NVS_KEY_PASS, pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str(pass) failed: %s", esp_err_to_name(err));
        nvs_close(h);
        return -1;
    }

    err = nvs_commit(h);
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        return -1;
    }

    ESP_LOGI(TAG, "Saved WiFi creds to NVS: SSID=%s", ssid);
    return 0;
}

int wifi_config_store_clear(void)
{
    nvs_handle_t h;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        // Nothing to clear if namespace doesn't exist.
        ESP_LOGI(TAG, "NVS namespace not found, nothing to clear");
        return 0;
    }

    err = nvs_erase_key(h, NVS_KEY_SSID);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_erase_key(ssid) failed: %s", esp_err_to_name(err));
        nvs_close(h);
        return -1;
    }

    err = nvs_erase_key(h, NVS_KEY_PASS);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_erase_key(pass) failed: %s", esp_err_to_name(err));
        nvs_close(h);
        return -1;
    }

    err = nvs_commit(h);
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        return -1;
    }

    ESP_LOGI(TAG, "Cleared WiFi creds from NVS");
    return 0;
}
