#include "bsp_audio.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "qqm_api.h"
#include "qqm_player.h"
#include "qqm_ui.h"
#include "qqm_wifi.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "qq_music";

typedef enum { PAGE_BOOT, PAGE_LOGIN, PAGE_PLAYLISTS, PAGE_SONGS, PAGE_PLAYER } page_t;
typedef struct { bsp_btn_t btn; bsp_btn_ev_t ev; } key_msg_t;

static QueueHandle_t s_keyq;
static page_t s_page;
static qqm_playlist_t s_playlists[QQM_MAX_PLAYLISTS];
static qqm_song_t s_songs[QQM_MAX_SONGS];
static qqm_lyric_line_t s_lyrics[QQM_MAX_LYRIC_LINES];
static int s_playlist_count;
static int s_song_count;
static int s_lyric_count;
static int s_playlist_cursor;
static int s_song_cursor;
static uint8_t *s_cover_buf;

static const char *playlist_main(int index, void *ctx)
{
    (void)ctx;
    return s_playlists[index].name;
}

static const char *playlist_sub(int index, void *ctx)
{
    (void)ctx;
    static char buf[24];
    snprintf(buf, sizeof(buf), "%d 首", s_playlists[index].count);
    return buf;
}

static const char *song_main(int index, void *ctx)
{
    (void)ctx;
    return s_songs[index].name;
}

static const char *song_sub(int index, void *ctx)
{
    (void)ctx;
    return s_songs[index].singer;
}

static const char *lyric_for_elapsed(int elapsed)
{
    const char *res = "";
    for (int i = 0; i < s_lyric_count; i++) {
        if (s_lyrics[i].t <= elapsed) res = s_lyrics[i].text;
        else break;
    }
    return res;
}

static void key_cb(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    key_msg_t msg = { .btn = btn, .ev = ev };
    if (s_keyq) xQueueSend(s_keyq, &msg, 0);
}

static void show_playlists(void)
{
    s_page = PAGE_PLAYLISTS;
    if (bsp_lvgl_lock(1000)) {
        ui_list_show("我的歌单", s_playlist_count, playlist_main, playlist_sub, NULL);
        ui_list_set_cursor(s_playlist_cursor);
        bsp_lvgl_unlock();
    }
}

static void show_songs(void)
{
    s_page = PAGE_SONGS;
    if (bsp_lvgl_lock(1000)) {
        ui_list_show("歌单歌曲", s_song_count, song_main, song_sub, NULL);
        ui_list_set_cursor(s_song_cursor);
        bsp_lvgl_unlock();
    }
}

static void load_playlists(void)
{
    ui_boot_status("正在读取歌单…", true);
    int count = 0;
    esp_err_t err = qqm_api_playlists(s_playlists, QQM_MAX_PLAYLISTS, &count);
    if (err == ESP_OK && count > 0) {
        s_playlist_count = count;
        s_playlist_cursor = 0;
        show_playlists();
    } else {
        ESP_LOGW(TAG, "读取歌单失败: %s count=%d", esp_err_to_name(err), count);
        ui_boot_status("歌单读取失败，OK 重试", false);
        s_page = PAGE_BOOT;
    }
}

static void load_songs(int playlist_index)
{
    if (playlist_index < 0 || playlist_index >= s_playlist_count) return;
    if (bsp_lvgl_lock(1000)) { ui_boot_show(); bsp_lvgl_unlock(); }
    ui_boot_status("正在读取歌曲…", true);
    int count = 0;
    esp_err_t err = qqm_api_playlist_songs(s_playlists[playlist_index].id, s_songs, QQM_MAX_SONGS, &count);
    if (err == ESP_OK && count > 0) {
        s_song_count = count;
        s_song_cursor = 0;
        show_songs();
    } else {
        ESP_LOGW(TAG, "读取歌曲失败: %s count=%d", esp_err_to_name(err), count);
        ui_boot_status("歌曲读取失败，长按返回", false);
        s_page = PAGE_BOOT;
    }
}

