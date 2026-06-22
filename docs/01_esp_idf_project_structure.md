# docs/01_esp_idf_project_structure.md

## Goal

Create the first clean ESP-IDF project structure for the ESP32-S3 Smart Motion Device.

This milestone does not implement TinyML, RTOS, battery management, display logic, or a real IMU driver yet.

The goal is simple:

* create a project that builds
* keep hardware pin mapping in one place
* prepare the structure for future drivers
* make the code understandable for a beginner

---

## 1. What I need to learn

### ESP-IDF project

An ESP-IDF project is a firmware project that contains application code, configuration, build files, and optional reusable components.

For this project, the ESP32-S3 firmware will be organized into:

* `main/` for high-level application logic
* `components/` for reusable modules
* `docs/` for project notes, architecture, and Codex tasks

### Component

A component is a reusable module of code.

In this project, future components can include:

* `board_config`
* `imu_sensor`
* `button_driver`
* `ui_feedback`
* `motion_buffer`
* `classifier`

### Board configuration

`board_config.h` stores hardware-specific definitions in one place.

Instead of writing raw pin numbers directly in `main.c`, the code should use names like:

```c
BOARD_LED_GPIO
BOARD_BUTTON_GPIO
BOARD_I2C_SDA_GPIO
BOARD_I2C_SCL_GPIO
```

This makes the project easier to change when the hardware changes.

---

## 2. Recommended project structure

```text
smart_motion_device/
├── CMakeLists.txt
├── main/
│   ├── CMakeLists.txt
│   └── main.c
├── components/
│   ├── board_config/
│   │   ├── CMakeLists.txt
│   │   └── include/
│   │       └── board_config.h
│   └── imu_sensor/
│       ├── CMakeLists.txt
│       ├── include/
│       │   └── imu_sensor.h
│       └── imu_sensor.c
└── docs/
    ├── 00_project_overview.md
    └── 01_esp_idf_project_structure.md
```

---

## 3. Files to create

### `main/main.c`

Purpose:

* application entry point
* contains `app_main()`
* prints startup log
* later calls project modules

For this milestone, `main.c` should only:

* start successfully
* print a log message
* print selected board configuration values

It should not implement IMU, TinyML, RTOS, or power management yet.

---

### `components/board_config/include/board_config.h`

Purpose:

* central place for hardware pin definitions
* prevents hardcoded GPIO numbers across the project

This file should contain placeholder pin definitions.

Important:

The GPIO values must be checked against the exact ESP32-S3 development board before wiring hardware.

Avoid using ESP32-S3 strapping pins for beginner-level button wiring unless there is a clear reason.

---

### `components/board_config/CMakeLists.txt`

Purpose:

* registers `board_config` as an ESP-IDF component
* exposes the `include/` directory to other components

---

### `components/imu_sensor/include/imu_sensor.h`

Purpose:

* public API placeholder for the future IMU driver
* gives the project a clean structure before real sensor logic is added

For this milestone, it can expose only:

```c
esp_err_t imu_sensor_init(void);
```

---

### `components/imu_sensor/imu_sensor.c`

Purpose:

* private implementation file for the future IMU driver

For this milestone, it can contain a stub implementation that logs that the IMU driver is not implemented yet.

---

### `components/imu_sensor/CMakeLists.txt`

Purpose:

* registers the IMU sensor component
* declares dependency on `board_config` if it uses board pin definitions

---

## 4. Beginner explanation

### `main/`

This is where the firmware starts.

The function `app_main()` is the entry point of an ESP-IDF application.

At this stage, `main.c` should stay small.

Good:

```c
app_main()
  -> print startup log
  -> print board config
  -> call imu_sensor_init()
```

Bad:

```c
app_main()
  -> configure every pin
  -> implement I2C manually
  -> read sensor
  -> run ML
  -> handle button
  -> update display
```

---

### `components/`

This folder contains reusable modules.

Each component should have one responsibility.

Examples:

* `board_config` knows hardware pin names
* `imu_sensor` will know how to talk to the IMU
* `button_driver` will know how to read and debounce the button
* `ui_feedback` will know how to control LED/display feedback

---

### `CMakeLists.txt`

These files tell ESP-IDF how to build the project.

Each component needs its own `CMakeLists.txt`.

A minimal component usually registers:

* source files
* include directories
* required dependencies

