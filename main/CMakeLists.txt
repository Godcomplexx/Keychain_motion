idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES board_config breakout_game device_clock draw_pad esp_driver_gpio esp_driver_usb_serial_jtag esp_hw_support esp_pm esp_timer flip_animation freertos i2c_bus idle_animation motion_detector motion_state mpu6050 oled_display phone_commands phone_sync time_animation
)
