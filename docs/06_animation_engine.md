# Milestone 6 Preview — Fluid Animation Prototype

## Goal

Create a cellular sand animation whose movement accepts normalized tilt independently
of the sensor model. The current implementation receives real ADXL345 X/Y
acceleration and renders 96 uniquely occupied pixel cells on the verified
128x64 OLED.

The ADXL345 raw-data connection is implemented. Motion-state detection remains
a separate milestone.

## Architecture

```text
main
  generates temporary tilt_x and tilt_y
       |
       v
fluid_animation
  updates position, velocity, damping, and boundary response
       |
       v
oled_display
  owns pixels, the 1024-byte framebuffer, and OLED transmission
```

ADXL345-derived tilt now replaces the temporary values previously generated in
`main`. The animation API remains sensor-independent.

## Input Contract

`fluid_animation_render()` receives:

* `tilt_x` from `-1.0` (left) to `1.0` (right)
* `tilt_y` from `-1.0` (up) to `1.0` (down)
* elapsed time in seconds

Inputs outside the normalized range are clamped. This prevents unexpected
sensor values from producing unbounded acceleration later.

## Minimal Algorithm

Each frame:

1. tilt adds acceleration to the blob velocity
2. damping gradually reduces velocity
3. velocity changes the blob position
4. display boundaries reflect part of the velocity
5. three overlapping filled circles form one deforming blob
6. the completed framebuffer is sent to the OLED

This is deliberately not a physical fluid simulation. It is a small visual
model suitable for validating component boundaries and display performance.

## Timing

The replacement OLED starts with a conservative 1 MHz diagnostic SPI clock.
After the display is verified, SPI can be increased and will send the complete
1024-byte frame faster than the original 100 kHz I2C transport. No new application
task is created; the prototype runs as a simple loop inside ESP-IDF's existing
main task and yields briefly after each frame.

## Files

```text
components/fluid_animation/CMakeLists.txt
components/fluid_animation/include/fluid_animation.h
components/fluid_animation/fluid_animation.c
components/oled_display/include/oled_display.h
components/oled_display/oled_display.c
main/CMakeLists.txt
main/main.c
```

## Acceptance Criteria

* the project builds for ESP32-C3
* the OLED still initializes successfully
* the 96-particle group moves in response to measured tilt
* the blob remains inside the visible display area
* no separate RTOS tasks, queues, motion states, or TinyML are added
* sensor input can be replaced without changing `fluid_animation`

## Current Test Result

* ESP32-C3 build: passed
* real OLED rendering: passed
* ADXL345 integration build: passed
* real ADXL345-controlled movement: hardware test pending
* visual design approval: not passed
* decision: keep this as a technical prototype and refine the FLUID appearance
  later

## Out of Scope

* shake and stillness detection
* FLUID/SLEEP/TIME state transitions
* complex particle or fluid simulation
* configurable animation themes

## What you should understand before moving on

1. Rendering is independent from the source of motion data.
2. Velocity and damping create inertia instead of directly placing the blob.
3. The OLED component owns framebuffer memory and hardware transmission.

## Manual exercise

Place the prototype flat and record raw X/Y/Z. Tilt it toward each edge and
compare the changing axis with particle motion. Do not change axis signs until
all four directions have been recorded.

## Review questions

1. Why are tilt values normalized?
2. What is the difference between position, velocity, and acceleration?
3. What does damping do?
4. Why does `fluid_animation` call `oled_display` instead of accessing the
   framebuffer directly?
5. Why does `fluid_animation` not include `adxl345.h` directly?