---

### `board_config.h`

This file is the hardware map.

It should answer:

* Which GPIO is LED?
* Which GPIO is button?
* Which GPIO is I2C SDA?
* Which GPIO is I2C SCL?
* Which I2C port is used?

The rest of the code should use names from this file instead of raw numbers.

---

## 5. Implementation checklist

* [ ] Create the ESP-IDF project.
* [ ] Set the target to ESP32-S3.
* [ ] Create `components/board_config/`.
* [ ] Create `board_config.h`.
* [ ] Add placeholder GPIO definitions.
* [ ] Add comments that all GPIO values must be checked against the real board.
* [ ] Create `components/imu_sensor/`.
* [ ] Add `imu_sensor.h`.
* [ ] Add `imu_sensor.c`.
* [ ] Add a stub `imu_sensor_init()` function.
* [ ] Create or update all required `CMakeLists.txt` files.
* [ ] Update `main/main.c`.
* [ ] Build with `idf.py build`.
* [ ] Flash and monitor with `idf.py flash monitor`.

---

## 6. Mistakes to avoid

### Hardcoding pins

Do not write raw GPIO numbers inside application logic.

Bad:

```c
gpio_set_level(2, 1);
```

Better:

```c
gpio_set_level(BOARD_LED_GPIO, 1);
```

---

### Using risky GPIOs without checking

Do not randomly choose ESP32-S3 GPIO pins.

Some pins have special boot, flash, PSRAM, USB, or strapping functions.

Always check the exact board pinout before wiring hardware.

---

### Starting with TinyML too early

Do not implement TinyML in this milestone.

Before TinyML, the project must have:

* stable ESP-IDF build
* clean project structure
* working I2C
* verified IMU data
* logging

---

### Creating too much architecture too early

Do not add RTOS tasks, event queues, watchdog, power states, or circular buffers in this milestone.

Those come later.

---

### Making `main.c` too large

`main.c` should coordinate modules.

It should not contain all hardware logic.

---

## 7. First Codex-ready task

### Task

Create the initial modular ESP-IDF project structure for an ESP32-S3 Smart Motion Device.

### Project context

The device will eventually include:

* ESP32-S3
* IMU sensor over I2C
* button
* LED or display feedback
* battery power
* TinyML activity classifier
* 3D printed enclosure

This task implements only the first firmware structure milestone.

### Files to create or modify

```text
CMakeLists.txt
main/CMakeLists.txt
main/main.c
components/board_config/CMakeLists.txt
components/board_config/include/board_config.h
components/imu_sensor/CMakeLists.txt
components/imu_sensor/include/imu_sensor.h
components/imu_sensor/imu_sensor.c
```

### Requirements

1. Use ESP-IDF C style.
2. Use `ESP_LOGI` for startup logging.
3. Create a `board_config` component.
4. Define placeholder hardware names in `board_config.h`:
   * `BOARD_LED_GPIO`
   * `BOARD_BUTTON_GPIO`
   * `BOARD_I2C_SDA_GPIO`
   * `BOARD_I2C_SCL_GPIO`
   * `BOARD_I2C_PORT`
5. Add comments that all GPIO values must be verified against the real ESP32-S3 board.
6. Do not use GPIO0 as the default button pin.
7. Create an `imu_sensor` component.
8. Add a stub function:

```c
esp_err_t imu_sensor_init(void);
```

9. In `main.c`, print:
   * startup message
   * selected LED GPIO
   * selected button GPIO
   * selected I2C SDA/SCL GPIO
10. Call `imu_sensor_init()` from `app_main()`.
11. Do not implement real I2C communication yet.
12. Do not implement RTOS tasks.
13. Do not implement TinyML.
14. Do not implement battery logic.
15. Do not implement display logic.

### Acceptance criteria

* `idf.py set-target esp32s3` works.
* `idf.py build` passes.
* `idf.py flash monitor` shows startup logs.
* GPIO values are centralized in `board_config.h`.
* `main.c` stays short and readable.
* IMU logic is separated into its own component.
* The code is simple enough for a beginner to understand.

### Suggested next milestone

After this task works, create:

```text
docs/02_i2c_scanner.md
```

The next implementation goal should be:

* configure I2C master
* scan I2C addresses
* confirm that the IMU appears on the bus
* only then start writing the real IMU driver
