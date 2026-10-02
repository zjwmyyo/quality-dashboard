idf_component_register(
    SRCS "main.c"
         "qqm_wifi.c"
         "qqm_api.c"
         "qqm_player.c"
         "qqm_ui.c"
         "font_cn_16.c"
         "../assets/fonts/font_ko_16.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_http_client esp_http_server json esp_wifi esp_event nvs_flash lvgl
)
