# ESP32-C3 Smart Motion Keychain

## 1. Project Summary

**ESP32-C3 Smart Motion Keychain** is a small keychain-sized embedded device with an OLED display and motion-based interaction.

The device uses an  **ESP32-C3** , an  **MPU6050 accelerometer/gyroscope** , and a  **0.96 inch 128x64 I2C OLED display** . It reacts to movement and shows simple animated states on the screen.

The project is designed as a learning project for embedded systems, product prototyping, firmware architecture, sensor integration, display UI, battery-powered design, and enclosure design.

---

## 2. Main Idea

The keychain behaves like a small animated physical object.

It has three core user-facing states:

1. **FLUID**
   * Default active state.
   * OLED shows a fluid-like animation.
   * Animation reacts to accelerometer movement and tilt.
2. **SLEEP**
   * Low-activity state.
   * If the device is still for more than 1 minute, it switches to a sleep animation.
   * Sleep animation should be slower and calmer than the fluid state.
3. **TIME**
   * Temporary time/clock animation state.
   * If the user shakes the keychain, it shows a time-style animation for 1 minute.
   * After 1 minute, it returns to FLUID or SLEEP depending on movement.

---

## 3. Project Goals

### Functional goals

* Read movement data from the MPU6050.
* Display animations on a 128x64 OLED screen.
* Detect basic motion events:
  * movement
  * stillness
  * shake
  * tilt
* Switch between FLUID, SLEEP, and TIME states.
* Keep hardware logic separated from application logic.
* Build the project using ESP-IDF.
* Design a small 3D printed keychain enclosure.

### Learning goals

This project should teach:

* ESP-IDF project structure
* C components and headers
* I2C bus basics
* OLED display control
* MPU6050 sensor reading
* motion thresholds
* state machines
* simple animation logic
* modular firmware architecture
* battery-powered device planning
* enclosure design for electronics
* documentation for portfolio/GitHub

---

## 4. Hardware

### Required components

* ESP32-C3 development board
* OLED display 0.96 inch, 128x64 pixels, 4-pin I2C
* MPU6050 GY-521 accelerometer/gyroscope module
* Wires / breadboard or soldered prototype
* 3D printed keychain enclosure later

### Optional components

* TTP223 capacitive touch sensor
* LiPo battery
* TP4056 charging module with protection
* Small power switch
* Buzzer or vibration motor later

---

## 5. Power Plan

Battery power is not part of the first milestone.

The first prototype can be powered over USB.

Later battery version should use:

* small 3.7V LiPo battery
* TP4056 charging module with protection
* 3.3V regulator if required by the ESP32-C3 board
* physical on/off switch
* safe charging current suitable for the battery size

Important rule:

The LiPo battery must not be connected directly to a 3.3V ESP32-C3 power pin without proper regulation/protection.

Battery and charging circuit will be designed later in a separate document:

* `docs/07_power_and_battery.md`

---

## 6. Time Synchronization Plan

The TIME state should eventually show real time.

Possible time sources:

1. **Wi-Fi + NTP**
   * Device connects to Wi-Fi only briefly.
   * It gets time from NTP.
   * Then Wi-Fi is turned off to save battery.
2. **Manual/BLE time setup**
   * Later option.
   * Time could be set from a phone.
3. **RTC module**
   * Later hardware option.
   * Not required for the first version.

MVP decision:

For the first version, TIME state can show a clock-style animation or uptime.

Real NTP time sync will be added later as a separate milestone.

---

## 7. Basic State Machine

### States

* `BOOT`
* `FLUID`
* `SLEEP`
* `TIME`
* `ERROR`

### Events

* `MOVEMENT_DETECTED`
* `STILLNESS_TIMEOUT`
* `SHAKE_DETECTED`
* `TIME_TIMEOUT`
* `TOUCH_DETECTED`
* `SENSOR_ERROR`
* `DISPLAY_ERROR`

