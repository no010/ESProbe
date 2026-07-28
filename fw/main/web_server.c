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
#include "main/access_control.h"
#include "main/uart_bridge.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"

#include "lwip/sockets.h"

#include <stdatomic.h>

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
"<div class=\"card\"><h2>Security</h2>"
"<p id=\"lockst\" style=\"font-size:13px\"></p>"
"<input id=\"pin\" type=\"password\" placeholder=\"PIN\" maxlength=\"15\">"
"<button id=\"unlock\" type=\"button\">Unlock debug ports</button>"
"<details style=\"margin-top:8px\"><summary style=\"font-size:13px;cursor:pointer\">Change / set PIN</summary>"
"<input id=\"oldpin\" type=\"password\" placeholder=\"Current PIN (if set)\" maxlength=\"15\">"
"<input id=\"newpin\" type=\"password\" placeholder=\"New PIN (empty = disable)\" maxlength=\"15\">"
"<button id=\"setpin\" type=\"button\">Save PIN</button></details>"
"<pre id=\"authlog\" style=\"display:none\"></pre></div>"
"<div class=\"card\"><h2>Serial Terminal</h2>"
"<p style=\"font-size:13px\"><a href=\"/terminal\">Open web serial terminal</a> (UART1 bridge)</p></div>"
"<div class=\"card\"><h2>Firmware Update (OTA)</h2>"
"<input type=\"file\" id=\"fw\" accept=\".bin\">"
"<button id=\"up\">Upload &amp; Flash</button>"
"<pre id=\"otalog\"></pre></div>"
"<script>"
"function J(u,b){return fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b}).then(r=>r.text());}"
"function alog(t){var l=document.getElementById('authlog');l.style.display='block';l.textContent=t;}"
"fetch('/status').then(r=>r.json()).then(j=>{document.getElementById('st').textContent=JSON.stringify(j,null,2);"
"document.getElementById('lockst').textContent=j.auth?'PIN protection: ON':'PIN protection: OFF (anyone on the network can debug)';"
"}).catch(e=>{document.getElementById('st').textContent='error'});"
"document.getElementById('unlock').onclick=function(){"
"J('/unlock','pin='+encodeURIComponent(document.getElementById('pin').value)).then(alog).catch(e=>alog('error: '+e));};"
"document.getElementById('setpin').onclick=function(){"
"J('/pin','old='+encodeURIComponent(document.getElementById('oldpin').value)+'&new='+encodeURIComponent(document.getElementById('newpin').value)).then(alog).catch(e=>alog('error: '+e));};"
"document.getElementById('up').onclick=function(){"
"var f=document.getElementById('fw').files[0];var l=document.getElementById('otalog');"
"if(!f){l.textContent='select a .bin first';return;}"
"l.textContent='uploading '+f.size+' bytes...';"
"fetch('/ota',{method:'POST',body:f}).then(r=>r.text()).then(t=>{l.textContent=t;}).catch(e=>{l.textContent='OTA failed: '+e;});"
"};"
"</script></body></html>";

// ---------------------------------------------------------------------------
// Web serial terminal page (served at GET /terminal). Talks to /ws/uart.
// ---------------------------------------------------------------------------
static const char kTerminalHtml[] =
"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>ESProbe Terminal</title><style>"
"body{margin:0;background:#1e1e1e;color:#d4d4d4;font-family:Consolas,monospace;display:flex;flex-direction:column;height:100vh}"
"#bar{padding:6px 10px;background:#2b5797;color:#fff;font-size:13px;display:flex;gap:8px;align-items:center}"
"#out{flex:1;overflow-y:auto;padding:8px;white-space:pre-wrap;word-break:break-all;font-size:13px;margin:0}"
"#in{border:0;outline:0;background:#333;color:#d4d4d4;padding:8px;font:inherit}"
"select,button{font-size:12px}"
"</style></head><body>"
"<div id=\"bar\">ESProbe UART "
"<select id=\"baud\"><option>115200</option><option>921600</option><option>460800</option>"
"<option>230400</option><option>57600</option><option>9600</option></select>"
"<button id=\"conn\">Connect</button><span id=\"stat\">disconnected</span>"
"<a href=\"/\" style=\"color:#cde;margin-left:auto\">config</a></div>"
"<pre id=\"out\"></pre>"
"<input id=\"in\" placeholder=\"type and press Enter to send (CRLF appended)\" autocomplete=\"off\">"
"<script>"
"var ws=null,out=document.getElementById('out'),stat=document.getElementById('stat');"
"var dec=new TextDecoder();"
"function ap(t){out.textContent+=t;if(out.textContent.length>60000)out.textContent=out.textContent.slice(-40000);out.scrollTop=out.scrollHeight;}"
"document.getElementById('conn').onclick=function(){"
"if(ws){ws.close();return;}"
"ws=new WebSocket('ws://'+location.host+'/ws/uart');ws.binaryType='arraybuffer';"
"ws.onopen=function(){stat.textContent='connected';this.send(document.getElementById('baud').value);};"
"ws.onclose=function(){stat.textContent='disconnected';ws=null;};"
"ws.onerror=function(){stat.textContent='error';};"
"ws.onmessage=function(e){ap(dec.decode(e.data));};};"
"document.getElementById('in').addEventListener('keydown',function(e){"
"if(e.key==='Enter'&&ws){ws.send(this.value+'\\r\\n');this.value='';}});"
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

