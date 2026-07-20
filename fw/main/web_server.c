/**
 * @file web_server.c
 * @brief HTTP config portal + status + OTA server implementation.
 */
#include "sdkconfig.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "main/web_server.h"
#include "main/wifi_config_store.h"
#include "main/wifi_handle.h"
#include "main/dap_configuration.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"

static const char *TAG = "WEB";

#define OTA_RECV_BUF_SIZE 1024

// ---------------------------------------------------------------------------
// Config portal HTML (served at GET /). Kept compact and dependency-free.
// ---------------------------------------------------------------------------
static const char kIndexHtml[] =
"<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>ESProbe</title><style>"
"body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;max-width:480px;"
"margin:0 auto;padding:16px;color:#333;background:#f5f5f5}"
"h1{font-size:20px;color:#2b5797}h2{font-size:15px;color:#2b5797;margin-top:24px}"
"input{width:100%;padding:8px;margin:6px 0;box-sizing:border-box;border:1px solid #ccc;border-radius:4px}"
"button{background:#2b5797;color:#fff;border:0;padding:10px 16px;border-radius:4px;cursor:pointer;width:100%}"
"button:hover{background:#1e3f6f}.card{background:#fff;border-radius:8px;padding:16px;margin:12px 0}"
"pre{background:#1e1e1e;color:#d4d4d4;padding:12px;border-radius:6px;overflow-x:auto;font-size:12px}"
"</style></head><body>"
"<h1>ESProbe Config</h1>"
"<div class=\"card\"><h2>WiFi</h2>"
"<form method=\"POST\" action=\"/save\">"
"<input name=\"ssid\" placeholder=\"SSID\" required maxlength=\"31\">"
"<input name=\"pass\" type=\"password\" placeholder=\"Password\" maxlength=\"63\">"
"<button type=\"submit\">Save &amp; Reboot</button></form></div>"
"<div class=\"card\"><h2>Status</h2><pre id=\"st\">loading...</pre></div>"
"<div class=\"card\"><h2>Firmware Update (OTA)</h2>"
"<input type=\"file\" id=\"fw\" accept=\".bin\">"
"<button id=\"up\">Upload &amp; Flash</button>"
"<pre id=\"otalog\"></pre></div>"
"<script>"
"fetch('/status').then(r=>r.json()).then(j=>{document.getElementById('st').textContent=JSON.stringify(j,null,2)}).catch(e=>{document.getElementById('st').textContent='error'});"
"document.getElementById('up').onclick=function(){"
"var f=document.getElementById('fw').files[0];var l=document.getElementById('otalog');"
"if(!f){l.textContent='select a .bin first';return;}"
"l.textContent='uploading '+f.size+' bytes...';"
"fetch('/ota',{method:'POST',body:f}).then(r=>r.text()).then(t=>{l.textContent=t;}).catch(e=>{l.textContent='OTA failed: '+e;});"
"};"
"</script></body></html>";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Reboot after a short delay so the HTTP response has time to flush.
static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

static void schedule_restart(void)
{
    xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
}

// In-place URL decode ("%XX" and '+' -> ' ').
static void url_decode(char *s)
{
    char *dst = s;
    while (*s) {
        if (*s == '%' && s[1] && s[2]) {
            char hi = s[1], lo = s[2];
            int h = (hi >= 'A') ? ((hi & 0xDF) - 'A' + 10) : (hi - '0');
            int l = (lo >= 'A') ? ((lo & 0xDF) - 'A' + 10) : (lo - '0');
            *dst++ = (char)((h << 4) | l);
            s += 3;
        } else if (*s == '+') {
            *dst++ = ' ';
            s++;
        } else {
            *dst++ = *s++;
        }
    }
    *dst = '\0';
}

// ---------------------------------------------------------------------------
// GET /  — config portal
// ---------------------------------------------------------------------------
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

