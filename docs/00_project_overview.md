# ESP32-C3 Smart Motion Keychain

## 1. Project Summary

**ESP32-C3 Smart Motion Keychain** is a small keychain-sized embedded device with an OLED display and motion-based interaction.

The device uses an **ESP32-C3**, an **ADXL345 accelerometer**
over I2C, and a **128x64 SPI OLED display**. It reacts to movement and shows
simple animated states on the screen.

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

* Read movement data from the ADXL345.
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
* ADXL345 sensor reading
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
* OLED display, 128x64 pixels, 7-pin SPI
* ADXL345 GY-291 accelerometer module
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
* `adxl345`
  * verifies and initializes ADXL345
  * reads raw X/Y/Z acceleration
* `flip_animation`
  * develops the 288-particle FLIP simulation in parallel
  * remains disconnected from OLED rendering until each checkpoint passes
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
│ ├── adxl345/
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
├── 04_adxl345.md
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
* connected I2C sensor addresses can be checked before writing drivers

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

### Milestone 4 — ADXL345 raw data

Goal:

Verify the ADXL345 identity and read raw acceleration data.

Result:

Serial monitor prints:

* acceleration X/Y/Z
* values change when the device moves

Document:

* `docs/04_adxl345.md`

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
* the verified ESP32-C3 Super Mini GPIO mapping is centralized in `board_config.h`

Milestone 1 was first tested on an ESP32-C6 development board and was later
verified successfully on the final ESP32-C3 Super Mini target.

Current status:

* the original 4-pin I2C OLED was verified at address `0x3C`
* the original OLED displayed `SMART KEYCHAIN`, `READY`, and the animation
* the display has now been replaced by a 7-pin SPI OLED
* the first SPI test initialized in software but the replacement OLED remained
  black
* the SPI command-parameter D/C handling was corrected for SSD1306
* the diagnostic SPI clock is temporarily set to 1 MHz
* the corrected SPI transport and 96-particle grid are verified on hardware
* the ADXL345 GY-291 is connected to GPIO5/GPIO6 over I2C
* the scanner verified the ADXL345 address as `0x53`

Next learning step:

* the ADXL345 driver verifies device ID `0xE5` before measurement
* the FLUID particle preview now receives real ADXL345 X/Y acceleration
* the animation firmware previously ran successfully on the original OLED
* the same framebuffer and animation code is now connected to the SPI transport
* the first blob concept is technically valid but its visual design is not
  accepted; visual refinement is intentionally deferred
* verify raw X/Y/Z logs and particle direction on the real hardware

### Learning Review / Проверка знаний

#### 1. How is building different from flashing?

**English:** Building compiles and links the source code into firmware files on
the computer. Flashing writes those firmware files into the controller's flash
memory. The firmware is normally changed by rebuilding and flashing it again.

**Русский:** Сборка компилирует и компонует исходный код в файлы прошивки на
компьютере. Прошивка записывает эти файлы во flash-память контроллера. Обычно
программу изменяют, повторно собирают и заново прошивают.

#### 2. Why were SDA and SCL initially set to `-1`?

**English:** `-1` meant that the GPIO pins had not been assigned yet. It did not
mean that no devices were connected. After checking the exact development
board pinout, the prototype selected GPIO5 for SDA and GPIO6 for SCL.

**Русский:** `-1` означало, что GPIO ещё не были назначены. Это не означало, что
устройства не подключены. После проверки распиновки платы для прототипа были
выбраны GPIO5 для SDA и GPIO6 для SCL.

#### 3. Which log messages confirm that `app_main()` ran?

**English:** ESP-IDF reports `Calling app_main()`, and the application then logs
`Smart Motion Keychain started`. Together these messages confirm that the entry
point was called and the application code executed.

**Русский:** ESP-IDF выводит `Calling app_main()`, после чего приложение выводит
`Smart Motion Keychain started`. Эти сообщения подтверждают, что точка входа
была вызвана и код приложения выполнился.

### I2C Learning Review / Проверка знаний I2C

#### 1. What does the scanner address range do?

**English:** The start and end definitions set the inclusive range used by the
scanner loop. A range from `0x03` through `0x77` probes every address between
those values. However, `0x03` through `0x07` are reserved or special-purpose
addresses, so this project normally uses `0x08` through `0x77`. The temporary
range `0x3C` through `0x3D` checks only the two common OLED addresses.