// Client IPv4 (network byte order) of an httpd request; 0 when unresolvable.
static uint32_t client_ip4_of_req(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    struct sockaddr_storage peer;
    socklen_t len = sizeof(peer);
    if (fd < 0 || getpeername(fd, (struct sockaddr *)&peer, &len) != 0)
        return 0;
    if (peer.ss_family == AF_INET)
        return ((struct sockaddr_in *)&peer)->sin_addr.s_addr;
#if LWIP_IPV6
    if (peer.ss_family == AF_INET6) {
        const uint8_t *b = (const uint8_t *)((struct sockaddr_in6 *)&peer)->sin6_addr.un.u8_addr;
        static const uint8_t v4map[12] = {0,0,0,0,0,0,0,0,0,0,0xFF,0xFF};
        if (memcmp(b, v4map, sizeof(v4map)) == 0) {
            uint32_t ip; memcpy(&ip, b + 12, 4); return ip;
        }
    }
#endif
    return 0;
}

// Read a small x-www-form-urlencoded body into buf (NUL-terminated).
static int read_form_body(httpd_req_t *req, char *buf, size_t buflen)
{
    int total = req->content_len < (int)buflen - 1 ? req->content_len : (int)buflen - 1;
    int received = 0;
    while (received < total) {
        int r = httpd_req_recv(req, buf + received, total - received);
        if (r <= 0)
            return -1;
        received += r;
    }
    buf[received] = '\0';
    return received;
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
        "\"uptime_s\":%lu,\"partition\":\"%s\",\"usbip_port\":3240,\"uart_port\":1234,"
        "\"auth\":%s}",
        ESPROBE_FW_VERSION,
        wifi_is_ap_mode() ? "ap" : "sta",
        ip_str, rssi, (unsigned long)uptime, part,
        access_control_has_pin() ? "true" : "false");

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
// POST /unlock — validate PIN, whitelist the client IP for debug ports
// ---------------------------------------------------------------------------
static esp_err_t unlock_post_handler(httpd_req_t *req)
{
    char buf[64];
    char pin[ACCESS_PIN_MAX_LEN] = {0};

    if (read_form_body(req, buf, sizeof(buf)) < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv failed");
        return ESP_FAIL;
    }
    httpd_query_key_value(buf, "pin", pin, sizeof(pin));
    url_decode(pin);

    if (!access_control_has_pin()) {
        httpd_resp_sendstr(req, "PIN protection is off; debug ports are open.");
        return ESP_OK;
    }

    uint32_t ip = client_ip4_of_req(req);
    int ret = access_control_unlock(ip, pin);
    if (ret == 0) {
        httpd_resp_sendstr(req, "Unlocked. This device can now use the debug/UART ports.");
    } else if (ret == -1) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "Wrong PIN.");
    } else {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "Session table full; reboot device or clear PIN.");
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// POST /pin — set or clear the access PIN
// ---------------------------------------------------------------------------
static esp_err_t pin_post_handler(httpd_req_t *req)
{
    char buf[96];
    char oldpin[ACCESS_PIN_MAX_LEN] = {0};
    char newpin[ACCESS_PIN_MAX_LEN] = {0};

    if (read_form_body(req, buf, sizeof(buf)) < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv failed");
        return ESP_FAIL;
    }
    httpd_query_key_value(buf, "old", oldpin, sizeof(oldpin));
    httpd_query_key_value(buf, "new", newpin, sizeof(newpin));
    url_decode(oldpin);
    url_decode(newpin);

    int ret = access_control_set_pin(newpin, oldpin);
    if (ret == 0) {
        httpd_resp_sendstr(req, newpin[0]
            ? "PIN saved. Debug ports now require unlock from each client."
            : "PIN cleared. Debug ports are open.");
    } else if (ret == -1) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "Wrong current PIN.");
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Failed to save PIN.");
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// GET /terminal — web serial terminal page
// ---------------------------------------------------------------------------
static esp_err_t terminal_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, kTerminalHtml, HTTPD_RESP_USE_STRLEN);
}

// ---------------------------------------------------------------------------
// /ws/uart — WebSocket <-> UART1 bridge
// ---------------------------------------------------------------------------
#ifdef CONFIG_HTTPD_WS_SUPPORT