// ---------------------------------------------------------------------------
// POST /save  — persist WiFi credentials then reboot
// ---------------------------------------------------------------------------
static esp_err_t save_post_handler(httpd_req_t *req)
{
    char buf[160];
    int total = req->content_len < (int)sizeof(buf) - 1 ? req->content_len : (int)sizeof(buf) - 1;
    int received = 0;

    while (received < total) {
        int r = httpd_req_recv(req, buf + received, total - received);
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv failed");
            return ESP_FAIL;
        }
        received += r;
    }
    buf[received] = '\0';

    char ssid[WIFI_SSID_MAX_LEN] = {0};
    char pass[WIFI_PASS_MAX_LEN] = {0};

    if (httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid)) != ESP_OK ||
        strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
        return ESP_FAIL;
    }
    // Password may be empty (open network) — ignore not-found.
    httpd_query_key_value(buf, "pass", pass, sizeof(pass));

    url_decode(ssid);
    url_decode(pass);

    if (wifi_config_store_save(ssid, pass) != 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "nvs save failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Saved SSID=%s, rebooting", ssid);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<html><body style='font-family:sans-serif'>"
        "<h2>Saved. Rebooting...</h2>"
        "<p>The device will reconnect using the new WiFi settings.</p>"
        "</body></html>");

    schedule_restart();
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// GET /status  — JSON status
// ---------------------------------------------------------------------------
static esp_err_t status_get_handler(httpd_req_t *req)
{
    char json[320];

    // IP address of the active interface.
    char ip_str[16] = "0.0.0.0";
    esp_netif_t *netif = wifi_is_ap_mode()
        ? esp_netif_get_handle_from_ifkey("WIFI_AP_DEF")
        : esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip.ip));
        }
    }

    // RSSI (STA mode only).
    int rssi = 0;
    if (!wifi_is_ap_mode()) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            rssi = ap.rssi;
        }
    }

    uint32_t uptime = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    const esp_partition_t *running = esp_ota_get_running_partition();
    const char *part = running ? running->label : "?";

    snprintf(json, sizeof(json),
        "{\"fw_version\":\"%s\",\"mode\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,"
        "\"uptime_s\":%lu,\"partition\":\"%s\",\"usbip_port\":3240,\"uart_port\":1234}",
        ESPROBE_FW_VERSION,
        wifi_is_ap_mode() ? "ap" : "sta",
        ip_str, rssi, (unsigned long)uptime, part);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

// ---------------------------------------------------------------------------
// POST /ota  — receive raw .bin body and flash via esp_ota
// ---------------------------------------------------------------------------
static esp_err_t ota_post_handler(httpd_req_t *req)
{
    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    if (update == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "No OTA partition (rebuild with two_ota table)");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "OTA -> partition '%s' (size %lu), content_len=%d",
             update->label, (unsigned long)update->size, req->content_len);

    esp_ota_handle_t ota = 0;
    esp_err_t err = esp_ota_begin(update, OTA_SIZE_UNKNOWN, &ota);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_begin failed");
        return ESP_FAIL;
    }

    char *buf = malloc(OTA_RECV_BUF_SIZE);
    if (buf == NULL) {
        esp_ota_abort(ota);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    int written = 0;
    while (remaining > 0) {
        int to_read = remaining < OTA_RECV_BUF_SIZE ? remaining : OTA_RECV_BUF_SIZE;
        int r = httpd_req_recv(req, buf, to_read);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            free(buf);
            esp_ota_abort(ota);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv error");
            return ESP_FAIL;
        }
        err = esp_ota_write(ota, buf, r);
        if (err != ESP_OK) {
            free(buf);
            esp_ota_abort(ota);
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_write failed");
            return ESP_FAIL;
        }
        written += r;
        remaining -= r;
    }
    free(buf);

    err = esp_ota_end(ota);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "ota_end failed (image invalid?)");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "set_boot failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA OK, wrote %d bytes, rebooting into '%s'", written, update->label);
    char msg[96];
    snprintf(msg, sizeof(msg), "OTA success (%d bytes). Rebooting into %s...",
             written, update->label);
    httpd_resp_sendstr(req, msg);

    schedule_restart();
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Server bootstrap
// ---------------------------------------------------------------------------
void web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;          // OTA path needs headroom
    config.lru_purge_enable = true;
    config.max_uri_handlers = 8;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return;
    }

    static const httpd_uri_t uri_root = {
        .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
    static const httpd_uri_t uri_save = {
        .uri = "/save", .method = HTTP_POST, .handler = save_post_handler };
    static const httpd_uri_t uri_status = {
        .uri = "/status", .method = HTTP_GET, .handler = status_get_handler };
    static const httpd_uri_t uri_ota = {
        .uri = "/ota", .method = HTTP_POST, .handler = ota_post_handler };

    httpd_register_uri_handler(server, &uri_root);
    httpd_register_uri_handler(server, &uri_save);
    httpd_register_uri_handler(server, &uri_status);
    httpd_register_uri_handler(server, &uri_ota);

    ESP_LOGI(TAG, "Web server started on port 80");
}
