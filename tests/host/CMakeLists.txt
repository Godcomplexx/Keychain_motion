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
