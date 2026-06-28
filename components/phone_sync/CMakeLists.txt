idf_component_register(
    SRCS "phone_sync.c"
    INCLUDE_DIRS "include"
    REQUIRES bt device_clock freertos nvs_flash
)