**Русский:** Начальное и конечное определения задают включительный диапазон
цикла scanner. Диапазон от `0x03` до `0x77` проверяет все адреса между этими
значениями. Однако `0x03–0x07` зарезервированы или имеют специальное назначение,
поэтому проект обычно использует `0x08–0x77`. Временный диапазон `0x3C–0x3D`
проверяет только два распространённых адреса OLED.

#### 2. What is an I2C bus?

**English:** An I2C bus is a shared communication connection that lets one
controller communicate with multiple addressed devices over the same SDA and
SCL lines. The devices must also share ground.

**Русский:** I2C bus — это общая линия связи, которая позволяет одному
контроллеру общаться с несколькими адресуемыми устройствами через одни и те же
линии SDA и SCL. У устройств также должна быть общая земля.

#### 3. What is SDA?

**English:** SDA means Serial Data. It carries addresses, commands, and data in
both directions between the ESP32-C3 and I2C devices.

**Русский:** SDA означает Serial Data — последовательные данные. Эта линия
передаёт адреса, команды и данные в обоих направлениях между ESP32-C3 и
устройствами I2C.

#### 4. What is SCL?

**English:** SCL means Serial Clock. The I2C controller generates this clock so
all devices know when each bit on SDA is valid and should be read.

**Русский:** SCL означает Serial Clock — последовательный тактовый сигнал.
Контроллер I2C создаёт этот сигнал, чтобы устройства знали, когда каждый бит на
SDA действителен и должен быть прочитан.

#### 5. What is an I2C address?

**English:** An I2C address is normally a 7-bit identifier for one device on a
shared bus. The controller sends the address first so only the matching device
responds. Scanner output uses the raw 7-bit value without the read/write bit.

**Русский:** I2C address — это обычно 7-битный идентификатор устройства на общей
шине. Контроллер сначала отправляет адрес, поэтому отвечает только совпавшее
устройство. Scanner показывает исходное 7-битное значение без бита чтения или
записи.

#### 6. Why does the scanner check addresses?

**English:** The scanner does not yet know which devices are present. It sends
each candidate address and checks for acknowledgement. This confirms the real
address and basic electrical communication before an OLED or sensor driver is
added.

**Русский:** Scanner ещё не знает, какие устройства присутствуют. Он отправляет
каждый возможный адрес и проверяет подтверждение. Это определяет настоящий
адрес и проверяет базовую электрическую связь до добавления драйвера OLED или
датчика.

#### 7. What does ACK mean?

**English:** ACK means acknowledgement. The addressed device pulls SDA low
during the acknowledgement clock pulse to confirm that it received the
address. `i2c_master_probe()` reports this as `ESP_OK`.

**Русский:** ACK означает acknowledgement — подтверждение. Устройство с нужным
адресом притягивает SDA к LOW во время такта подтверждения, сообщая, что адрес
получен. `i2c_master_probe()` возвращает для этого `ESP_OK`.

#### 8. What does NACK mean?

**English:** NACK means not acknowledged. No device pulled SDA low for that
address. During a scan this normally means that the address is unused, and
ESP-IDF reports `ESP_ERR_NOT_FOUND`. NACK differs from a timeout, which can mean
that the bus is stuck or wired incorrectly.

**Русский:** NACK означает отсутствие подтверждения. Ни одно устройство не
притянуло SDA к LOW для этого адреса. Во время scan это обычно означает, что
адрес свободен, а ESP-IDF возвращает `ESP_ERR_NOT_FOUND`. NACK отличается от
timeout, который может означать зависшую шину или неправильное подключение.

#### 9. Why is an OLED usually at `0x3C` or `0x3D`?

**English:** Common SSD1306-compatible OLED controllers use a hardware address
selection input. Depending on how that input or a module solder jumper is
connected, the 7-bit address is usually `0x3C` or `0x3D`. The scanner must still
confirm the address of the actual module.

**Русский:** Распространённые OLED-контроллеры, совместимые с SSD1306, имеют
аппаратный вход выбора адреса. В зависимости от его подключения или перемычки
на модуле 7-битный адрес обычно равен `0x3C` или `0x3D`. Scanner всё равно должен
подтвердить адрес конкретного модуля.

