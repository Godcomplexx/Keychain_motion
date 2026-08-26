idf_component_register(
    SRCS "phone_commands.c"
    INCLUDE_DIRS "include"
    REQUIRES motion_state phone_sync
)
