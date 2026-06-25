idf_component_register(
    SRCS "idle_animation.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES board_config oled_display
)