#### 10. Why does `board_config.h` store SDA and SCL?

**English:** SDA and SCL GPIO numbers describe the physical board wiring. They
belong in `board_config.h` so hardware changes are made in one place and driver
code does not contain duplicated raw GPIO numbers. Scanner behavior, such as
its address range, belongs in `i2c_bus.c` instead.

**Русский:** Номера GPIO для SDA и SCL описывают физическое подключение платы.
Они хранятся в `board_config.h`, чтобы аппаратные изменения выполнялись в одном
месте, а драйверы не содержали повторяющиеся необработанные номера GPIO.
Поведение scanner, например диапазон адресов, относится к `i2c_bus.c`.

### OLED Learning Review / Проверка знаний OLED

#### 1. Why was an ACK at `0x3C` not enough to confirm the OLED driver?

**English:** ACK confirmed that a device responded at `0x3C` and that basic I2C
communication worked. It did not confirm the controller model, initialization
commands, framebuffer format, or actual pixel output.

**Русский:** ACK подтвердил, что устройство отвечает по адресу `0x3C` и базовая
связь I2C работает. Он не подтверждал модель контроллера, команды
инициализации, формат framebuffer или реальный вывод пикселей.

#### 2. What confirmed SSD1306 compatibility?

**English:** The driver initialized without errors, the framebuffer was sent,
and the OLED displayed `SMART KEYCHAIN` and `READY` correctly and stably. This
confirms practical SSD1306 compatibility for this project.

**Русский:** Драйвер инициализировался без ошибок, framebuffer был отправлен, а
OLED правильно и стабильно показал `SMART KEYCHAIN` и `READY`. Это подтверждает
практическую совместимость с SSD1306 для данного проекта.

#### 3. Why must the I2C bus remain initialized after scanning?

**English:** The scanner and OLED share the same bus. After scanning, the OLED
still needs that bus to receive initialization commands and framebuffer data.
Deleting the bus would invalidate the handle used by the display driver.

**Русский:** Scanner и OLED используют одну общую шину. После сканирования OLED
всё ещё нужна эта шина для получения команд инициализации и данных framebuffer.
Удаление шины сделало бы handle драйвера дисплея недействительным.

#### 4. Where is the image stored before it is sent to the OLED?

**English:** It is stored in the private static `s_framebuffer` array inside
`oled_display.c`. A 128x64 monochrome image needs 1024 bytes because each pixel
uses one bit.

**Русский:** Изображение хранится в приватном статическом массиве
`s_framebuffer` внутри `oled_display.c`. Монохромному изображению 128x64 нужен
1024-байтовый буфер, потому что каждый пиксель занимает один бит.

#### 5. Why does the image remain after `app_main()` returns?

**English:** Returning from `app_main()` does not turn off the ESP32-C3 or the
OLED. The OLED controller keeps the received image in its own display memory
until power is removed, the controller is reset, or new data overwrites it.

**Русский:** Завершение `app_main()` не выключает ESP32-C3 или OLED. Контроллер
OLED хранит полученное изображение в собственной памяти дисплея до отключения
питания, сброса контроллера или записи новых данных.

---

## 14. Cellular Pixel Sand Specification / Спецификация клеточного пиксельного песка

### 14.1 Status and purpose

| Metadata | Value |
|---|---|
| Author | Project owner |
| Date | 2026-06-23 |
| Status | **Cellular model active; FLIP checkpoint 1 built** |
| Reviewer | Project owner |

The target is a handful of 96 dry sand pixels inside the OLED rectangle. This
uses cellular pseudo-physics rather than continuous rigid-body physics. Every
OLED coordinate is a cell that can be either empty or occupied. Two particles
can never share one cell.

Real ADXL345 X/Y input selects movement direction and movement frequency. A
particle tries the adjacent cell in that direction. If it is occupied, the
particle may slide into one of two alternative neighboring cells. There are no
collision impulses, strong repulsion forces, fractional positions, or particle
velocities.

Current implementation checkpoint:

* the old three-circle blob has been removed
* a private `128 x 64` occupancy grid stores all particle locations
* reset creates exactly 96 occupied cells in a centered 12 by 8 pattern
* one occupied cell always renders as exactly one OLED pixel
* movement direction comes from the signs of ADXL345 X/Y tilt
* movement frequency grows with tilt magnitude
* an occupied target cell is never overwritten
* blocked particles try alternate neighboring cells and form a pile
* display edges act as solid container walls

