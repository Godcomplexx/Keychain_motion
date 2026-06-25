idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES adxl345 device_clock esp_timer flip_animation freertos i2c_bus idle_animation motion_detector motion_state oled_display phone_sync step_counter time_animation
)
