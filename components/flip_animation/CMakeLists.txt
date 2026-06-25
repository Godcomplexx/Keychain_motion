idf_component_register(
    SRCS "flip_animation.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES board_config oled_display esp_timer
)
