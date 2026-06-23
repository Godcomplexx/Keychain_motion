idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES fluid_animation freertos i2c_bus oled_display
)
