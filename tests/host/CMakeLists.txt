cmake_minimum_required(VERSION 3.16)

# This is a native PC test, not an ESP-IDF or Zephyr firmware target.
project(smart_motion_keychain_host_tests C)

enable_testing()

add_executable(test_pet_behavior
    test_pet_behavior.c
    ../../components/pet_behavior/pet_behavior.c
)

target_include_directories(test_pet_behavior PRIVATE
    ../../components/pet_behavior/include
)

add_test(NAME pet_behavior COMMAND test_pet_behavior)

# The command planner is portable logic that the main loops execute rather
# than decide, so it can be checked here instead of on a keychain with one
# USB port. esp_err.h comes from the nRF port's compatibility shim - the same
# one the firmware uses - rather than a stub written for the tests.
# Only the header of motion_state is needed: the planner names event values
# and calls none of its functions, and its implementation logs through the
# platform's esp_log shim, which is not host code.
add_executable(test_phone_commands
    test_phone_commands.c
    ../../components/phone_commands/phone_commands.c
)

target_include_directories(test_phone_commands PRIVATE
    ../../components/phone_commands/include
    ../../components/motion_state/include
    ../../components/phone_sync/include
    ../../components/device_clock/include
    ../../xiao_nrf52840/include
)

add_test(NAME phone_commands COMMAND test_phone_commands)

# Not a test: it prints the clock screen as ASCII so its layout can be judged
# without flashing a keychain. The glyph table it draws with is the
# firmware's own, so what it shows is what the panel shows.
add_executable(preview_time_screen
    preview_time_screen.c
    ../../components/time_animation/time_animation.c
    ../../components/message_screen/message_screen.c
)

target_include_directories(preview_time_screen PRIVATE
    ../../components/time_animation/include
    ../../components/message_screen/include
    ../../components/oled_display/include
    ../../components/oled_display
    ../../xiao_nrf52840/include
)
