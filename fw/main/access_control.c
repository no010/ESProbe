/**
 * @file access_control.c
 * @brief PIN-based access control implementation (see access_control.h).
 *
 * Follows the project convention of atomic state + static buffers for
 * cross-task safety (web server task writes, tcp/uart tasks read).
 */
#include <string.h>
#include <stdatomic.h>

#include "main/access_control.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "AUTH";

#define AUTH_NVS_NAMESPACE "authcfg"
#define AUTH_NVS_KEY_PIN   "pin"

static char s_pin[ACCESS_PIN_MAX_LEN] = {0};
static _Atomic bool s_pin_set = false;

// Unlocked client IPs (network byte order). 0 = free slot.
static _Atomic uint32_t s_sessions[ACCESS_MAX_SESSIONS] = {0};

void access_control_init(void)
{
    nvs_handle_t nvs;
    if (nvs_open(AUTH_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGI(TAG, "No auth config, debug ports open");
        return;
    }
    size_t len = sizeof(s_pin);
    esp_err_t err = nvs_get_str(nvs, AUTH_NVS_KEY_PIN, s_pin, &len);
    nvs_close(nvs);

    if (err == ESP_OK && s_pin[0] != '\0') {
        atomic_store(&s_pin_set, true);
        ESP_LOGI(TAG, "PIN configured, debug ports locked until web unlock");
    }
}

bool access_control_has_pin(void)
{
    return atomic_load(&s_pin_set);
}

bool access_control_is_allowed(uint32_t ip)
{
    if (!atomic_load(&s_pin_set))
        return true;

    for (int i = 0; i < ACCESS_MAX_SESSIONS; i++) {
        if (atomic_load(&s_sessions[i]) == ip)
            return true;
    }
    return false;
}

int access_control_unlock(uint32_t ip, const char *pin)
{
    if (!atomic_load(&s_pin_set))
        return 0; // nothing to unlock

    if (pin == NULL || strncmp(pin, s_pin, sizeof(s_pin)) != 0) {
        ESP_LOGW(TAG, "Unlock rejected: wrong PIN");
        return -1;
    }

    // Already unlocked?
    for (int i = 0; i < ACCESS_MAX_SESSIONS; i++) {
        if (atomic_load(&s_sessions[i]) == ip)
            return 0;
    }
    // Claim a free slot.
    for (int i = 0; i < ACCESS_MAX_SESSIONS; i++) {
        uint32_t expected = 0;
        if (atomic_compare_exchange_strong(&s_sessions[i], &expected, ip)) {
            ESP_LOGI(TAG, "Client unlocked (slot %d)", i);
            return 0;
        }
    }
    ESP_LOGW(TAG, "Unlock rejected: session table full");
    return -2;
}

int access_control_set_pin(const char *new_pin, const char *old_pin)
{
    if (atomic_load(&s_pin_set)) {
        if (old_pin == NULL || strncmp(old_pin, s_pin, sizeof(s_pin)) != 0)
            return -1;
    }
    if (new_pin == NULL)
        new_pin = "";
    if (strlen(new_pin) >= ACCESS_PIN_MAX_LEN)
        return -2;

    nvs_handle_t nvs;
    if (nvs_open(AUTH_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK)
        return -2;

    esp_err_t err;
    if (new_pin[0] == '\0') {
        err = nvs_erase_key(nvs, AUTH_NVS_KEY_PIN);
        if (err == ESP_ERR_NVS_NOT_FOUND)
            err = ESP_OK;
    } else {
        err = nvs_set_str(nvs, AUTH_NVS_KEY_PIN, new_pin);
    }
    if (err == ESP_OK)
        err = nvs_commit(nvs);
    nvs_close(nvs);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS write failed: %s", esp_err_to_name(err));
        return -2;
    }

    // Apply in RAM after successful persist. Clear sessions on any change.
    strlcpy(s_pin, new_pin, sizeof(s_pin));
    for (int i = 0; i < ACCESS_MAX_SESSIONS; i++)
        atomic_store(&s_sessions[i], 0);
    atomic_store(&s_pin_set, new_pin[0] != '\0');

    ESP_LOGI(TAG, "PIN %s", new_pin[0] ? "updated" : "cleared");
    return 0;
}
