idf_component_register(
    SRCS "pet_face.c"
    INCLUDE_DIRS "include"
    REQUIRES pet_behavior
    PRIV_REQUIRES oled_display
)
