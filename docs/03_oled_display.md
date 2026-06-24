# Milestone 3 — OLED Startup Screen

## 1. Goal

Display a minimal startup screen on the 0.96 inch 128x64 I2C OLED connected to
the ESP32-C3 Smart Motion Keychain.

The first screen should contain only:

```text
SMART KEYCHAIN
READY
```

This milestone will initialize the display, clear it, draw static text, and
leave that text visible. It will not implement animations, ADXL345 support,
RTOS application tasks, or TinyML.

---

## 2. Verified Prerequisites

The following facts are already confirmed:

* board: ESP32-C3 Super Mini
* SDA: GPIO5
* SCL: GPIO6
* OLED power: 3.3 V
* detected OLED I2C address: `0x3C`
* the I2C scanner builds, flashes, and receives ACK from the OLED

The address confirms electrical communication. It does not identify the OLED
controller. Many 0.96 inch 128x64 modules use SSD1306, but some visually similar
modules use another controller such as SH1106. The controller must be verified
from the product listing, board marking, or a controlled initialization test.

---

## 3. What SSD1306 Is

SSD1306 is a display controller commonly used in small monochrome OLED modules.
The controller sits between the ESP32-C3 and the OLED pixels.

The ESP32-C3 does not control each pixel electrically. Instead, it sends the
SSD1306:

* commands that configure the display
* pixel data that determines which pixels are on or off

ESP-IDF 5.3.1 already contains an SSD1306 panel driver inside the `esp_lcd`
component. The future project component `oled_display` should wrap that API and
provide a small application-facing interface.

Using address `0x3C` is common for SSD1306 modules, but the address alone is not
proof that the controller is SSD1306.

---

## 4. Why the OLED Needs Initialization

After power-up, the controller does not know every detail of the connected
panel. Initialization sends commands that establish a known state.

Typical initialization responsibilities include:

* selecting the display height and multiplex ratio
* choosing the display memory addressing mode
* configuring row and column orientation
* enabling the internal charge pump when required
* clearing old or random display memory
* turning the panel on

Without correct initialization, the module may stay black, show shifted data,
display noise, or appear mirrored even when I2C communication works.

---

## 5. Commands and Pixel Data

The same I2C address carries two kinds of information:

* **command data** changes controller settings
* **display data** changes pixels

The OLED controller uses a control byte to distinguish commands from pixel
data. The future driver should use ESP-IDF's panel and panel-I/O APIs instead of
duplicating the entire SSD1306 command protocol in `main.c`.

---

## 6. What a Framebuffer Is

A framebuffer is a block of RAM representing the screen before it is sent to
the display.

The OLED resolution is 128 by 64 pixels, and each monochrome pixel needs one
bit:

```text
128 × 64 = 8192 bits
8192 ÷ 8 = 1024 bytes
```

The program changes bits in this 1024-byte buffer when it draws text or shapes.
It then sends the buffer to the OLED.

Benefits of a framebuffer:

* the screen can be composed before it becomes visible
* clearing the screen is simple
* text and shapes use the same pixel representation
* later animations can reuse the same drawing foundation

The framebuffer should be private to the `oled_display` component. Application
code should not manipulate its bytes directly.

---

## 7. Why `oled_display` Must Be a Separate Component

The display component should have one responsibility: convert simple drawing
requests into OLED output.

This separation keeps responsibilities clear:

```text
main
  coordinates startup

i2c_bus
  owns the shared I2C master bus

oled_display
  initializes the OLED
  owns the framebuffer and font
  clears and updates the screen
```

Later, ADXL345 can use `i2c_bus` without depending on OLED drawing
code. Animation logic can call `oled_display` without knowing I2C commands.

---

## 8. I2C Bus Lifetime Change Needed Later

The current scanner follows this lifecycle:

```text
initialize bus → scan → deinitialize bus
```

That lifecycle is correct for a one-time scanner, but the OLED cannot use a bus
after it has been deinitialized.

The future OLED implementation must keep the bus alive:

```text
initialize bus → optional scan → initialize OLED → draw screen
```

The `i2c_bus` component should remain the owner of the ESP-IDF bus handle. The
OLED component will need controlled access to that handle or a small bus API.
This ownership decision must be explained before implementation.

---

## 9. Minimal OLED Milestone

The smallest useful implementation should:

1. initialize the existing shared I2C bus
2. create OLED panel I/O for address `0x3C`
3. initialize the assumed SSD1306 controller
4. create and clear a 1024-byte framebuffer
5. draw `SMART KEYCHAIN`
6. draw `READY` on a second line
7. send the framebuffer to the OLED
8. log success or a meaningful error

