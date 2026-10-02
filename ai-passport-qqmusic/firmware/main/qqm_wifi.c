#include "qqm_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "qqm_wifi";
static EventGroupHandle_t s_evt;
static httpd_handle_t s_http;
static bool s_started, s_setup;
static char s_server[128], s_ap_name[33], s_ap_password[64];

static bool load_setting(const char *key, char *out, size_t cap)
{
    nvs_handle_t nvs;
    if (nvs_open("qqm_setup", NVS_READONLY, &nvs) != ESP_OK) return false;
    size_t len = cap;
    bool ok = nvs_get_str(nvs, key, out, &len) == ESP_OK && out[0] != 0;
    nvs_close(nvs);
    return ok;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED && !s_setup) {
        xEventGroupClearBits(s_evt, QQM_WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_evt, QQM_WIFI_CONNECTED_BIT);
    }
}

static int digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool form_value(const char *body, const char *key, char *out, size_t cap)
{
    size_t keylen = strlen(key);
    const char *p = body;
    while (*p) {
        const char *end = strchr(p, '&');
        if (!end) end = p + strlen(p);
        if ((size_t)(end - p) > keylen && !strncmp(p, key, keylen) && p[keylen] == '=') {
            const char *v = p + keylen + 1;
            size_t n = 0;
            while (v < end) {
                unsigned char c = (unsigned char)*v++;
                if (c == '+') c = ' ';
                else if (c == '%') {
                    if (end - v < 2) return false;
                    int hi = digit(v[0]), lo = digit(v[1]);
                    if (hi < 0 || lo < 0) return false;
                    c = (unsigned char)((hi << 4) | lo);
                    v += 2;
                }
                if (!c || n + 1 >= cap) return false;
                out[n++] = (char)c;
            }
            out[n] = 0;
            return true;
        }
        p = *end ? end + 1 : end;
    }
    return false;
}

static esp_err_t setup_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req,
        "<!doctype html><html lang='zh'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>AI Passport 音乐设置</title><style>body{font:18px system-ui;max-width:440px;margin:36px auto;padding:20px}"
        "input,button{box-sizing:border-box;width:100%;padding:12px;margin:8px 0 18px;font:inherit}</style>"
        "<h2>AI Passport 音乐设置</h2><p>填写与运行后端的电脑同一局域网的 2.4 GHz Wi-Fi。</p>"
        "<form method='post' action='/save'><label>Wi-Fi 名称<input name='ssid' maxlength='32' required></label>"
        "<label>Wi-Fi 密码<input name='password' type='password' maxlength='63'></label>"
        "<label>后端地址<input name='server' type='url' placeholder='http://192.168.1.100:3200' required></label>"
        "<button type='submit'>保存并连接</button></form><p>不要填写 localhost。</p></html>");
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
}

static esp_err_t save_config(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form size");
        return ESP_FAIL;
    }
    char body[513] = {0}, ssid[33] = {0}, password[64] = {0}, server[128] = {0};
    size_t total = 0;
    while (total < (size_t)req->content_len) {
        int n = httpd_req_recv(req, body + total, req->content_len - total);
        if (n <= 0) return ESP_FAIL;
        total += n;
    }
    if (!form_value(body, "ssid", ssid, sizeof(ssid)) || !ssid[0] ||
        !form_value(body, "password", password, sizeof(password)) ||
        !form_value(body, "server", server, sizeof(server)) ||
        (password[0] && strlen(password) < 8) ||
        strncmp(server, "http://", 7) || strlen(server) < 11 ||
        strchr(server + 7, '/') || strchr(server + 7, '@') ||
        strchr(server + 7, ' ') || server[strlen(server) - 1] == ':') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Check Wi-Fi and server address");
        return ESP_FAIL;
    }
    nvs_handle_t nvs;
    if (nvs_open("qqm_setup", NVS_READWRITE, &nvs) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save settings");
        return ESP_FAIL;
    }
    esp_err_t err = nvs_set_str(nvs, "ssid", ssid);
    if (err == ESP_OK) err = nvs_set_str(nvs, "password", password);
    if (err == ESP_OK) err = nvs_set_str(nvs, "server", server);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save settings");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req, "<meta charset='utf-8'><p>已保存，设备正在重新连接。请返回 AI Passport 查看结果。</p>");
    xTaskCreate(restart_task, "qqm_restart", 2048, NULL, 3, NULL);
    return ESP_OK;
}

esp_err_t qqm_wifi_start_setup(void)
{
    if (s_setup) return ESP_OK;
    if (!s_started) return ESP_ERR_INVALID_STATE;
    s_setup = true;
    esp_err_t stop_err = esp_wifi_stop();
    if (stop_err != ESP_OK && stop_err != ESP_ERR_WIFI_NOT_STARTED) return stop_err;
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(s_ap_name, sizeof(s_ap_name), "Passport-Music-%02X%02X", mac[4], mac[5]);
    snprintf(s_ap_password, sizeof(s_ap_password), "Music%02X%02X%02X%02X", mac[2], mac[3], mac[4], mac[5]);
    esp_netif_create_default_wifi_ap();
    wifi_config_t ap = {0};
    strncpy((char *)ap.ap.ssid, s_ap_name, sizeof(ap.ap.ssid) - 1);
    strncpy((char *)ap.ap.password, s_ap_password, sizeof(ap.ap.password) - 1);
    ap.ap.ssid_len = strlen(s_ap_name);
    ap.ap.max_connection = 1;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 2;
    ESP_ERROR_CHECK(httpd_start(&s_http, &cfg));
    httpd_uri_t page = { .uri = "/", .method = HTTP_GET, .handler = setup_page };
    httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = save_config };
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_http, &page));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_http, &save));
    ESP_LOGI(TAG, "设置热点已启动: %s", s_ap_name);
    return ESP_OK;
}

esp_err_t qqm_wifi_init(void)
{
    if (s_started) return ESP_OK;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    s_evt = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_event, NULL));
    s_started = true;
    char ssid[33] = {0}, password[64] = {0};
    bool saved = load_setting("ssid", ssid, sizeof(ssid)) &&
                 load_setting("server", s_server, sizeof(s_server));
    if (saved) load_setting("password", password, sizeof(password));
    else {
        strncpy(ssid, CONFIG_QQM_WIFI_SSID, sizeof(ssid) - 1);
        strncpy(password, CONFIG_QQM_WIFI_PASSWORD, sizeof(password) - 1);
        strncpy(s_server, CONFIG_QQM_SERVER_URL, sizeof(s_server) - 1);
    }
    if (!ssid[0] || !s_server[0]) return qqm_wifi_start_setup();
    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, password, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "正在连接 Wi-Fi: %s", ssid);
    return ESP_OK;
}

esp_err_t qqm_wifi_wait(uint32_t timeout_ms)
{
    if (!s_evt) return ESP_ERR_INVALID_STATE;
    if (s_setup) return ESP_ERR_INVALID_STATE;
    EventBits_t bits = xEventGroupWaitBits(s_evt, QQM_WIFI_CONNECTED_BIT,
                                           pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return (bits & QQM_WIFI_CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool qqm_wifi_is_connected(void)
{
    return s_evt && (xEventGroupGetBits(s_evt) & QQM_WIFI_CONNECTED_BIT);
}

bool qqm_wifi_needs_setup(void) { return s_setup; }
const char *qqm_wifi_server_url(void) { return s_server; }

void qqm_wifi_setup_hint(char *out, size_t cap)
{
    snprintf(out, cap, "手机连接热点:\n%s\n密码:%s\n浏览器打开 192.168.4.1", s_ap_name, s_ap_password);
}
