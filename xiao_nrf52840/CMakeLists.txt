cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(smart_motion_keychain_xiao)

set(KEYCHAIN_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# oled_display_zephyr.c (SSD1306/I2C, standard hardware) and
# gc9107_display_zephyr.c (the bodge-wired round SPI panel) both implement
# oled_display.h and cannot be compiled together - same symbols, one linker.
# Every unit still ships the I2C OLED unless a build deliberately overrides
# this, e.g. -DKEYCHAIN_DISPLAY_DRIVER=gc9107_display_zephyr.c.
set(KEYCHAIN_DISPLAY_DRIVER "oled_display_zephyr.c" CACHE STRING
    "Which src/*_zephyr.c file provides the oled_display.h API")

target_sources(app PRIVATE
    src/battery_zephyr.c
    src/i2c_scan.c
    src/main.c
    src/mpu6050_zephyr.c
    src/${KEYCHAIN_DISPLAY_DRIVER}
    src/phone_sync_zephyr.c
    src/usb_dfu_touch.c
    ${KEYCHAIN_ROOT}/components/breakout_game/breakout_game.c
    ${KEYCHAIN_ROOT}/components/device_clock/device_clock.c
    ${KEYCHAIN_ROOT}/components/draw_pad/draw_pad.c
    ${KEYCHAIN_ROOT}/components/flip_animation/flip_animation.c
    ${KEYCHAIN_ROOT}/components/fluid_animation/fluid_animation.c
    ${KEYCHAIN_ROOT}/components/idle_animation/idle_animation.c
    ${KEYCHAIN_ROOT}/components/message_screen/message_screen.c
    ${KEYCHAIN_ROOT}/components/motion_detector/motion_detector.c
    ${KEYCHAIN_ROOT}/components/motion_state/motion_state.c
    ${KEYCHAIN_ROOT}/components/phone_commands/phone_commands.c
    ${KEYCHAIN_ROOT}/components/pet_behavior/pet_behavior.c
    ${KEYCHAIN_ROOT}/components/pet_face/pet_face.c
    ${KEYCHAIN_ROOT}/components/time_animation/time_animation.c
)

target_include_directories(app PRIVATE
    include
    ${KEYCHAIN_ROOT}/components/breakout_game/include
    ${KEYCHAIN_ROOT}/components/device_clock/include
    ${KEYCHAIN_ROOT}/components/draw_pad/include
    ${KEYCHAIN_ROOT}/components/flip_animation/include
    ${KEYCHAIN_ROOT}/components/fluid_animation/include
    ${KEYCHAIN_ROOT}/components/idle_animation/include
    ${KEYCHAIN_ROOT}/components/message_screen/include
    ${KEYCHAIN_ROOT}/components/motion_detector/include
    ${KEYCHAIN_ROOT}/components/motion_state/include
    ${KEYCHAIN_ROOT}/components/phone_commands/include
    ${KEYCHAIN_ROOT}/components/mpu6050/include
    ${KEYCHAIN_ROOT}/components/oled_display
    ${KEYCHAIN_ROOT}/components/oled_display/include
    ${KEYCHAIN_ROOT}/components/pet_behavior/include
    ${KEYCHAIN_ROOT}/components/pet_face/include
    ${KEYCHAIN_ROOT}/components/phone_sync/include
    ${KEYCHAIN_ROOT}/components/time_animation/include
)

# The shared animation code uses a few standard single-precision math helpers.
target_link_libraries(app PRIVATE m)