### 14.2 What the first prototype must contain

* **FR-1:** The grid MUST contain exactly 96 occupied cells after reset.
* **FR-2:** Every occupied cell MUST render as one OLED pixel.
* **FR-3:** No two particles may occupy the same integer coordinate.
* **FR-4:** Normalized ADXL345 X/Y tilt MUST select the movement direction.
* **FR-5:** Display boundaries MUST behave as solid walls.
* **FR-6:** A particle MUST enter a target cell only when it is free.
* **FR-7:** A blocked particle SHOULD try neighboring slide cells.
* **FR-8:** Tilt magnitude MUST control how often grid movement occurs.
* **FR-9:** One particle MUST move at most once during one grid update.
* **FR-10:** The implementation MUST NOT use pair impulses, repulsion,
  cohesion, a new RTOS task, a state machine, or TinyML.

### 14.2.1 Parallel FLIP experiment

The cellular sand engine remains the active OLED renderer and is the fallback
implementation. A separate `flip_animation` component is being developed so a
failed FLIP checkpoint cannot remove the working animation.

Checkpoint 1 contains only the FLIP data model:

```text
particles:       288
particle layout: 24 x 12
fluid grid:      32 x 16 = 512 cells
OLED rendering:  still uses cellular fallback
```

Every FLIP particle stores fractional X/Y position and X/Y velocity. Every
grid cell reserves fields for current and previous velocity, interpolation
weight, pressure, divergence, and a fluid marker. All arrays use static memory;
there is no heap allocation.

The FLIP implementation must pass these checkpoints separately:

1. Validate particle/grid memory and initial bounds.
2. Transfer particle velocity to the grid and back.
3. Add pressure projection for approximate incompressibility.
4. Add a spatial hash and particle separation.
5. Render FLIP particles and switch from the cellular fallback only after a
   successful real-device test.

The Fluid Pendant article states that its exact source is not public. This
project therefore implements its own learning-oriented FLIP approximation
based on the published algorithm, rather than claiming a direct port.

### 14.3 Archived continuous-model notes (superseded)

> Sections 14.3 through 14.17 below describe the earlier float/velocity
> experiment. They are retained only as learning history and are not the
> current implementation contract. Sections 14.1 and 14.2 define the active
> cellular model.

#### Former particle data model

Each particle needs four independent values:

| Field | Type | Meaning | Constraint |
|---|---|---|---|
| `x` | `float` | horizontal subpixel position | visible range after collision resolution |
| `y` | `float` | vertical subpixel position | visible range after collision resolution |
| `velocity_x` | `float` | horizontal velocity | finite pixels per second |
| `velocity_y` | `float` | vertical velocity | finite pixels per second |

The values should use `float` for the first learning implementation. The
particle is converted to integer coordinates only during rendering. For
example, a particle can move through positions `24.10`, `24.35`, and `24.70`
before it is finally rendered at pixel 25. Keeping only integer positions would
discard these small movements and make the animation jump or stop at low
speeds.

Suggested organization inside `fluid_animation.c`:

```text
particle type
    x
    y
    velocity_x
    velocity_y

private array
    96 particles
```

The array should remain private to the animation component. The public
`fluid_animation_render()` API does not need to change.

#### API contract

```c
void fluid_animation_reset(void);

esp_err_t fluid_animation_render(float tilt_x,
                                 float tilt_y,
                                 float delta_seconds);
```

`fluid_animation_reset()` MUST initialize all 96 private particle records.
`fluid_animation_render()` MUST clamp normalized tilt inputs, advance the
simulation, draw one framebuffer, and return the result of OLED transmission.
No particle array or mutable particle pointer may be exposed through the public
header.

### 14.4 Initial particle placement

`fluid_animation_reset()` should place the 96 particles in a compact 12 by 8
grid near the center of the display:

```text
12 columns x 8 rows = 96 particles
```

Start with one pixel of space between neighboring integer positions and set all
velocities to zero. Calculate the starting offset from `BOARD_OLED_WIDTH` and
`BOARD_OLED_HEIGHT` instead of hardcoding an absolute top-left position.

