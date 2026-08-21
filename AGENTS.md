# AGENTS.md

# Project: SmartMotion Keychain (nRF52840 v2 / ESP32-C3 v1)

## Project context

This is a learning-focused embedded systems project with two firmware targets:

* **v2, current:** nRF52840/E73, Zephyr 4.2, SSD1306 OLED, LIS2DW12 on the
  custom PCB, optional MPU-6050 compatibility, and a protected LiPo.
* **v1, reference prototype:** ESP32-C3, ESP-IDF 6.0.1, SSD1306 OLED, and
  MPU-6050. It established the original motion, clock, sleep, and game logic.

The Android 8.0+ companion application shares the same BLE protocol with both
firmware versions. Portable behavior, clock, animation, and game code stays in
`components/`; platform-specific drivers stay in `main/` or `xiao_nrf52840/`.

## Main product behavior

The current v2 screen is a procedural companion driven by events, priorities,
and interaction memory. The original FLIP animation remains available as a
self-directed activity. Phone time sync and phone-started Breakout remain part
of the product behavior.

The v1 firmware retains its four explicit states:

* FLUID: default animation based on accelerometer movement
* SLEEP: slow sleep animation after 30 seconds of stillness
* TIME: clock screen after a successful phone time sync
* GAME: phone-started Breakout controlled by accelerometer tilt

## Learning mode rules

This project is not only about producing code. It is for learning embedded systems.

When helping with a task:

1. Do not generate large amounts of code immediately.
2. First explain the concept in beginner-friendly language.
3. Then explain the Zephyr/ESP-IDF/embedded terminology.
4. Then propose a small implementation plan.
5. Then create or modify only the minimum required files.
6. Add comments explaining why each important line exists.
7. After coding, explain the diff file by file.
8. Give me one small exercise to complete manually.
9. Give me 3–5 review questions to check understanding.
10. Do not move to the next milestone until the current one builds or is clearly blocked.

## Engineering rules

* Keep each platform's `main.c` focused on integration and lifecycle policy.
* Keep hardware drivers separate from portable application logic.
* Keep portable components free of Zephyr, ESP-IDF, and RTOS types.
* Do not hardcode GPIO numbers across the project.
* Use devicetree overlays for Zephyr mappings and `board_config.h` for ESP-IDF.
* Use Zephyr logging/`printk` in v2 and `ESP_LOGI`, `ESP_LOGW`, and `ESP_LOGE` in v1.
* Prefer small components over one large file.
* Keep application behavior in one simple loop; add custom RTOS tasks only when required.
* Do not add TinyML until rule-based motion detection works on real hardware.
* Do not invent final GPIO pins. Verify them against the PCB/netlist first.
* Do not assume an I2C device works before startup probing confirms its address.
* Do not enable battery or wake policy until its ADC/interrupt path is verified on hardware.

## Current docs

Read these files before making changes:

* `README.md` or `README.ru.md`
* `docs/ARCHITECTURE.md`
* `docs/HARDWARE.md`
* `xiao_nrf52840/README.md`
* `tools/flash/README.md` for bootloader, SWD, or USB update work

## Milestone order

The ESP32-C3 v1 milestones are complete and retained as a working reference.

Current v2 milestones:

1. Reproducible pristine builds for XIAO and the final E73 configuration
2. Solder and probe LIS2DW12; verify live motion reactions
3. Configure and verify `ACC_INT1` wake-up
4. Calibrate SAADC battery measurement and low-battery behavior
5. Measure current in active, idle, BLE, and sleep states
6. Replace the MCUboot development key and validate the release/update flow
7. Complete enclosure and full hardware regression testing

## Code style

* Use C for firmware and portable components.
* Use clear function names.
* Use `esp_err_t` for shared fallible APIs that already use the compatibility layer.
* Keep public headers in `include/`.
* Keep private implementation in `.c` files.
* Keep each component focused on one responsibility.

## Build and test

For v2, activate a Zephyr 4.2 workspace and SDK first:

```powershell
$env:ZEPHYR_BASE = '<workspace>\zephyr'
$env:ZEPHYR_SDK_INSTALL_DIR = '<zephyr-sdk>'

python -m west build -p always -b xiao_ble/nrf52840 `
  D:\key_chain\xiao_nrf52840 -d D:\key_chain\xiao_nrf52840\build-xiao

python -m west build -p always -b nrf52840dk/nrf52840 `
  D:\key_chain\xiao_nrf52840 -d D:\key_chain\xiao_nrf52840\build-e73 `
  -- -DEXTRA_CONF_FILE=D:/key_chain/xiao_nrf52840/gerber.conf
```

For portable host tests:

```powershell
cmake -S tests/host -B build-host -G Ninja
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

For the Android application:

```powershell
cd android_time_sync
.\gradlew.bat lintDebug assembleDebug
```

For the v1 reference firmware:

```powershell
idf.py set-target esp32c3
idf.py build
idf.py flash monitor
```

If build commands cannot be run in the current environment, explain exactly
what should be run locally and what output should be expected.

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
