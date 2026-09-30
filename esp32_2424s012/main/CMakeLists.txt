set(KEYCHAIN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

idf_component_register(
    SRCS "main.c" "round_display.c" "round_touch.c" "round_motion.c"
         "round_orientation.c"
         "${KEYCHAIN_ROOT}/components/round_reactions/round_reactions.c"
         "${KEYCHAIN_ROOT}/components/pet_face/pet_face.c"
    INCLUDE_DIRS "." "${KEYCHAIN_ROOT}/components/pet_face/include"
                 "${KEYCHAIN_ROOT}/components/round_reactions/include"
                 "${KEYCHAIN_ROOT}/components/pet_behavior/include"
                 "${KEYCHAIN_ROOT}/components/oled_display/include"
                 "${KEYCHAIN_ROOT}/components/oled_display"
    PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi esp_lcd
                  esp_timer espressif__esp_lcd_gc9a01
)

target_link_libraries(${COMPONENT_LIB} PRIVATE m)