This regular arrangement is intentional. Random placement can create several
particles at the same location and makes the first collision test harder to
understand. Small random offsets may be studied later, after the regular version
works.

### 14.5 Fixed physics steps

The OLED and the simulation do not need to run at the same frequency. The
replacement OLED uses SPI, so a complete framebuffer update can be much faster
than it was over 100 kHz I2C after hardware verification. Physics timing should still remain
independent from display transmission timing.

Current timing target:

```text
display input step: 0.02 seconds (50 Hz)
physics substep:    0.004 seconds
steps per frame:    5
framebuffer sends:  once per displayed frame
```

For every displayed frame:

```text
split the 0.02-second frame interval into 5 parts
repeat 5 times
    integrate gravity and position
    resolve particle and wall collisions

clear framebuffer
draw all 96 particles
present framebuffer once
```

Do not call `oled_display_present()` after every physics step. That would send
five almost identical framebuffers and would make I2C transmission the main
workload.

Non-functional requirements:

* **NFR-1:** One frame MUST represent `0.02 s` split into five stable substeps.
* **NFR-2:** One displayed frame MUST use five physics substeps and exactly one
  framebuffer transmission.
* **NFR-3:** All particle positions and velocities MUST remain finite during a
  continuous five-minute hardware test.
* **NFR-4:** Pair processing SHOULD initially use the direct unique-pair method
  so the implementation remains understandable.
* **NFR-5:** Rendering or OLED errors MUST be returned to `main.c` rather than
  hidden.

### 14.6 Order of one physics step

Implement and test one stage at a time in this order:

1. Apply tilt-directed gravity to every particle velocity.
2. Update particle positions.
3. Process every unique particle pair once (`i < j`).
4. Separate overlapping equal-mass particles equally.
5. Apply a low-restitution collision impulse.
6. Exchange tangential velocity to approximate grain friction.
7. Resolve collisions with the four screen boundaries.
8. Clamp unreasonable velocity.

Liquid-specific viscosity and cohesion are intentionally deferred because the
current requested behavior is dry sand rather than a connected liquid blob.

### 14.7 Simulated gravity

The existing normalized `tilt_x` and `tilt_y` inputs remain in the range
`-1.0` to `1.0`. Treat them as the direction and strength of gravity:

```text
acceleration_x = tilt_x * gravity_strength
acceleration_y = tilt_y * gravity_strength

velocity_x += acceleration_x * step_seconds
velocity_y += acceleration_y * step_seconds
```

The first static checkpoint used zero input. The current checkpoint converts
ADXL345 X/Y acceleration into these two values; the sensor does not replace
particle physics or rendering.

### 14.8 Pair interaction rules

For every unique pair, calculate the horizontal and vertical difference and
the squared distance. Compare squared distances where possible so a square-root
calculation is not performed for pairs that are too far apart.

#### Repulsion

Repulsion prevents particles from remaining on top of each other. If the
distance is smaller than the minimum separation, move or accelerate the two
particles in opposite directions. Apply equal and opposite corrections so the
pair does not gain motion in only one direction.

If the distance is zero, there is no valid direction between the particles.
Use a small deterministic separation direction in this case. Never divide by
zero.

#### Collision response

An overlapping pair receives a low-restitution impulse along its collision
normal. Equal and opposite changes preserve the equal-mass model and prevent
one particle from receiving all collision energy.

#### Friction

Tangential relative velocity is reduced by a small fraction during contact.
This lets grains settle into a pile instead of bouncing forever. No attraction
is applied between grains that are not touching.

### 14.9 Boundary collisions

The valid visible coordinates are:

```text
x: 0 through BOARD_OLED_WIDTH - 1
y: 0 through BOARD_OLED_HEIGHT - 1
```

When a particle crosses a boundary:

1. return its position to the nearest valid coordinate
2. reverse the velocity component perpendicular to that wall
3. multiply the reversed velocity by wall restitution

A restitution below `1.0` removes energy on impact. A value of `1.0` produces a
perfect bounce and usually looks less like liquid.

### 14.10 Starting parameter ranges

These are starting ranges for experiments, not final visual settings:

