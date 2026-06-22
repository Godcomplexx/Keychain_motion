# AGENTS.md

# Project: ESP32-C3 Smart Motion Keychain

## Project context

This is a learning-focused embedded systems project.

The device is a small keychain-sized embedded product with:

* ESP32-C3
* 0.96 inch 128x64 I2C OLED display
* MPU6050 GY-521 accelerometer/gyroscope
* optional TTP223 touch sensor
* optional LiPo battery and TP4056 charger later
* 3D printed enclosure later

The project is built with ESP-IDF.

## Main product behavior

The keychain has three main user-facing states:

* FLUID: default animation based on accelerometer movement
* SLEEP: slow sleep animation after 60 seconds of stillness
* TIME: clock/time animation for 60 seconds after shake

## Learning mode rules

This project is not only about producing code. It is for learning embedded systems.

When helping with a task:

1. Do not generate large amounts of code immediately.
2. First explain the concept in beginner-friendly language.
3. Then explain the ESP-IDF/embedded terminology.
4. Then propose a small implementation plan.
5. Then create or modify only the minimum required files.
6. Add comments explaining why each important line exists.
7. After coding, explain the diff file by file.
8. Give me one small exercise to complete manually.
9. Give me 3–5 review questions to check understanding.
10. Do not move to the next milestone until the current one builds or is clearly blocked.

## Engineering rules

* Keep `main.c` small.
* Do not hardcode GPIO numbers across the project.
* Use `board_config.h` for hardware mapping.
* Keep hardware drivers separate from application logic.
* Use ESP-IDF style.
* Use `ESP_LOGI`, `ESP_LOGW`, and `ESP_LOGE` for logging.
* Prefer small components over one large file.
* Do not add RTOS until the simple loop version works.
* Do not add TinyML until rule-based motion detection works.
* Do not add battery logic until USB-powered prototype works.
* Do not invent final GPIO pins. Use placeholders and comments until hardware is verified.
* Do not assume I2C devices work before an I2C scanner confirms their addresses.

## Current docs

Read these files before making changes:

* `docs/00_project_overview.md`
* `docs/01_esp_idf_project_structure.md`

## Milestone order

1. ESP-IDF project structure
2. I2C scanner
3. OLED startup screen
4. MPU6050 raw data
5. Motion state machine
6. Animation engine
7. Battery and charging plan
8. Time sync
9. Enclosure design

## Code style

* Use C for ESP-IDF firmware.
* Use clear function names.
* Use `esp_err_t` for functions that can fail.
* Keep public headers in `include/`.
* Keep private implementation in `.c` files.
* Keep each component focused on one responsibility.

## Build and test

Use these commands when applicable:

```bash
idf.py set-target esp32c3
idf.py build
idf.py flash monitor
```

If build commands cannot be run in the current environment, explain exactly what I should run locally and what output I should expect.

## Teaching requirement

For every task, include a final section:

```text
What you should understand before moving on:
1.
2.
3.

Manual exercise:
...

Review questions:
1.
2.
3.
```