static httpd_handle_t s_ws_server = NULL;
static _Atomic int s_ws_fd = -1;          // active terminal client, -1 = none
static _Atomic bool s_ws_first_msg = false; // first WS message = baudrate

// Poll UART RX and push to the WebSocket client until it disappears.
static void ws_uart_push_task(void *arg)
{
    static uint8_t buf[256]; // single consumer task, static per project style
    while (1) {
        int fd = atomic_load(&s_ws_fd);
        if (fd < 0)
            break;
        int n = uart_bridge_read(buf, sizeof(buf), 50);
        if (n > 0) {
            httpd_ws_frame_t frame = {
                .type = HTTPD_WS_TYPE_BINARY,
                .payload = buf,
                .len = (size_t)n,
            };
            if (httpd_ws_send_frame_async(s_ws_server, fd, &frame) != ESP_OK) {
                // Client gone; close_fn will finish the cleanup.
                break;
            }
        } else if (n < 0) {
            break; // bridge unavailable
        }
    }
    vTaskDelete(NULL);
}

static esp_err_t ws_uart_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) { // handshake completed
        if (access_control_has_pin() &&
            !access_control_is_allowed(client_ip4_of_req(req))) {
            ESP_LOGW(TAG, "WS terminal rejected by access control");
            return ESP_FAIL;
        }
        if (!uart_bridge_ws_take()) {
            ESP_LOGW(TAG, "WS terminal busy or UART bridge disabled");
            return ESP_FAIL;
        }
        atomic_store(&s_ws_first_msg, true);
        atomic_store(&s_ws_fd, httpd_req_to_sockfd(req));
        xTaskCreate(ws_uart_push_task, "ws_uart", 3072, NULL, 3, NULL);
        ESP_LOGI(TAG, "WS terminal connected");
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {0};
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0); // query length
    if (err != ESP_OK)
        return err;

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        return ESP_OK; // close_fn handles cleanup
    }

    if (frame.len == 0 || frame.len > 512)
        return ESP_OK;

    uint8_t payload[512];
    frame.payload = payload;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK)
        return err;

    // First message selects the baudrate (digits only), matching the
    // TCP bridge convention.
    if (atomic_exchange(&s_ws_first_msg, false)) {
        char tmp[8] = {0};
        if (frame.len < sizeof(tmp)) {
            memcpy(tmp, payload, frame.len);
            int baud = atoi(tmp);
            if (baud > 0 && baud < 2000000) {
                ESP_LOGI(TAG, "WS terminal baud: %d", baud);
                uart_bridge_set_baud(baud);
                return ESP_OK;
            }
        }
    }

    uart_bridge_write(payload, frame.len);
    return ESP_OK;
}

// Global socket-close hook: tidy up when the terminal client disconnects.
static void ws_close_fn(httpd_handle_t hd, int sockfd)
{
    int expected = sockfd;
    if (atomic_compare_exchange_strong(&s_ws_fd, &expected, -1)) {
        uart_bridge_ws_release();
        ESP_LOGI(TAG, "WS terminal disconnected");
    }
    close(sockfd);
}

#endif // CONFIG_HTTPD_WS_SUPPORT

// ---------------------------------------------------------------------------
// Server bootstrap
// ---------------------------------------------------------------------------
void web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;          // OTA path needs headroom
    config.lru_purge_enable = true;
    config.max_uri_handlers = 12;
#ifdef CONFIG_HTTPD_WS_SUPPORT
    config.close_fn = ws_close_fn;
#endif

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
    static const httpd_uri_t uri_unlock = {
        .uri = "/unlock", .method = HTTP_POST, .handler = unlock_post_handler };
    static const httpd_uri_t uri_pin = {
        .uri = "/pin", .method = HTTP_POST, .handler = pin_post_handler };
    static const httpd_uri_t uri_terminal = {
        .uri = "/terminal", .method = HTTP_GET, .handler = terminal_get_handler };
#ifdef CONFIG_HTTPD_WS_SUPPORT
    static const httpd_uri_t uri_ws_uart = {
        .uri = "/ws/uart", .method = HTTP_GET, .handler = ws_uart_handler,
        .is_websocket = true };
#endif

    httpd_register_uri_handler(server, &uri_root);
    httpd_register_uri_handler(server, &uri_save);
    httpd_register_uri_handler(server, &uri_status);
    httpd_register_uri_handler(server, &uri_ota);
    httpd_register_uri_handler(server, &uri_unlock);
    httpd_register_uri_handler(server, &uri_pin);
    httpd_register_uri_handler(server, &uri_terminal);
#ifdef CONFIG_HTTPD_WS_SUPPORT
    s_ws_server = server;
    httpd_register_uri_handler(server, &uri_ws_uart);
#endif

    ESP_LOGI(TAG, "Web server started on port 80");
}
