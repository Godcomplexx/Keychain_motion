# This is a header-only component, so it has no SRCS entry.
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_spi
)
