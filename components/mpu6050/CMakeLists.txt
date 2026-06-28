idf_component_register(
    SRCS "mpu6050.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common esp_driver_i2c i2c_bus
    PRIV_REQUIRES board_config
)
