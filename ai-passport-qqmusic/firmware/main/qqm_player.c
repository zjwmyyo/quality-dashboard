#include "qqm_player.h"

#include "bsp_audio.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "qqm_player";

#define PCM_BUF_BYTES   1024
#define HTTP_BUF_SIZE    1024
#define PCM_BYTES_PER_SEC (16000 * 2)

typedef enum { CMD_STOP = 1, CMD_PAUSE, CMD_RESUME, CMD_NEXT, CMD_PREV } player_cmd_t;
typedef enum { END_EOF, END_NEXT, END_PREV, END_STOP } end_reason_t;

static StaticSemaphore_t s_mutex_buf;
static SemaphoreHandle_t s_mutex;
static QueueHandle_t s_cmdq;

static qqm_song_t *s_songs;
static int s_count;
static int s_index;
static qqm_player_status_t s_st;
static int s_volume = CONFIG_QQM_VOLUME;

typedef struct {
    volatile bool running;
    volatile bool paused;
    volatile int  buffer_pct;
    volatile int  elapsed;
} pipe_t;

static pipe_t *s_pipe;
static TaskHandle_t s_run_task;

static void set_state(qqm_player_state_t st)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_st.state = st;
    xSemaphoreGive(s_mutex);
}

static void fill_track_info(int idx)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_st.index = idx;
    s_st.elapsed = 0;
    s_st.duration = s_songs[idx].duration;
    s_st.buffer_pct = 0;
    strncpy(s_st.name, s_songs[idx].name, sizeof(s_st.name) - 1);
    s_st.name[sizeof(s_st.name) - 1] = 0;
    strncpy(s_st.singer, s_songs[idx].singer, sizeof(s_st.singer) - 1);
    s_st.singer[sizeof(s_st.singer) - 1] = 0;
    xSemaphoreGive(s_mutex);
}

static bool send_cmd(player_cmd_t c, TickType_t wait)
{
    return s_cmdq && xQueueSend(s_cmdq, &c, wait) == pdTRUE;
}

static end_reason_t play_one(void)
{
    int idx = s_index;
    fill_track_info(idx);
    set_state(QQM_PLAYER_OPENING);

    pipe_t pipe = { .running = true };
    s_pipe = &pipe;

    char url[256];
    qqm_api_stream_url(s_songs[idx].mid, url, sizeof(url));
    ESP_LOGI(TAG, "准备播放 idx=%d mid=%s url=%s", idx, s_songs[idx].mid, url);

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 15000,
        .buffer_size = HTTP_BUF_SIZE,
        .disable_auto_redirect = false,
    };
    esp_http_client_handle_t c = NULL;
    uint8_t *pcm = NULL;
    end_reason_t reason = END_STOP;

    c = esp_http_client_init(&cfg);
    pcm = malloc(PCM_BUF_BYTES);
    if (!c || !pcm) {
        ESP_LOGE(TAG, "播放器内存不足 c=%p pcm=%p", c, pcm);
        set_state(QQM_PLAYER_IDLE);
        goto out;
    }

    ESP_LOGI(TAG, "开始 PCM 拉流: %s", url);
    esp_err_t err = esp_http_client_open(c, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "打开流失败: %s", esp_err_to_name(err));
        goto out;
    }
    int clen = esp_http_client_fetch_headers(c);
    int status = esp_http_client_get_status_code(c);
    ESP_LOGI(TAG, "流响应 %d, content-length=%d", status, clen);
    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "音频 HTTP 状态异常: %d", status);
        goto out;
    }

    bsp_audio_set_format(16000, 16, 1);
    bsp_audio_set_volume((uint8_t)s_volume);
    ESP_LOGI(TAG, "格式 PCM 16000Hz 1ch s16le");
    set_state(QQM_PLAYER_PLAYING);
    reason = END_EOF;

    int total_bytes = 0;
    int idle_reads = 0;
    int last_log_sec = -1;
    while (pipe.running) {
        player_cmd_t cmd = 0;
        if (xQueueReceive(s_cmdq, &cmd, 0)) {
            if (cmd == CMD_STOP) { reason = END_STOP; break; }
            if (cmd == CMD_NEXT) { reason = END_NEXT; break; }
            if (cmd == CMD_PREV) { reason = END_PREV; break; }
            if (cmd == CMD_PAUSE) pipe.paused = true;
            if (cmd == CMD_RESUME) pipe.paused = false;
        }
        while (pipe.paused && pipe.running) {
            set_state(QQM_PLAYER_PAUSED);
            if (xQueueReceive(s_cmdq, &cmd, pdMS_TO_TICKS(120))) {
                if (cmd == CMD_STOP) { reason = END_STOP; goto out; }
                if (cmd == CMD_NEXT) { reason = END_NEXT; goto out; }
                if (cmd == CMD_PREV) { reason = END_PREV; goto out; }
                if (cmd == CMD_RESUME || cmd == CMD_PAUSE) pipe.paused = false;
            }
        }
        set_state(QQM_PLAYER_PLAYING);

        int r = esp_http_client_read(c, (char *)pcm, PCM_BUF_BYTES);
        if (r < 0) {
            ESP_LOGW(TAG, "读取 PCM 失败 r=%d", r);
            reason = END_STOP;
            break;
        }
        if (r == 0) {
            if (++idle_reads > 30) { reason = (total_bytes > 0) ? END_EOF : END_STOP; break; }
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }
        idle_reads = 0;
        pipe.buffer_pct = 100;
        if (bsp_audio_write(pcm, r) != ESP_OK) {
            ESP_LOGW(TAG, "写入音频失败 bytes=%d", r);
            reason = END_STOP;
            break;
        }
        total_bytes += r;
        pipe.elapsed = total_bytes / PCM_BYTES_PER_SEC;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_st.elapsed = pipe.elapsed;
        s_st.buffer_pct = pipe.buffer_pct;
        xSemaphoreGive(s_mutex);
        if (pipe.elapsed != last_log_sec && (pipe.elapsed % 2) == 0) {
            last_log_sec = pipe.elapsed;
            ESP_LOGI(TAG, "PCM 播放 %d 秒 bytes=%d", pipe.elapsed, total_bytes);
        }
    }

