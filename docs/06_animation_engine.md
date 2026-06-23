# Milestone 6 Preview — Fluid Animation Prototype

## Goal

Create a software-only fluid animation before the MPU6050 is available. The
prototype uses simulated normalized tilt values and renders a moving liquid-like
blob on the verified 128x64 SSD1306-compatible OLED.

This is an early preview of Milestone 6. It does not mark the MPU6050 and motion
state milestones as completed.

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

Later, MPU6050-derived tilt will replace only the temporary values generated in
`main`. The animation API will remain the same.

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

The I2C bus remains at the already verified 100 kHz speed. A complete 1024-byte
frame is the main frame-rate limitation. No new application task is created;
the prototype runs as a simple loop inside ESP-IDF's existing main task and
yields briefly after each frame.

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
* one connected blob moves smoothly around the screen
* the blob remains inside the visible display area
* no separate RTOS tasks, queues, MPU6050 code, or TinyML are added
* simulated input can later be replaced without changing `fluid_animation`

## Current Test Result

* ESP32-C3 build: passed
* real OLED rendering: passed
* continuous simulated movement: passed
* visual design approval: not passed
* decision: keep this as a technical prototype and refine the FLUID appearance
  later

## Out of Scope

* real accelerometer input
* shake and stillness detection
* FLUID/SLEEP/TIME state transitions
* complex particle or fluid simulation
* configurable animation themes

## What you should understand before moving on

1. Rendering is independent from the source of motion data.
2. Velocity and damping create inertia instead of directly placing the blob.
3. The OLED component owns framebuffer memory and hardware transmission.

## Manual exercise

Change only the simulated `tilt_y` amplitude in `main.c` from `0.45f` to
`0.0f`, rebuild, and observe which direction of movement disappears. Restore
`0.45f` after the test.

## Review questions

1. Why are tilt values normalized?
2. What is the difference between position, velocity, and acceleration?
3. What does damping do?
4. Why does `fluid_animation` call `oled_display` instead of accessing the
   framebuffer directly?
5. What code will be replaced when the MPU6050 becomes available?