static void update_cover_for(int song_index)
{
    if (!s_cover_buf || song_index < 0 || song_index >= s_song_count) return;
    int bytes = 0;
    esp_err_t err = qqm_api_cover_raw(s_songs[song_index].albummid, s_cover_buf, QQM_COVER_BYTES, &bytes);
    if (err == ESP_OK && bytes == QQM_COVER_BYTES) ui_player_set_cover(s_cover_buf, bytes);
    else ESP_LOGW(TAG, "封面读取失败: %s bytes=%d", esp_err_to_name(err), bytes);
}

static void start_song(int song_index)
{
    if (song_index < 0 || song_index >= s_song_count) return;
    s_song_cursor = song_index;
    s_lyric_count = 0;
    qqm_api_lyric(s_songs[song_index].mid, s_lyrics, QQM_MAX_LYRIC_LINES, &s_lyric_count);
    if (bsp_lvgl_lock(1000)) { ui_player_show(); bsp_lvgl_unlock(); }
    s_page = PAGE_PLAYER;
    qqm_player_start(s_songs, s_song_count, song_index);
    update_cover_for(song_index);
}

static void refresh_player(void)
{
    if (s_page != PAGE_PLAYER) return;
    qqm_player_status_t st;
    qqm_player_get_status(&st);
    if (st.index >= 0 && st.index < s_song_count && st.index != s_song_cursor) {
        s_song_cursor = st.index;
        s_lyric_count = 0;
        qqm_api_lyric(s_songs[s_song_cursor].mid, s_lyrics, QQM_MAX_LYRIC_LINES, &s_lyric_count);
        update_cover_for(s_song_cursor);
    }
    ui_player_tick(&st, lyric_for_elapsed(st.elapsed));
}

static void handle_key(const key_msg_t *msg)
{
    if (msg->ev == BSP_BTN_PRESS) return;
    if (s_page == PAGE_BOOT && msg->btn == BSP_BTN_OK && msg->ev == BSP_BTN_CLICK) {
        if (qqm_wifi_needs_setup()) {
            char hint[128];
            qqm_wifi_setup_hint(hint, sizeof(hint));
            ui_boot_status(hint, false);
        } else {
            load_playlists();
        }
        return;
    }
    if (s_page == PAGE_LOGIN && msg->btn == BSP_BTN_OK && msg->ev == BSP_BTN_CLICK) {
        qqm_login_state_t st = QQM_LOGIN_UNKNOWN;
        if (qqm_api_login_state(&st) == ESP_OK) ui_login_state(st);
        if (st == QQM_LOGIN_CONFIRMED) load_playlists();
        return;
    }
    if (s_page == PAGE_PLAYLISTS) {
        if (msg->ev == BSP_BTN_CLICK && msg->btn == BSP_BTN_UP) {
            s_playlist_cursor = (s_playlist_cursor + s_playlist_count - 1) % s_playlist_count;
            if (bsp_lvgl_lock(500)) { ui_list_set_cursor(s_playlist_cursor); bsp_lvgl_unlock(); }
        } else if (msg->ev == BSP_BTN_CLICK && msg->btn == BSP_BTN_DOWN) {
            s_playlist_cursor = (s_playlist_cursor + 1) % s_playlist_count;
            if (bsp_lvgl_lock(500)) { ui_list_set_cursor(s_playlist_cursor); bsp_lvgl_unlock(); }
        } else if (msg->ev == BSP_BTN_CLICK && msg->btn == BSP_BTN_OK) {
            load_songs(s_playlist_cursor);
        }
        return;
    }
    if (s_page == PAGE_SONGS) {
        if (msg->ev == BSP_BTN_LONG && msg->btn == BSP_BTN_OK) {
            show_playlists();
        } else if (msg->ev == BSP_BTN_CLICK && msg->btn == BSP_BTN_UP) {
            s_song_cursor = (s_song_cursor + s_song_count - 1) % s_song_count;
            if (bsp_lvgl_lock(500)) { ui_list_set_cursor(s_song_cursor); bsp_lvgl_unlock(); }
        } else if (msg->ev == BSP_BTN_CLICK && msg->btn == BSP_BTN_DOWN) {
            s_song_cursor = (s_song_cursor + 1) % s_song_count;
            if (bsp_lvgl_lock(500)) { ui_list_set_cursor(s_song_cursor); bsp_lvgl_unlock(); }
        } else if (msg->ev == BSP_BTN_CLICK && msg->btn == BSP_BTN_OK) {
            start_song(s_song_cursor);
        }
        return;
    }
    if (s_page == PAGE_PLAYER) {
        if (msg->ev == BSP_BTN_LONG && msg->btn == BSP_BTN_OK) {
            qqm_player_stop();
            show_songs();
        } else if (msg->ev == BSP_BTN_CLICK && msg->btn == BSP_BTN_OK) {
            qqm_player_toggle_pause();
        } else if (msg->ev == BSP_BTN_CLICK && msg->btn == BSP_BTN_UP) {
            qqm_player_prev();
        } else if (msg->ev == BSP_BTN_CLICK && msg->btn == BSP_BTN_DOWN) {
            qqm_player_next();
        } else if (msg->ev == BSP_BTN_LONG && msg->btn == BSP_BTN_UP) {
            qqm_player_volume_delta(10);
        } else if (msg->ev == BSP_BTN_LONG && msg->btn == BSP_BTN_DOWN) {
            qqm_player_volume_delta(-10);
        }
    }
}

