// Wi-Fi STA 连接：使用 Kconfig 中的 SSID/密码，连接成功后事件组置位。
#pragma once

#include "esp_err.h"
#include "stdbool.h"
#include <stddef.h>

#define QQM_WIFI_CONNECTED_BIT  (1 << 0)
#define QQM_WIFI_FAILED_BIT     (1 << 1)

esp_err_t qqm_wifi_init(void);
// 阻塞等待联网，timeout_ms 超时返回 ESP_ERR_TIMEOUT。
esp_err_t qqm_wifi_wait(uint32_t timeout_ms);
bool qqm_wifi_is_connected(void);
bool qqm_wifi_needs_setup(void);
const char *qqm_wifi_server_url(void);
void qqm_wifi_setup_hint(char *out, size_t cap);
esp_err_t qqm_wifi_start_setup(void);