It should not add:

* animation loops
* motion input
* ADXL345 code
* application-created RTOS tasks
* queues or events
* TinyML
* battery behavior

---

## 10. Planned Files

Files expected for the implementation stage:

```text
components/oled_display/CMakeLists.txt
components/oled_display/include/oled_display.h
components/oled_display/oled_display.c
components/oled_display/font_5x7.h
components/i2c_bus/include/i2c_bus.h
components/i2c_bus/i2c_bus.c
main/CMakeLists.txt
main/main.c
```

Responsibilities:

* `oled_display.h` — small public display API
* `oled_display.c` — initialization, framebuffer, text drawing, screen update
* `font_5x7.h` — private bitmap data for a minimal readable font
* `i2c_bus` files — keep the shared bus alive and expose controlled access
* `main.c` — coordinate initialization and report errors
* component CMake files — declare source files and ESP-IDF dependencies

These files were created or changed after the controlled SSD1306 compatibility
test was explicitly accepted.

---

## 11. Small Implementation Plan

1. Confirm that the module is SSD1306-compatible.
2. Decide how `oled_display` receives controlled access to the I2C bus handle.
3. Adjust the bus lifecycle so it is not deleted immediately after scanning.
4. Create the focused `oled_display` component.
5. Initialize ESP-IDF panel I/O at address `0x3C`.
6. Initialize the SSD1306 panel through `esp_lcd`.
7. Add a private 1024-byte framebuffer and minimal 5x7 font.
8. Clear the display and draw the two required text lines.
9. Build, flash, and test on the real OLED.
10. Stop after the static startup screen works.

### Planned code changes, file by file

These are the next changes, but they are not implemented yet:

* `components/board_config/include/board_config.h`
  * add the verified 7-bit OLED address `0x3C`
  * add the display dimensions: width `128`, height `64`
  * keep GPIO mapping and hardware constants in one place
* `components/i2c_bus/include/i2c_bus.h` and `components/i2c_bus/i2c_bus.c`
  * keep the initialized I2C bus alive after the scan
  * provide `oled_display` with controlled access to the shared bus
  * keep ownership of the ESP-IDF I2C bus handle inside `i2c_bus`
* `components/oled_display/CMakeLists.txt`
  * register the new component, its source file, and its dependencies
* `components/oled_display/include/oled_display.h`
  * declare a small public API that returns `esp_err_t`
* `components/oled_display/oled_display.c`
  * initialize the confirmed OLED controller at address `0x3C`
  * own the private 1024-byte framebuffer
  * clear the framebuffer, draw text, and send the result to the OLED
  * log initialization and transmission failures with ESP-IDF logging
* `components/oled_display/font_5x7.h`
  * contain only the private bitmap glyphs needed for the startup text
* `main/CMakeLists.txt`
  * add `oled_display` as a dependency of `main`
* `main/main.c`
  * initialize the I2C bus
  * optionally run the already working scanner
  * initialize the OLED and show `SMART KEYCHAIN` / `READY`
  * stop deinitializing I2C immediately after scanning
  * remain responsible only for startup coordination and error reporting

The first code change should be the I2C bus lifetime/API adjustment. The OLED
component cannot work if `main` deletes the bus immediately after the scan.

---

## 12. Acceptance Criteria

Milestone 3 is complete only when:

* `idf.py build` passes
* firmware flashes to the ESP32-C3
* serial monitor reports successful OLED initialization
* the screen is cleared before text is drawn
* the OLED visibly shows `SMART KEYCHAIN`
* the OLED visibly shows `READY`
* the image remains stable without resets or I2C errors
* `main.c` remains small
* OLED implementation is isolated in `oled_display`
* no ADXL345, animation, RTOS application task, or TinyML code is added

---

## 13. Mistakes to Avoid

### Assuming the controller from the address

Address `0x3C` proves that a device acknowledged. It does not prove SSD1306
compatibility.

### Deinitializing I2C too early

The OLED cannot receive commands or framebuffer data after the shared bus has
been deleted.

### Putting drawing code in `main.c`

`main.c` should coordinate components, not contain fonts, pixel math, or OLED
commands.

### Confusing 7-bit and shifted addresses

Use the verified 7-bit address `0x3C` with the selected ESP-IDF API. Do not
manually shift it unless the API explicitly requires that format.

### Adding animations before static output works

First prove initialization, clear, text rendering, and stable output. Animation
belongs to a later milestone.

### Ignoring abnormal heat

The OLED should not become noticeably hot. Disconnect power immediately if it
heats up again, and recheck VCC/GND and the module for damage.

---