### State transitions

* `BOOT → FLUID`
* `FLUID → SLEEP` if the device is still for more than 60 seconds
* `SLEEP → FLUID` if movement is detected
* `FLUID → TIME` if shake is detected
* `SLEEP → TIME` if shake is detected
* `TIME → FLUID` after 60 seconds if movement is detected
* `TIME → SLEEP` after 60 seconds if no movement is detected
* Any state → `ERROR` if display or sensor initialization fails

---

## 8. Firmware Architecture

The firmware should be modular.

Recommended components:

* `board_config`
  * stores GPIO pins and hardware configuration
* `i2c_bus`
  * initializes I2C
  * provides I2C scan/debug functionality
* `oled_display`
  * controls SSD1306 OLED display
  * exposes functions for clearing screen, drawing text, drawing pixels/shapes
* `mpu6050`
  * initializes MPU6050
  * reads raw accelerometer and gyroscope data
* `motion_state`
  * stores current state
  * handles state transitions
* `motion_detector`
  * detects movement, stillness, shake, and tilt from accelerometer data
* `animation_engine`
  * draws FLUID, SLEEP, and TIME animations
* `time_sync`
  * later component for Wi-Fi/NTP time synchronization
* `power_manager`
  * later component for battery and sleep behavior

---

## 9. Suggested Project Structure

smart_motion_keychain/
├── CMakeLists.txt
├── main/
│ ├── CMakeLists.txt
│ └── main.c
├── components/
│ ├── board_config/
│ ├── i2c_bus/
│ ├── oled_display/
│ ├── mpu6050/
│ ├── motion_state/
│ ├── motion_detector/
│ ├── animation_engine/
│ ├── time_sync/
│ └── power_manager/
└── docs/
├── 00_project_overview.md
├── 01_esp_idf_project_structure.md
├── 02_i2c_scanner.md
├── 03_oled_display.md
├── 04_mpu6050.md
├── 05_motion_states.md
├── 06_animation_engine.md
├── 07_power_and_battery.md
├── 08_time_sync.md
├── 09_enclosure_design.md
└── 10_testing_plan.md

---

## 10. Development Milestones

### Milestone 1 — ESP-IDF project structure

Goal:

Create a clean ESP-IDF project for ESP32-C3.

Result:

* project builds
* `app_main()` runs
* startup log appears in serial monitor
* hardware pins are centralized in `board_config.h`

Document:

* `docs/01_esp_idf_project_structure.md`

---

### Milestone 2 — I2C scanner

Goal:

Verify that ESP32-C3 can communicate over I2C.

Result:

* serial monitor prints detected I2C device addresses
* OLED and MPU6050 addresses can be checked before writing drivers

Document:

* `docs/02_i2c_scanner.md`

---

### Milestone 3 — OLED startup screen

Goal:

Display first text on the OLED.

Result:

OLED shows:

* project name
* READY status
* optional small boot animation

Document:

* `docs/03_oled_display.md`

---

### Milestone 4 — MPU6050 raw data

Goal:

Read accelerometer and gyroscope data from MPU6050.

Result:

Serial monitor prints:

* acceleration X/Y/Z
* gyro X/Y/Z
* values change when the device moves

Document:

* `docs/04_mpu6050.md`

---

### Milestone 5 — Motion state machine

Goal:

Implement FLUID, SLEEP, and TIME states without complex animations yet.

Result:

* state transitions are logged
* shake switches to TIME
* stillness switches to SLEEP
* movement wakes from SLEEP

Document:

* `docs/05_motion_states.md`

---

### Milestone 6 — Animation engine

Goal:

Draw basic animations on the OLED.

Result:

* FLUID animation reacts to tilt
* SLEEP animation plays after stillness
* TIME animation plays after shake

Document:

* `docs/06_animation_engine.md`

---

### Milestone 7 — Battery and charging plan

Goal:

Plan battery-powered version safely.