| Parameter | Suggested starting range | Effect |
|---|---:|---|
| particle count | exactly 96 | size of the first experiment |
| physics step | `0.02 s` | simulation stability |
| substeps per frame | `5` | physics updates per OLED frame |
| gravity strength | `220 px/s²` | responsive ADXL345 tilt on the small OLED |
| minimum separation | `1.0–1.5 px` | particle overlap prevention |
| collision solver passes | `2` | overlap correction quality |
| wall restitution | `0.12` | low wall bounce for dry grains |
| particle restitution | `0.05` | low grain-to-grain bounce |
| friction fraction | `0.18` | tangential velocity exchange |

Change only one parameter at a time and record the visible result. First find a
stable result; visual refinement comes after stability.

### 14.11 Rendering

Rendering should not change particle physics:

1. clear the framebuffer once
2. round or convert every particle position to integer `x` and `y`
3. call `oled_display_set_pixel(x, y, true)` once per particle
4. call `oled_display_present()` once after all particles are drawn
5. return the display error to the caller

Two particles can occasionally round to the same OLED pixel even when their
fractional positions differ. This is acceptable in the first prototype. The
physics should still keep their fractional positions separate.

### 14.12 Complexity and later optimization

The first implementation may compare every particle with every other particle.
For 96 particles, processing only unique pairs requires:

```text
96 x 95 / 2 = 4560 pair checks per physics step
```

This direct approach is suitable for learning because it is easy to inspect.
Do not add a spatial grid until hardware measurement shows that pair processing
is too slow. A later version with several hundred particles should use a grid
or buckets so each particle checks only nearby cells.

### 14.13 Edge cases to handle

* **EC-1:** Two particles have exactly the same position.
* **EC-2:** `delta_seconds` is zero or negative.
* **EC-3:** A delayed frame produces an unexpectedly large `delta_seconds`.
* **EC-4:** A particle crosses a wall by more than one pixel during one step.
* **EC-5:** Velocity grows until particles continuously jump across the display.
* **EC-6:** Several particles render to the same integer pixel.
* **EC-7:** `oled_display_present()` returns an error.

Clamp an excessive time step and, if necessary, excessive velocity. Do not hide
OLED transmission errors.

### 14.14 Implementation sequence for manual learning

Complete and test these checkpoints separately:

1. Replace the single blob state with 96 particle records.
2. Place a static 12 by 8 pixel group and verify that exactly 96 particles are
   processed.
3. Add tilt gravity without pair interaction.
4. Add wall collisions and verify all four edges.
5. Add pair repulsion and test overlapping particles.
6. Add grain friction and low restitution.
7. Run five physics substeps for each OLED frame.
8. Verify that the group forms a pile instead of one overlapping pixel.
9. Measure whether the hardware animation remains responsive.
10. Document the chosen constants and why they were selected.

After every checkpoint, build, flash, and observe the real OLED before adding
the next behavior.

### 14.15 Acceptance criteria

* **AC-1 (FR-1, FR-3):** Given a reset, when the first frame is rendered, then
  all 96 particle records are initialized in a centered 12 by 8 arrangement.
* **AC-2 (FR-4):** Given measured tilt, when physics advances, then the
  particle group moves in the tilt direction.
* **AC-3 (FR-5):** Given a particle reaches any display edge, when the next step
  completes, then its position remains inside the valid display coordinates.
* **AC-4 (FR-6, EC-1):** Given two particles are closer than the minimum
  separation, when repulsion is applied, then their separation increases
  without division by zero.
* **AC-5 (FR-7):** Given two grains collide, their normal approach speed is
  reduced and tangential velocity is exchanged through friction.
* **AC-6 (FR-8):** Given sustained tilt toward a wall, particles form a visible
  multi-pixel pile instead of collapsing into one coordinate.
* **AC-7 (FR-9, NFR-1, NFR-2):** Given one display frame is produced, then
  physics advances five times but the framebuffer is transmitted only once.
* **AC-8 (FR-2):** Given 96 initialized particles, when rendering completes,
  then each particle contributes exactly one `oled_display_set_pixel()` call.
* **AC-9 (FR-4):** Given valid ADXL345 samples, normalized `tilt_x` and
  `tilt_y` values move the particle group in the corresponding direction.
* **AC-10 (FR-10):** The implementation creates no new RTOS task and does not
  add motion states, metaballs, TinyML, or another sensor driver.
* **AC-11 (NFR-3):** Given a five-minute continuous hardware run, then all
  particle positions and velocities remain finite.