## 14. Codex-Ready Implementation Task

### Task

Implement only the minimal OLED startup screen for the ESP32-C3 Smart Motion
Keychain after confirming that the module is SSD1306-compatible.

### Read first

```text
AGENTS.md
docs/00_project_overview.md
docs/01_esp_idf_project_structure.md
docs/02_i2c_scanner.md
docs/03_oled_display.md
```

### Requirements

1. Explain the I2C bus ownership change before editing code.
2. Reuse the existing `i2c_bus` component.
3. Use the verified 7-bit OLED address `0x3C`.
4. Create a focused `oled_display` component.
5. Prefer ESP-IDF 5.3.1 `esp_lcd` SSD1306 support.
6. Keep a private 1024-byte framebuffer in the display component.
7. Use a small private bitmap font.
8. Clear the screen and show `SMART KEYCHAIN` and `READY`.
9. Use `esp_err_t` and ESP-IDF logging for failures.
10. Keep `main.c` small.
11. Build and test on the real OLED.
12. Do not implement animations.
13. Do not implement ADXL345 support.
14. Do not add application-created RTOS tasks.
15. Do not add TinyML or battery logic.

### Implementation gate

The user explicitly accepted an SSD1306-compatible controlled test. The code
builds successfully, but the controller remains unconfirmed until the real OLED
shows the expected startup screen correctly.

### Controlled-test status

* SSD1306-compatible initialization: implemented
* 1024-byte private framebuffer: implemented
* minimal 5x7 startup font: implemented
* ESP-IDF build: passed
* real OLED output: pending hardware test
* SH1106 implementation: not added

---

## 15. Manual Exercise

On paper, draw three boxes named `main`, `i2c_bus`, and `oled_display`. Draw an
arrow from `main` to both components, and an arrow from `oled_display` to the
shared bus interface. Under each box, write one responsibility it owns and one
responsibility it must not own.

Then calculate the framebuffer size without looking at the answer:

```text
width × height ÷ 8
```

---

### Completed result

Component relationships:

```text
main
 |-- calls i2c_bus: initialize and optionally scan
 |-- calls oled_display: initialize and show the startup screen
                         |
                         `-- uses the shared bus managed by i2c_bus
```

Responsibilities:

* `main` owns startup order and error reporting. It must not own fonts, pixel
  calculations, or OLED commands.
* `i2c_bus` owns I2C initialization and the shared bus handle. It must not own
  OLED text or framebuffer logic.
* `oled_display` owns OLED initialization, the framebuffer, font rendering, and
  screen updates. It must not own application states or sensor logic.

Framebuffer calculation for a 128x64 monochrome display:

```text
128 x 64 = 8192 pixels
8192 pixels x 1 bit = 8192 bits
8192 bits / 8 = 1024 bytes = 1 KiB
```

The calculated answer is correct: one full monochrome framebuffer requires
`1024` bytes.

---

## 16. Review Questions

1. Why does ACK at address `0x3C` not prove that the controller is SSD1306?
2. Why must the I2C bus remain initialized while the OLED is in use?
3. Why does a 128x64 monochrome framebuffer require 1024 bytes?
4. Why should the framebuffer and font stay private to `oled_display`?
5. What exact visible result is required before animation work can begin?

---

## 17. Replacement 7-Pin SPI OLED

The original 4-pin I2C OLED was replaced after the I2C version of Milestone 3
had already passed. The replacement module exposes a 4-wire SPI interface:

```text
OLED GND -> ESP32-C3 GND
OLED VDD -> ESP32-C3 3V3
OLED SCK -> ESP32-C3 GPIO4
OLED SDA -> ESP32-C3 GPIO10 (SPI MOSI, not I2C SDA)
OLED RES -> ESP32-C3 GPIO1
OLED DC  -> ESP32-C3 GPIO3
OLED CS  -> ESP32-C3 GPIO7
```

The framebuffer, font, and public `oled_display` API remain unchanged. Only the
ESP-IDF panel I/O transport changed from I2C to SPI. The `i2c_bus` component is
retained for the ADXL345 but is not required by the SPI OLED.

Current replacement-display status:

* SPI GPIO mapping: configured in `board_config.h`
* diagnostic SPI clock: 1 MHz
* ESP-IDF build: passed
* assumed controller for controlled test: SSD1306-compatible
* real startup screen: pending hardware test
* real animation output: pending hardware test

The first SPI run produced a black screen even though ESP-IDF returned success.
Unlike I2C, SPI has no ACK. The SPI panel I/O was then corrected so SSD1306
command parameters are transmitted with D/C low (`dc_low_on_param`).