int app_main(void)
{
    ESP_LOGI(TAG, "QQ Music Passport starting");
    s_keyq = xQueueCreate(12, sizeof(key_msg_t));
    s_cover_buf = heap_caps_malloc(QQM_COVER_BYTES, MALLOC_CAP_8BIT);

    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(bsp_display_init());
    ESP_ERROR_CHECK(bsp_lvgl_init() == NULL ? ESP_FAIL : ESP_OK);
    bsp_display_backlight(100);
    if (bsp_lvgl_lock(1000)) { ui_boot_show(); bsp_lvgl_unlock(); }
    ui_boot_status("初始化播放器…", true);
    qqm_player_module_init();
    ESP_ERROR_CHECK(bsp_button_init(key_cb, NULL));

    ui_boot_status("正在连接 Wi-Fi…", true);
    ESP_ERROR_CHECK(qqm_wifi_init());
    if (qqm_wifi_needs_setup()) {
        char hint[128];
        qqm_wifi_setup_hint(hint, sizeof(hint));
        ui_boot_status(hint, false);
        s_page = PAGE_BOOT;
    } else if (qqm_wifi_wait(20000) != ESP_OK) {
        ESP_ERROR_CHECK(qqm_wifi_start_setup());
        char hint[128];
        qqm_wifi_setup_hint(hint, sizeof(hint));
        ui_boot_status(hint, false);
        s_page = PAGE_BOOT;
    } else {
        ui_boot_status("正在检查登录…", true);
        qqm_login_state_t st = QQM_LOGIN_UNKNOWN;
        if (qqm_api_login_state(&st) == ESP_OK && st == QQM_LOGIN_CONFIRMED) {
            load_playlists();
        } else {
            if (bsp_lvgl_lock(1000)) { ui_login_show(qqm_api_server()); bsp_lvgl_unlock(); }
            ui_login_state(st);
            s_page = PAGE_LOGIN;
        }
    }

    TickType_t last = xTaskGetTickCount();
    for (;;) {
        key_msg_t msg;
        if (xQueueReceive(s_keyq, &msg, pdMS_TO_TICKS(100)) == pdTRUE) handle_key(&msg);
        TickType_t now = xTaskGetTickCount();
        if (now - last >= pdMS_TO_TICKS(500)) {
            last = now;
            refresh_player();
        }
    }
}
