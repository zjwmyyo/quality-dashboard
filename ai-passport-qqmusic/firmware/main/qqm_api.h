// 后端 API 客户端：HTTP GET + JSON 归一化。所有数据结构与后端路由一一对应。
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define QQM_MAX_PLAYLISTS   48
#define QQM_MAX_SONGS       64
#define QQM_NAME_LEN        80
#define QQM_SINGER_LEN      64
#define QQM_ID_LEN          32

typedef struct {
    char id[QQM_ID_LEN];
    char name[QQM_NAME_LEN];
    int  count;
} qqm_playlist_t;

typedef struct {
    char mid[QQM_ID_LEN];
    char name[QQM_NAME_LEN];
    char singer[QQM_SINGER_LEN];
    char albummid[QQM_ID_LEN];
    int  duration;      // 秒
} qqm_song_t;

typedef struct {
    int t;
    char text[56];
} qqm_lyric_line_t;

#define QQM_MAX_LYRIC_LINES 24
#define QQM_COVER_W 64
#define QQM_COVER_H 64
#define QQM_COVER_BYTES (QQM_COVER_W * QQM_COVER_H * 2)

typedef enum {
    QQM_LOGIN_UNKNOWN = 0,
    QQM_LOGIN_WAITING,
    QQM_LOGIN_SCANNED,
    QQM_LOGIN_CONFIRMED,
} qqm_login_state_t;

// 同步拉取登录状态（内部轮询二维码会话）。
esp_err_t qqm_api_login_state(qqm_login_state_t *out);

// 拉取“我的歌单”，写入 out 数组，*count 返回数量。
esp_err_t qqm_api_playlists(qqm_playlist_t *out, int max, int *count);

// 拉取某歌单全部歌曲（内部自动分页）。
esp_err_t qqm_api_playlist_songs(const char *playlist_id, qqm_song_t *out, int max, int *count);

// 拉取当前歌曲歌词，最多返回 QQM_MAX_LYRIC_LINES 行。
esp_err_t qqm_api_lyric(const char *mid, qqm_lyric_line_t *out, int max, int *count);

// 拉取 64x64 RGB565LE 专辑封面，out 至少 QQM_COVER_BYTES。
esp_err_t qqm_api_cover_raw(const char *albummid, uint8_t *out, int cap, int *bytes);

// 拼接音频流地址到 out_buf（/api/stream?mid=xxx）。
void qqm_api_stream_url(const char *mid, char *out_buf, int buf_len);

// 后端服务器根地址（CONFIG_QQM_SERVER_URL）。
const char *qqm_api_server(void);