* **AC-12 (NFR-5, EC-7):** Given OLED transmission fails, when rendering
  returns, then the same non-success `esp_err_t` reaches `main.c`.
* **AC-13 (EC-2, EC-3):** Given a non-positive or unexpectedly large frame
  delta, when rendering begins, then the physics step is clamped to the defined
  safe range and no non-finite value is produced.
* **AC-14 (EC-4, EC-5):** Given a particle crosses a wall or reaches excessive
  velocity, when collision resolution completes, then the particle returns to
  the visible range and its velocity remains within the selected safety limit.
* **AC-15 (EC-6):** Given multiple fractional positions round to one OLED
  pixel, when the frame is rendered, then the framebuffer remains valid and the
  particles keep their independent fractional states.
* **AC-16 (NFR-4):** Given 96 particles, when one direct pair pass completes,
  then every unique pair is processed once for a total of 4560 checks.

### 14.16 Out of scope for the first prototype

* **OS-1:** Physically accurate Navier–Stokes simulation — too complex for this
  learning checkpoint.
* **OS-2:** SPH density and pressure solver — reserved for a later experiment.
* **OS-3:** Metaball or filled-surface rendering — particles must remain visible
  as individual pixels.
* **OS-4:** Hundreds or thousands of particles — performance is not yet
  measured.
* **OS-5:** Spatial acceleration grid — direct pair checks are easier to study.
* **OS-6:** Gyroscope input — ADXL345 provides acceleration only.
* **OS-7:** Shake, stillness, FLUID, SLEEP, and TIME transitions — separate
  milestones.
* **OS-8:** Automatic 400 kHz I2C change — requires a separate hardware test.

### 14.17 Answers to the review questions / Ответы на вопросы

#### 1. Why should particle coordinates not be stored only as integers?

**English:** Physics often moves a particle by less than one pixel during a
single step. Fractional coordinates preserve these small changes until they
accumulate into visible movement. Integer-only coordinates would repeatedly
discard the fractional part, causing jerky motion or preventing slow movement
entirely.

**Русский:** За один физический шаг частица часто перемещается меньше чем на
один пиксель. Дробные координаты сохраняют маленькие изменения, пока они не
накопятся в видимое движение. Целые координаты каждый раз отбрасывали бы
дробную часть, поэтому движение стало бы дёрганым или полностью остановилось
бы на маленькой скорости.

#### 2. What keeps the particles together?

**English:** Weak cohesion attracts nearby particles, while viscosity reduces
differences in their velocities. Cohesion alone would collapse the group, so
short-range repulsion must remain stronger and prevent overlap. The balance of
repulsion, viscosity, and cohesion creates the simplified liquid behavior.

**Русский:** Слабое сцепление притягивает соседние частицы, а вязкость уменьшает
разницу между их скоростями. Одно только сцепление сжало бы все частицы в одну
точку, поэтому отталкивание на маленькой дистанции должно быть сильнее и не
давать частицам накладываться. Баланс отталкивания, вязкости и сцепления создаёт
упрощённое поведение жидкости.

#### 3. Why perform several physics steps for one displayed frame?

**English:** Smaller physics steps make acceleration and collisions more stable
and reduce the chance that a particle jumps through another particle or a wall.
The OLED is slower than the desired physics frequency, so several simulation
steps can be calculated before one framebuffer transmission. This improves
physics without increasing I2C traffic.

**Русский:** Маленькие физические шаги делают ускорение и столкновения
стабильнее и уменьшают вероятность того, что частица перескочит через другую
частицу или стену. OLED обновляется медленнее желаемой частоты физики, поэтому
можно вычислить несколько шагов симуляции перед одной отправкой framebuffer.
Физика становится точнее, а количество передач по I2C не увеличивается.

---

## 15. Portfolio Description Draft

ESP32-C3 Smart Motion Keychain is a compact embedded product prototype with an OLED display and accelerometer-based interaction. The device reacts to motion, displays fluid-like animations, enters a sleep animation after inactivity, and shows a time-style animation after a shake gesture.

The project demonstrates ESP-IDF firmware architecture, I2C sensor/display integration, motion detection, state-machine design, embedded UI, and early industrial design considerations for a small 3D printed keychain enclosure.
