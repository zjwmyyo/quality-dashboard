// QQ 音乐播放器 UI：启动页、扫码登录页、列表页、播放页（深色主题，全新设计）。
#pragma once

#include "stdbool.h"
#include "lvgl.h"
#include <stdint.h>
#include "qqm_api.h"
#include "qqm_player.h"

LV_FONT_DECLARE(lv_font_cn16);

void qqm_ui_init(void);

// --- 启动页 ---
void ui_boot_show(void);
void ui_boot_status(const char *text, bool busy);

// --- 扫码登录页 ---
void ui_login_show(const char *server_url);
void ui_login_state(qqm_login_state_t state);

// --- 列表页（歌单 / 歌曲共用）---
// 标题与每行文本由回调提供，避免 UI 层持有数据。
typedef const char *(*ui_text_fn)(int index, void *ctx);
void ui_list_show(const char *title, int total, ui_text_fn main_fn,
                  ui_text_fn sub_fn, void *ctx);
void ui_list_set_cursor(int index);
int  ui_list_get_cursor(void);

// --- 播放页 ---
void ui_player_show(void);
void ui_player_tick(const qqm_player_status_t *st, const char *lyric);
void ui_player_set_cover(const uint8_t *rgb565le, int bytes);
