#include "qqm_ui.h"

#include "bsp_display.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define COL_BG      0x101316   // 近黑背景
#define COL_CARD    0x1A1F26   // 卡片
#define COL_GREEN   0x31C27C   // QQ 音乐绿
#define COL_GREEN_D 0x1F7A50
#define COL_WHITE   0xF2F5F7
#define COL_GRAY    0x8A94A3
#define COL_DARK    0x0C0E11

#define ROW_H       52
#define LIST_TOP    42
#define LIST_BOTTOM 266
#define ROWS_VIS    ((LIST_BOTTOM - LIST_TOP) / ROW_H)

static lv_obj_t *s_scr;

// 各页面控件
static lv_obj_t *s_boot_status;
static lv_obj_t *s_boot_spinner;

static lv_obj_t *s_login_state;

static lv_obj_t *s_list;
static lv_obj_t *s_rows[ROWS_VIS];
static lv_obj_t *s_row_main[ROWS_VIS];
static lv_obj_t *s_row_sub[ROWS_VIS];
static lv_obj_t *s_list_title;
static lv_obj_t *s_list_hint;
static int s_list_total;
static int s_cursor;
static ui_text_fn s_main_fn, s_sub_fn;
static void *s_list_ctx;

static lv_obj_t *s_p_name, *s_p_singer, *s_p_time, *s_p_bar, *s_p_bar_ind,
               *s_p_state, *s_p_volume, *s_p_disc_note, *s_p_lyric, *s_p_cover;
static lv_image_dsc_t s_cover_dsc;
static lv_font_t s_multilingual_font;
static bool s_multilingual_font_ready;
LV_FONT_DECLARE(lv_font_ko16);
static char s_last_name[96];
static char s_last_singer[96];
static char s_last_lyric[128];

static const lv_font_t *ui_text_font(void)
{
    if (!s_multilingual_font_ready) {
        s_multilingual_font = lv_font_cn16;
        s_multilingual_font.fallback = &lv_font_ko16;
        s_multilingual_font_ready = true;
    }
    return &s_multilingual_font;
}

static void text_single_line(char *out, size_t capacity, const char *in)
{
    size_t n = 0;
    if (!in) in = "";
    while (*in && n + 1 < capacity) {
        unsigned char c = (unsigned char)*in;
        if (c == '\n' || c == '\r' || c == '\t') {
            out[n++] = ' ';
            in++;
            continue;
        }
        size_t len = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        if (n + len >= capacity) break;
        for (size_t i = 0; i < len && *in; i++) out[n++] = *in++;
    }
    out[n] = 0;
}

// ---------------------------------------------------------------------------
static void reset_page_refs(void)
{
    s_boot_status = NULL;
    s_boot_spinner = NULL;
    s_login_state = NULL;
    s_list = NULL;
    s_list_title = NULL;
    s_list_hint = NULL;
    for (int i = 0; i < ROWS_VIS; i++) {
        s_rows[i] = NULL;
        s_row_main[i] = NULL;
        s_row_sub[i] = NULL;
    }
    s_p_name = NULL;
    s_p_singer = NULL;
    s_p_time = NULL;
    s_p_bar = NULL;
    s_p_bar_ind = NULL;
    s_p_state = NULL;
    s_p_volume = NULL;
    s_p_disc_note = NULL;
    s_p_lyric = NULL;
    s_p_cover = NULL;
    s_last_name[0] = 0;
    s_last_singer[0] = 0;
    s_last_lyric[0] = 0;
}


static lv_obj_t *base_screen(void)
{
    reset_page_refs();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

static lv_obj_t *mk_label(lv_obj_t *parent, lv_align_t align, int x, int y,
                          int w, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, ui_text_font(), 0);
    lv_obj_set_style_text_color(l, color, 0);
    if (w > 0) {
        lv_obj_set_width(l, w);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    }
    lv_obj_align(l, align, x, y);
    return l;
}

static void load(lv_obj_t *scr)
{
    if (s_scr) lv_obj_delete(s_scr);
    s_scr = scr;
    lv_screen_load(s_scr);
}

