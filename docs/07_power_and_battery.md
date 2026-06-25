# Power and Battery Plan

## Goal

Optimize firmware power consumption as much as possible without removing the
main product behavior:

- `FLUID` animation while the keychain is active.
- `SLEEP` visible idle animation after stillness.
- `TIME` screen after shake or phone sync.
- BLE phone time sync when the local clock is stale.
- ADXL345 motion detection.

The first target battery discussed for the prototype is a 3.7 V LiPo around
350 mAh. With the current measured current near 50 mA, this is close to the
minimum for about 6 hours, so software optimization is required.

## Current Estimate

Current measured consumption:

```text
~50 mA
```

Rough runtime with 350 mAh:

```text
350 mAh / 50 mA = 7 hours theoretical
```

Realistic runtime is lower because LiPo capacity is not fully usable and power
conversion has losses:

```text
about 4.5-6 hours realistic
```

## Power Optimization Backlog

### P0 - Measure Before Changing

- [ ] Measure current in `FLUID`.
- [ ] Measure current in `SLEEP`.
- [ ] Measure current in `TIME`.
- [ ] Measure current during BLE advertising.
- [ ] Measure current during BLE connection/time sync.
- [ ] Record whether current is measured from USB 5 V or from the 3.3 V rail.

### P1 - Keep Features, Reduce Display Cost

- [ ] Lower OLED contrast in `SLEEP`.
- [ ] Keep `SLEEP` animation at low FPS.
- [ ] Add a longer-stillness mode that turns OLED off but keeps wake behavior.
- [ ] Keep `TIME` screen duration limited.
- [ ] Avoid unnecessary full-screen refreshes when content has not changed.

### P2 - Reduce CPU Work

- [ ] Cap `FLUID` animation FPS to a measured target.
- [ ] Reduce CPU work in `SLEEP` and `TIME`.
- [ ] Measure FLIP frame time after each tuning change.
- [ ] Avoid logging too often in normal battery builds.

### P3 - Reduce BLE Cost

- [ ] Start BLE advertising only when `device_clock` is stale.
- [ ] Stop advertising immediately after successful phone sync.
- [ ] Investigate fully stopping or deinitializing the BLE controller after sync.
- [ ] Keep Android auto-sync interval conservative so the phone is not scanning
      continuously.

### P4 - Use ADXL345 Low-Power Features

- [ ] Configure ADXL345 low-power/data-rate settings for idle states.
- [ ] Investigate ADXL345 activity/inactivity interrupt pins.
- [ ] Wake active animation from accelerometer interrupt instead of polling
      frequently.
- [ ] Keep raw polling only where it is needed for visible animation.

### P5 - ESP32-C3 Sleep Modes

- [ ] Add light sleep for long `SLEEP` periods.
- [ ] Keep wake sources compatible with ADXL345 interrupt.
- [ ] Do not add deep sleep until OLED-off behavior and wake flow are accepted.
- [ ] Verify that BLE sync still works after returning from sleep.

## Target Current Budget

The practical target is to reduce average current, not necessarily active
current.

Possible target:

```text
FLUID active:              35-50 mA
TIME visible:              25-40 mA
SLEEP visible animation:   10-20 mA
Long stillness OLED off:    1-5 mA
```

If the average current can be reduced to about 25 mA:

```text
350 mAh / 25 mA = 14 hours theoretical
```

If the average current can be reduced to about 15 mA:

```text
350 mAh / 15 mA = 23 hours theoretical
```

The exact result must be measured on real hardware.

## Acceptance Criteria

- [ ] The keychain still has `FLUID`, `SLEEP`, and `TIME`.
- [ ] BLE time sync still works.
- [ ] The OLED still shows visible feedback unless the device is in an accepted
      long-stillness OLED-off mode.
- [ ] Average current is measured before and after optimization.
- [ ] Runtime estimate is updated from measured current, not only from theory.