out:
    if (c) esp_http_client_cleanup(c);
    free(pcm);
    s_pipe = NULL;
    return reason;
}

static void run_task(void *arg)
{
    (void)arg;
    if (!s_songs || s_count == 0) {
        s_run_task = NULL;
        vTaskDelete(NULL);
    }
    for (;;) {
        end_reason_t r = play_one();
        vTaskDelay(pdMS_TO_TICKS(120));
        if (r == END_STOP) { set_state(QQM_PLAYER_IDLE); break; }
        if (r == END_NEXT) s_index = (s_index + 1) % s_count;
        else if (r == END_PREV) s_index = (s_index - 1 + s_count) % s_count;
        else s_index = (s_index + 1) % s_count;
    }
    s_run_task = NULL;
    vTaskDelete(NULL);
}

void qqm_player_start(const qqm_song_t *songs, int count, int index)
{
    ESP_LOGI(TAG, "请求播放 count=%d index=%d", count, index);
    if (s_pipe || s_run_task) {
        send_cmd(CMD_STOP, pdMS_TO_TICKS(100));
        for (int i = 0; i < 40 && (s_pipe || s_run_task); i++) vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (s_cmdq) xQueueReset(s_cmdq);
    if (s_songs) { free(s_songs); s_songs = NULL; }
    s_count = count;
    s_index = index;
    s_songs = calloc(count, sizeof(qqm_song_t));
    if (!s_songs) { ESP_LOGE(TAG, "歌曲列表分配失败"); return; }
    memcpy(s_songs, songs, count * sizeof(qqm_song_t));
    bsp_audio_init();
    bsp_audio_set_volume((uint8_t)s_volume);
    if (xTaskCreate(run_task, "qqm_run", 8192, NULL, 6, &s_run_task) != pdPASS) {
        ESP_LOGE(TAG, "播放任务创建失败");
        s_run_task = NULL;
        set_state(QQM_PLAYER_IDLE);
    }
}

void qqm_player_stop(void) { send_cmd(CMD_STOP, pdMS_TO_TICKS(100)); }
void qqm_player_next(void)
{
    if (s_pipe || s_run_task) send_cmd(CMD_NEXT, pdMS_TO_TICKS(100));
    else if (s_songs && s_count > 0) {
        s_index = (s_index + 1) % s_count;
        xTaskCreate(run_task, "qqm_run", 8192, NULL, 6, &s_run_task);
    }
}
void qqm_player_prev(void)
{
    if (s_pipe || s_run_task) send_cmd(CMD_PREV, pdMS_TO_TICKS(100));
    else if (s_songs && s_count > 0) {
        s_index = (s_index - 1 + s_count) % s_count;
        xTaskCreate(run_task, "qqm_run", 8192, NULL, 6, &s_run_task);
    }
}

void qqm_player_volume_delta(int delta)
{
    int v = s_volume + delta;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    s_volume = v;
    bsp_audio_set_volume((uint8_t)s_volume);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_st.volume = s_volume;
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "音量 %d", s_volume);
}

int qqm_player_volume(void) { return s_volume; }

void qqm_player_toggle_pause(void)
{
    if (!s_pipe) return;
    if (s_pipe->paused) send_cmd(CMD_RESUME, pdMS_TO_TICKS(100));
    else send_cmd(CMD_PAUSE, pdMS_TO_TICKS(100));
}

void qqm_player_get_status(qqm_player_status_t *out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_st.volume = s_volume;
    *out = s_st;
    if (s_pipe) out->buffer_pct = s_pipe->buffer_pct;
    xSemaphoreGive(s_mutex);
}

bool qqm_player_active(void) { return s_pipe != NULL || s_run_task != NULL; }

void qqm_player_module_init(void)
{
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buf);
    s_cmdq = xQueueCreate(6, sizeof(player_cmd_t));
    memset(&s_st, 0, sizeof(s_st));
    s_st.state = QQM_PLAYER_IDLE;
    s_st.volume = s_volume;
}
