#include "qqm_api.h"
#include "qqm_wifi.h"

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "qqm_api";

#define RECV_BUF_LEN  49152   // 歌单/歌曲 JSON 最大缓冲（浏览与播放错峰，内存可复用）

typedef struct {
    char *buf;
    int   cap;
    int   len;
    bool  overflow;
} rx_ctx_t;

static esp_err_t on_http(esp_http_client_event_t *evt)
{
    rx_ctx_t *rx = (rx_ctx_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && rx) {
        int n = evt->data_len;
        if (rx->len + n >= rx->cap) {
            n = rx->cap - 1 - rx->len;
            rx->overflow = true;
        }
        if (n > 0) {
            memcpy(rx->buf + rx->len, evt->data, n);
            rx->len += n;
            rx->buf[rx->len] = '\0';
        }
    }
    return ESP_OK;
}

// GET path（以 / 开头），返回 0 终止的响应体。调用方提供 buf。
static esp_err_t http_get(const char *path, char *buf, int cap, int timeout_ms)
{
    char url[256];
    snprintf(url, sizeof(url), "%s%s", qqm_wifi_server_url(), path);

    rx_ctx_t rx = { .buf = buf, .cap = cap, .len = 0, .overflow = false };
    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = on_http,
        .user_data = &rx,
        .timeout_ms = timeout_ms,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
        .disable_auto_redirect = false,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_ERR_NO_MEM;

    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GET %s 失败: %s", path, esp_err_to_name(err));
        return err;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "GET %s HTTP %d", path, status);
        return ESP_FAIL;
    }
    buf[rx.len] = '\0';
    if (rx.overflow) ESP_LOGW(TAG, "响应过大已截断: %s", path);
    return ESP_OK;
}

static void copy_str(char *dst, int cap, const char *src)
{
    if (!src) { dst[0] = '\0'; return; }
    int n = 0;
    while (*src && n + 1 < cap) {
        unsigned char lead = (unsigned char)*src;
        int bytes = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        if (n + bytes >= cap) break;
        for (int i = 0; i < bytes && *src; i++) dst[n++] = *src++;
    }
    dst[n] = '\0';
}

const char *qqm_api_server(void) { return qqm_wifi_server_url(); }

esp_err_t qqm_api_login_state(qqm_login_state_t *out)
{
    char *buf = malloc(RECV_BUF_LEN);
    if (!buf) return ESP_ERR_NO_MEM;
    esp_err_t err = http_get("/api/qrcode/state", buf, RECV_BUF_LEN, 20000);
    if (err == ESP_OK) {
        cJSON *root = cJSON_Parse(buf);
        cJSON *st = root ? cJSON_GetObjectItem(root, "state") : NULL;
        *out = QQM_LOGIN_UNKNOWN;
        if (st && cJSON_IsString(st)) {
            if (!strcmp(st->valuestring, "confirmed")) *out = QQM_LOGIN_CONFIRMED;
            else if (!strcmp(st->valuestring, "scanned")) *out = QQM_LOGIN_SCANNED;
            else *out = QQM_LOGIN_WAITING;
        }
        if (root) cJSON_Delete(root);
        if (*out == QQM_LOGIN_UNKNOWN) err = ESP_FAIL;
    }
    free(buf);
    return err;
}

esp_err_t qqm_api_playlists(qqm_playlist_t *out, int max, int *count)
{
    char *buf = malloc(RECV_BUF_LEN);
    if (!buf) return ESP_ERR_NO_MEM;
    *count = 0;
    esp_err_t err = http_get("/api/playlists", buf, RECV_BUF_LEN, 15000);
    if (err == ESP_OK) {
        cJSON *root = cJSON_Parse(buf);
        cJSON *list = root ? cJSON_GetObjectItem(root, "list") : NULL;
        int n = 0;
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, list) {
            if (n >= max) break;
            cJSON *id = cJSON_GetObjectItem(it, "id");
            cJSON *name = cJSON_GetObjectItem(it, "name");
            cJSON *cnt = cJSON_GetObjectItem(it, "count");
            if (!id || !cJSON_IsString(id)) continue;
            copy_str(out[n].id, sizeof(out[n].id), id->valuestring);
            copy_str(out[n].name, sizeof(out[n].name),
                     name && cJSON_IsString(name) ? name->valuestring : "歌单");
            out[n].count = cnt ? cnt->valueint : 0;
            n++;
        }
        *count = n;
        if (root) cJSON_Delete(root);
        ESP_LOGI(TAG, "歌单 %d 个", n);
    }
    free(buf);
    return err;
}