Result:

* choose small LiPo size
* choose TP4056/protection module
* define enclosure constraints
* define safe charging current
* decide how power switch works

Document:

* `docs/07_power_and_battery.md`

---

### Milestone 8 — Time sync

Goal:

Add real time support later.

Result:

* on boot or charging, device briefly enables Wi-Fi
* gets time using NTP
* turns Wi-Fi off
* TIME state can show real time

Document:

* `docs/08_time_sync.md`

---

### Milestone 9 — Enclosure design

Goal:

Design a 3D printed keychain enclosure.

Result:

* OLED opening
* keyring hole
* USB access
* sensor placement
* optional touch sensor area
* space for battery and TP4056 later

Document:

* `docs/09_enclosure_design.md`

---

## 11. Non-Goals for First Version

Do not implement these at the beginning:

* TinyML
* BLE phone app
* full battery optimization
* custom PCB
* deep sleep
* Wi-Fi always on
* complex physics simulation
* waterproof enclosure
* production-ready charging circuit

These can be added later after the basic prototype works.

---

## 12. Design Rules

* Keep `main.c` small.
* Do not hardcode GPIO numbers across files.
* Use `board_config.h` for hardware mapping.
* Test I2C before writing sensor/display drivers.
* Test each milestone on real hardware.
* Keep display code separate from motion detection.
* Keep sensor driver separate from animation logic.
* Do not add RTOS until the simple loop version works.
* Do not add TinyML until rule-based motion detection works.
* Do not design the final enclosure before measuring real components.

---

## 13. Current Status

### Milestone 1 — Completed

The initial ESP-IDF project structure has been created successfully.

Verified results:

* the project builds successfully
* the firmware flashes successfully
* `app_main()` runs
* startup messages appear through ESP-IDF logging
* GPIO placeholders are centralized in `board_config.h`

Milestone 1 was tested on an ESP32-C6 development board. The final target is
ESP32-C3, so the target and board GPIO mapping will be verified again when the
ESP32-C3 board is available.

Next milestone:

* Milestone 2 — I2C scanner

### Learning Review / Проверка знаний

#### 1. How is building different from flashing?

**English:** Building compiles and links the source code into firmware files on
the computer. Flashing writes those firmware files into the controller's flash
memory. The firmware is normally changed by rebuilding and flashing it again.

**Русский:** Сборка компилирует и компонует исходный код в файлы прошивки на
компьютере. Прошивка записывает эти файлы во flash-память контроллера. Обычно
программу изменяют, повторно собирают и заново прошивают.

#### 2. Why are SDA and SCL set to `-1`?

**English:** `-1` means that the GPIO pins have not been assigned yet. It does
not mean that no devices are connected. Safe SDA and SCL pins must be selected
after checking the exact development board pinout.

**Русский:** `-1` означает, что GPIO ещё не назначены. Это не означает, что
устройства не подключены. Безопасные выводы SDA и SCL нужно выбрать после
проверки распиновки конкретной платы.

#### 3. Which log messages confirm that `app_main()` ran?

**English:** ESP-IDF reports `Calling app_main()`, and the application then logs
`Smart Motion Keychain started`. Together these messages confirm that the entry
point was called and the application code executed.

**Русский:** ESP-IDF выводит `Calling app_main()`, после чего приложение выводит
`Smart Motion Keychain started`. Эти сообщения подтверждают, что точка входа
была вызвана и код приложения выполнился.

---

## 14. Portfolio Description Draft

ESP32-C3 Smart Motion Keychain is a compact embedded product prototype with an OLED display and accelerometer-based interaction. The device reacts to motion, displays fluid-like animations, enters a sleep animation after inactivity, and shows a time-style animation after a shake gesture.

The project demonstrates ESP-IDF firmware architecture, I2C sensor/display integration, motion detection, state-machine design, embedded UI, and early industrial design considerations for a small 3D printed keychain enclosure.
