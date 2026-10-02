// 流式 MP3 播放器：HTTP 拉流任务 → 小环形缓冲 → Helix 解码任务 → ES8311。
// 预缓冲 + 网络/解码解耦，保证播放不卡顿。
#pragma once

#include "stdbool.h"
#include "qqm_api.h"

typedef enum {
    QQM_PLAYER_IDLE = 0,
    QQM_PLAYER_OPENING,   // 连接/缓冲中
    QQM_PLAYER_PLAYING,
    QQM_PLAYER_PAUSED,
    QQM_PLAYER_ENDED,
} qqm_player_state_t;

typedef struct {
    qqm_player_state_t state;
    int  index;          // 当前曲目在列表中的下标
    int  elapsed;        // 已播放秒数
    int  duration;       // 总时长（秒，未知为 0）
    int  buffer_pct;     // 输入缓冲占用百分比
    int  volume;         // 输出音量 0..100
    char name[QQM_NAME_LEN];
    char singer[QQM_SINGER_LEN];
} qqm_player_status_t;

// 加载歌曲列表并从 index 开始播放（列表由播放器内部拷贝）。
void qqm_player_start(const qqm_song_t *songs, int count, int index);
void qqm_player_stop(void);
void qqm_player_toggle_pause(void);
void qqm_player_next(void);
void qqm_player_prev(void);
void qqm_player_volume_delta(int delta);
int  qqm_player_volume(void);
void qqm_player_get_status(qqm_player_status_t *out);
bool qqm_player_active(void);

// 初始化播放器控制任务（app_main 早期调用一次）。
void qqm_player_module_init(void);