esp_err_t qqm_api_playlist_songs(const char *playlist_id, qqm_song_t *out, int max, int *count)
{
    char path[160];
    snprintf(path, sizeof(path), "/api/playlist?id=%s", playlist_id);
    char *buf = malloc(RECV_BUF_LEN);
    if (!buf) return ESP_ERR_NO_MEM;
    *count = 0;
    esp_err_t err = http_get(path, buf, RECV_BUF_LEN, 15000);
    if (err == ESP_OK) {
        cJSON *root = cJSON_Parse(buf);
        cJSON *list = root ? cJSON_GetObjectItem(root, "list") : NULL;
        int n = 0;
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, list) {
            if (n >= max) break;
            cJSON *mid = cJSON_GetObjectItem(it, "mid");
            cJSON *name = cJSON_GetObjectItem(it, "name");
            cJSON *singer = cJSON_GetObjectItem(it, "singer");
            cJSON *albummid = cJSON_GetObjectItem(it, "albummid");
            cJSON *dur = cJSON_GetObjectItem(it, "duration");
            if (!mid || !cJSON_IsString(mid)) continue;
            copy_str(out[n].mid, sizeof(out[n].mid), mid->valuestring);
            copy_str(out[n].name, sizeof(out[n].name),
                     name && cJSON_IsString(name) ? name->valuestring : "未知歌曲");
            copy_str(out[n].singer, sizeof(out[n].singer),
                     singer && cJSON_IsString(singer) ? singer->valuestring : "");
            copy_str(out[n].albummid, sizeof(out[n].albummid),
                     albummid && cJSON_IsString(albummid) ? albummid->valuestring : "");
            out[n].duration = dur ? dur->valueint : 0;
            n++;
        }
        *count = n;
        ESP_LOGI(TAG, "歌曲 %d 首", n);
        if (root) cJSON_Delete(root);
    }
    free(buf);
    return err;
}


esp_err_t qqm_api_lyric(const char *mid, qqm_lyric_line_t *out, int max, int *count)
{
    char path[96];
    snprintf(path, sizeof(path), "/api/lyric?mid=%s", mid);
    char *buf = malloc(4096);
    if (!buf) return ESP_ERR_NO_MEM;
    *count = 0;
    esp_err_t err = http_get(path, buf, 4096, 12000);
    if (err == ESP_OK) {
        cJSON *root = cJSON_Parse(buf);
        cJSON *list = root ? cJSON_GetObjectItem(root, "lines") : NULL;
        int n = 0;
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, list) {
            if (n >= max) break;
            cJSON *t = cJSON_GetObjectItem(it, "t");
            cJSON *text = cJSON_GetObjectItem(it, "text");
            if (!text || !cJSON_IsString(text)) continue;
            out[n].t = t ? t->valueint : 0;
            copy_str(out[n].text, sizeof(out[n].text), text->valuestring);
            n++;
        }
        *count = n;
        ESP_LOGI(TAG, "歌词 %d 行", n);
        if (root) cJSON_Delete(root);
    }
    free(buf);
    return err;
}



esp_err_t qqm_api_cover_raw(const char *albummid, uint8_t *out, int cap, int *bytes)
{
    if (bytes) *bytes = 0;
    if (!albummid || !albummid[0] || !out || cap < QQM_COVER_BYTES) return ESP_ERR_INVALID_ARG;
    char url[256];
    snprintf(url, sizeof(url), "%s/api/cover.raw?albummid=%s", qqm_wifi_server_url(), albummid);
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 12000,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
        .disable_auto_redirect = false,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_http_client_open(c, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "封面打开失败: %s", esp_err_to_name(err));
        esp_http_client_cleanup(c);
        return err;
    }
    esp_http_client_fetch_headers(c);
    int status = esp_http_client_get_status_code(c);
    int total = 0;
    while (total < QQM_COVER_BYTES) {
        int n = esp_http_client_read(c, (char *)out + total, QQM_COVER_BYTES - total);
        if (n < 0) { err = ESP_FAIL; break; }
        if (n == 0) break;
        total += n;
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    if (status != 200 || total != QQM_COVER_BYTES) {
        ESP_LOGW(TAG, "封面失败 HTTP %d bytes=%d", status, total);
        return ESP_FAIL;
    }
    if (bytes) *bytes = total;
    ESP_LOGI(TAG, "封面 %d 字节", total);
    return ESP_OK;
}

void qqm_api_stream_url(const char *mid, char *out_buf, int buf_len)
{
    snprintf(out_buf, buf_len, "%s/api/pcm?mid=%s", qqm_wifi_server_url(), mid);
}