// 顶部标题栏
static lv_obj_t *header(lv_obj_t *scr, const char *title)
{
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 240, 40);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COL_DARK), 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *accent = lv_obj_create(bar);
    lv_obj_remove_style_all(accent);
    lv_obj_set_size(accent, 4, 22);
    lv_obj_align(accent, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(accent, 2, 0);
    lv_obj_set_style_bg_color(accent, lv_color_hex(COL_GREEN), 0);

    lv_obj_t *t = mk_label(bar, LV_ALIGN_LEFT_MID, 24, 0, 200,
                           lv_color_hex(COL_WHITE));
    lv_label_set_text(t, title);
    return bar;
}

static void bottom_hint(lv_obj_t *scr, const char *text)
{
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 240, 38);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COL_DARK), 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = mk_label(bar, LV_ALIGN_CENTER, 0, 0, 232,
                           lv_color_hex(COL_GRAY));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, text);
}

// ---------------------------------------------------------------------------
// 启动页
// ---------------------------------------------------------------------------
void ui_boot_show(void)
{
    lv_obj_t *scr = base_screen();

    // logo：绿色圆角方块 + 音符
    lv_obj_t *logo = lv_obj_create(scr);
    lv_obj_remove_style_all(logo);
    lv_obj_set_size(logo, 92, 92);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -40);
    lv_obj_set_style_bg_opa(logo, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(logo, 22, 0);
    lv_obj_set_style_bg_color(logo, lv_color_hex(COL_GREEN), 0);
    lv_obj_clear_flag(logo, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *note = mk_label(logo, LV_ALIGN_CENTER, 0, -2, 0, lv_color_hex(COL_DARK));
    lv_obj_set_style_text_font(note, &lv_font_montserrat_20, 0);
    lv_label_set_text(note, LV_SYMBOL_AUDIO);

    lv_obj_t *name = mk_label(scr, LV_ALIGN_CENTER, 0, 30, 200,
                              lv_color_hex(COL_WHITE));
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(name, "QQ 音乐 · Passport");

    s_boot_spinner = lv_spinner_create(scr);
    lv_obj_set_size(s_boot_spinner, 26, 26);
    lv_obj_align(s_boot_spinner, LV_ALIGN_CENTER, 0, 78);
    lv_obj_set_style_arc_color(s_boot_spinner, lv_color_hex(COL_GREEN),
                               LV_PART_INDICATOR);

    s_boot_status = mk_label(scr, LV_ALIGN_CENTER, 0, 118, 220,
                             lv_color_hex(COL_GRAY));
    lv_obj_set_style_text_align(s_boot_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_boot_status, "启动中…");
    load(scr);
}

void ui_boot_status(const char *text, bool busy)
{
    if (!bsp_lvgl_lock(500)) return;
    if (s_boot_status) lv_label_set_text(s_boot_status, text);
    if (s_boot_spinner) {
        if (busy) lv_obj_clear_flag(s_boot_spinner, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_boot_spinner, LV_OBJ_FLAG_HIDDEN);
    }
    bsp_lvgl_unlock();
}

// ---------------------------------------------------------------------------
// 扫码登录页
// ---------------------------------------------------------------------------
void ui_login_show(const char *server_url)
{
    lv_obj_t *scr = base_screen();
    header(scr, "扫码登录");

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 216, 132);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *steps = mk_label(card, LV_ALIGN_TOP_LEFT, 14, 12, 190,
                               lv_color_hex(COL_WHITE));
    lv_label_set_long_mode(steps, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(steps, 74);
    lv_label_set_text(steps,
        "1. 电脑/手机浏览器打开:\n2. 用 QQ 音乐 App 扫码登录");

    lv_obj_t *url = mk_label(card, LV_ALIGN_BOTTOM_LEFT, 14, -10, 190,
                             lv_color_hex(COL_GREEN));
    lv_obj_set_style_text_font(url, &lv_font_montserrat_14, 0);
    lv_label_set_text(url, server_url);

    s_login_state = mk_label(scr, LV_ALIGN_TOP_MID, 0, 196, 220,
                             lv_color_hex(COL_GRAY));
    lv_obj_set_style_text_align(s_login_state, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_login_state, "等待扫码…");

    bottom_hint(scr, "OK: 刷新状态");
    load(scr);
}

void ui_login_state(qqm_login_state_t state)
{
    if (!bsp_lvgl_lock(500)) return;
    if (!s_login_state) { bsp_lvgl_unlock(); return; }
    switch (state) {
    case QQM_LOGIN_WAITING:
        lv_label_set_text(s_login_state, "等待扫码…");
        lv_obj_set_style_text_color(s_login_state, lv_color_hex(COL_GRAY), 0);
        break;
    case QQM_LOGIN_SCANNED:
        lv_label_set_text(s_login_state, "已扫码，请在手机上确认");
        lv_obj_set_style_text_color(s_login_state, lv_color_hex(0xF5C542), 0);
        break;
    case QQM_LOGIN_CONFIRMED:
        lv_label_set_text(s_login_state, "登录成功，正在加载歌单…");
        lv_obj_set_style_text_color(s_login_state, lv_color_hex(COL_GREEN), 0);
        break;
    default:
        lv_label_set_text(s_login_state, "连接服务器中…");
        break;
    }
    bsp_lvgl_unlock();
}

// ---------------------------------------------------------------------------
// 列表页
// ---------------------------------------------------------------------------
static void refresh_rows(void)
{
    int top = s_cursor - (s_cursor % ROWS_VIS);
    char buf[112];
    for (int i = 0; i < ROWS_VIS; i++) {
        int idx = top + i;
        bool valid = idx < s_list_total;
        bool sel = idx == s_cursor;
        lv_obj_t *row = s_rows[i];
        if (!valid) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(row,
            lv_color_hex(sel ? COL_GREEN : COL_CARD), 0);

        const char *m = s_main_fn ? s_main_fn(idx, s_list_ctx) : "";
        const char *sub = s_sub_fn ? s_sub_fn(idx, s_list_ctx) : "";
        lv_label_set_text(s_row_main[i], m ? m : "");
        if (s_row_sub[i]) {
            if (sub && sub[0]) {
                snprintf(buf, sizeof(buf), "%s", sub);
                lv_label_set_text(s_row_sub[i], buf);
                lv_obj_clear_flag(s_row_sub[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_row_sub[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        lv_obj_set_style_text_color(s_row_main[i],
            lv_color_hex(sel ? COL_DARK : COL_WHITE), 0);
        if (s_row_sub[i])
            lv_obj_set_style_text_color(s_row_sub[i],
                lv_color_hex(sel ? COL_DARK : COL_GRAY), 0);
    }
}

void ui_list_show(const char *title, int total, ui_text_fn main_fn,
                  ui_text_fn sub_fn, void *ctx)
{
    lv_obj_t *scr = base_screen();
    header(scr, title);
    s_list_total = total;
    s_cursor = 0;
    s_main_fn = main_fn;
    s_sub_fn = sub_fn;
    s_list_ctx = ctx;

    for (int i = 0; i < ROWS_VIS; i++) {
        lv_obj_t *row = lv_obj_create(scr);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 232, ROW_H - 5);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, LIST_TOP + i * ROW_H);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(COL_CARD), 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        s_row_main[i] = mk_label(row, LV_ALIGN_TOP_LEFT, 10, 4, 212,
                                 lv_color_hex(COL_WHITE));
        s_row_sub[i] = mk_label(row, LV_ALIGN_TOP_LEFT, 10, 27, 212,
                                lv_color_hex(COL_GRAY));
        lv_obj_set_style_text_font(s_row_sub[i], ui_text_font(), 0);
        s_rows[i] = row;
    }
    bottom_hint(scr, "上下选择  OK进入  长按返回");
    refresh_rows();
    load(scr);
}

void ui_list_set_cursor(int index)
{
    if (index < 0) index = 0;
    if (index >= s_list_total) index = s_list_total - 1;
    s_cursor = index;
    if (bsp_lvgl_lock(500)) {
        refresh_rows();
        bsp_lvgl_unlock();
    }
}

int ui_list_get_cursor(void) { return s_cursor; }

// ---------------------------------------------------------------------------
// 播放页
// ---------------------------------------------------------------------------
void ui_player_show(void)
{
    lv_obj_t *scr = base_screen();
    header(scr, "正在播放");

    // 唱片圆盘
    lv_obj_t *disc = lv_obj_create(scr);
    lv_obj_remove_style_all(disc);
    lv_obj_set_size(disc, 92, 92);
    lv_obj_align(disc, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_set_style_radius(disc, 10, 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(disc, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_border_color(disc, lv_color_hex(COL_GREEN), 0);
    lv_obj_set_style_border_width(disc, 3, 0);
    lv_obj_clear_flag(disc, LV_OBJ_FLAG_SCROLLABLE);
    s_p_cover = lv_image_create(disc);
    lv_obj_set_size(s_p_cover, 64, 64);
    lv_obj_align(s_p_cover, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_p_cover, LV_OBJ_FLAG_HIDDEN);
    s_p_disc_note = mk_label(disc, LV_ALIGN_CENTER, 0, 0, 0,
                             lv_color_hex(COL_GREEN));
    lv_obj_set_style_text_font(s_p_disc_note, &lv_font_montserrat_20, 0);
    lv_label_set_text(s_p_disc_note, LV_SYMBOL_AUDIO);

    // 状态文字悬浮在封面两侧，不占用歌词行。
    s_p_state = mk_label(scr, LV_ALIGN_TOP_LEFT, 4, 72, 66,
                         lv_color_hex(COL_GREEN));
    lv_label_set_long_mode(s_p_state, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(s_p_state, 66, 42);
    lv_obj_set_style_text_align(s_p_state, LV_TEXT_ALIGN_CENTER, 0);

    s_p_volume = mk_label(scr, LV_ALIGN_TOP_RIGHT, -4, 72, 66,
                          lv_color_hex(COL_GREEN));
    lv_label_set_long_mode(s_p_volume, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(s_p_volume, 66, 42);
    lv_obj_set_style_text_align(s_p_volume, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *name_view = lv_obj_create(scr);
    lv_obj_remove_style_all(name_view);
    lv_obj_set_size(name_view, 214, 22);
    lv_obj_align(name_view, LV_ALIGN_TOP_MID, 0, 144);
    lv_obj_clear_flag(name_view, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    s_p_name = mk_label(name_view, LV_ALIGN_CENTER, 0, 0, 214,
                        lv_color_hex(COL_WHITE));
    lv_label_set_long_mode(s_p_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_size(s_p_name, 214, 22);
    lv_obj_set_style_text_align(s_p_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *singer_view = lv_obj_create(scr);
    lv_obj_remove_style_all(singer_view);
    lv_obj_set_size(singer_view, 214, 20);
    lv_obj_align(singer_view, LV_ALIGN_TOP_MID, 0, 169);
    lv_obj_clear_flag(singer_view, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    s_p_singer = mk_label(singer_view, LV_ALIGN_CENTER, 0, 0, 214,
                          lv_color_hex(COL_GRAY));
    lv_label_set_long_mode(s_p_singer, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_size(s_p_singer, 214, 20);
    lv_obj_set_style_text_align(s_p_singer, LV_TEXT_ALIGN_CENTER, 0);

    // 进度条
    s_p_bar = lv_obj_create(scr);
    lv_obj_remove_style_all(s_p_bar);
    lv_obj_set_size(s_p_bar, 216, 6);
    lv_obj_align(s_p_bar, LV_ALIGN_TOP_MID, 0, 204);
    lv_obj_set_style_radius(s_p_bar, 3, 0);
    lv_obj_set_style_bg_opa(s_p_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_p_bar, lv_color_hex(COL_CARD), 0);
    s_p_bar_ind = lv_obj_create(s_p_bar);
    lv_obj_remove_style_all(s_p_bar_ind);
    lv_obj_set_size(s_p_bar_ind, 4, 6);
    lv_obj_set_style_radius(s_p_bar_ind, 3, 0);
    lv_obj_set_style_bg_opa(s_p_bar_ind, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_p_bar_ind, lv_color_hex(COL_GREEN), 0);

    s_p_time = mk_label(scr, LV_ALIGN_TOP_MID, 0, 214, 216,
                        lv_color_hex(COL_GRAY));
    lv_obj_set_style_text_font(s_p_time, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_p_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *lyric_view = lv_obj_create(scr);
    lv_obj_remove_style_all(lyric_view);
    lv_obj_set_size(lyric_view, 216, 42);
    lv_obj_align(lyric_view, LV_ALIGN_TOP_MID, 0, 235);
    lv_obj_clear_flag(lyric_view, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    s_p_lyric = mk_label(lyric_view, LV_ALIGN_CENTER, 0, 0, 216,
                          lv_color_hex(COL_WHITE));
    lv_obj_set_style_text_font(s_p_lyric, ui_text_font(), 0);
    lv_obj_set_style_text_line_space(s_p_lyric, 0, 0);
    lv_label_set_long_mode(s_p_lyric, LV_LABEL_LONG_DOT);
    lv_obj_set_size(s_p_lyric, 216, 40);
    lv_label_set_max_lines(s_p_lyric, 2);
    lv_obj_set_style_text_align(s_p_lyric, LV_TEXT_ALIGN_CENTER, 0);

    bottom_hint(scr, "上下切歌 长上下音量 OK暂停");
    load(scr);
}



void ui_player_set_cover(const uint8_t *rgb565le, int bytes)
{
    if (!rgb565le || bytes != 64 * 64 * 2) return;
    if (!bsp_lvgl_lock(500)) return;
    if (s_p_cover) {
        memset(&s_cover_dsc, 0, sizeof(s_cover_dsc));
        s_cover_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        s_cover_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        s_cover_dsc.header.w = 64;
        s_cover_dsc.header.h = 64;
        s_cover_dsc.header.stride = 64 * 2;
        s_cover_dsc.data_size = bytes;
        s_cover_dsc.data = rgb565le;
        lv_image_set_src(s_p_cover, &s_cover_dsc);
        lv_obj_remove_flag(s_p_cover, LV_OBJ_FLAG_HIDDEN);
        if (s_p_disc_note) lv_obj_add_flag(s_p_disc_note, LV_OBJ_FLAG_HIDDEN);
    }
    bsp_lvgl_unlock();
}

void ui_player_tick(const qqm_player_status_t *st, const char *lyric)
{
    if (!bsp_lvgl_lock(500)) return;
    if (!s_p_name) { bsp_lvgl_unlock(); return; }
    char name_line[sizeof(s_last_name)];
    char singer_line[sizeof(s_last_singer)];
    text_single_line(name_line, sizeof(name_line), st->name[0] ? st->name : "…");
    text_single_line(singer_line, sizeof(singer_line), st->singer);
    const char *name = name_line;
    const char *singer = singer_line;
    char lyric_line[sizeof(s_last_lyric)];
    text_single_line(lyric_line, sizeof(lyric_line),
                      (lyric && lyric[0]) ? lyric : "♪");
    const char *ly = lyric_line;
    if (strcmp(s_last_name, name) != 0) {
        lv_label_set_text(s_p_name, name);
        strncpy(s_last_name, name, sizeof(s_last_name) - 1);
        s_last_name[sizeof(s_last_name) - 1] = 0;
    }
    if (strcmp(s_last_singer, singer) != 0) {
        lv_label_set_text(s_p_singer, singer);
        strncpy(s_last_singer, singer, sizeof(s_last_singer) - 1);
        s_last_singer[sizeof(s_last_singer) - 1] = 0;
    }

    if (s_p_lyric && strcmp(s_last_lyric, ly) != 0) {
        lv_label_set_text(s_p_lyric, ly);
        strncpy(s_last_lyric, ly, sizeof(s_last_lyric) - 1);
        s_last_lyric[sizeof(s_last_lyric) - 1] = 0;
    }

    char tbuf[48];
    int total = st->duration > 0 ? st->duration : 0;
    snprintf(tbuf, sizeof(tbuf), "%d:%02d / %d:%02d",
             st->elapsed / 60, st->elapsed % 60, total / 60, total % 60);
    lv_label_set_text(s_p_time, tbuf);

    int pct = total > 0 ? (st->elapsed * 100 / total) : 0;
    if (pct > 100) pct = 100;
    lv_obj_set_width(s_p_bar_ind, 4 + (212 * pct / 100));

    switch (st->state) {
    case QQM_PLAYER_OPENING:
        snprintf(tbuf, sizeof(tbuf), "缓冲中\n%d%%", st->buffer_pct);
        lv_label_set_text(s_p_state, tbuf);
        break;
    case QQM_PLAYER_PAUSED:
        lv_label_set_text(s_p_state, "已暂停");
        break;
    case QQM_PLAYER_PLAYING:
        snprintf(tbuf, sizeof(tbuf), "缓冲\n%d%%", st->buffer_pct);
        lv_label_set_text(s_p_state, tbuf);
        break;
    default:
        lv_label_set_text(s_p_state, "");
        break;
    }
    snprintf(tbuf, sizeof(tbuf), "音量\n%d%%", st->volume);
    lv_label_set_text(s_p_volume, tbuf);
    bsp_lvgl_unlock();
}
