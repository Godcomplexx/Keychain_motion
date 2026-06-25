# 2026-06-25 Daily Changes

## Summary

Today the project moved from a display-and-motion prototype to a three-state
keychain firmware with phone time synchronization.

Current user-facing states:

- `FLUID` - active FLIP particle animation driven by ADXL345 movement.
- `SLEEP` - slow low-brightness idle animation after stillness.
- `TIME` - date, time, and daily step count screen.

The Android companion app was also added so the phone can send current local
time to the keychain over BLE.

## Firmware Changes

### OLED and I2C

- The firmware now uses the I2C SSD1306 OLED on the shared I2C bus.
- Current OLED wiring:
  - `SDA -> GPIO5`
  - `SCL -> GPIO6`
  - `VCC -> 3.3V`
  - `GND -> GND`
- The ADXL345 accelerometer remains on the same I2C bus.
- I2C speed was unified through `board_config.h`.

### Motion States

Added dedicated motion-state logic:

- `motion_state`
- `motion_detector`

The firmware now handles:

- `FLUID -> SLEEP` after stillness timeout.
- `SLEEP -> FLUID` after movement.
- `FLUID/SLEEP -> TIME` after shake.
- `TIME -> FLUID/SLEEP` after the TIME duration ends.
- `FLUID/SLEEP -> TIME` after phone time sync through `TIME_REQUESTED`.

### FLUID Animation

- The active animation uses the FLIP particle renderer.
- Particle rendering was enlarged.
- Particle count and separation were tuned for the OLED/ESP32-C3 performance.
- Logs now include frame timing for the FLIP stages.

### SLEEP Animation

- A sprite-sheet based idle animation was added.
- SLEEP animation updates slowly for lower power use.
- Current SLEEP frame interval is about 300 ms, around 3.3 FPS.
- OLED contrast is reduced in SLEEP instead of turning the display fully off.

### TIME Screen

Added `time_animation`:

- Displays time as `HH:MM:SS`.
- Displays date as `YYYY-MM-DD`.
- Displays daily steps as `STEPS N`.
- Shows a progress bar for the TIME state duration.

### Software Clock

Added `device_clock`:

- Starts from firmware build time as a fallback.
- Can be updated from phone-provided time.
- Marks time as stale if it was never phone-synced or if the last sync is too old.

### BLE Phone Sync

Added `phone_sync`:

- BLE device name: `KeychainSync`.
- Uses NimBLE.
- Starts BLE advertising when the device clock is stale.
- Accepts phone time in text format:

  ```text
  YYYY-MM-DD HH:MM:SS
  ```

- On valid write:
  - logs `Phone time received`
  - updates `device_clock`
  - logs `Clock synced from phone`
  - stops BLE advertising
  - requests the `TIME` screen

### Step Counter

Added `step_counter`:

- Keeps a daily step count.
- Resets when the software date changes.
- Uses acceleration magnitude plus fast/slow filtering.
- Counts a step on a separated peak, not on every raw movement delta.

This is still approximate and needs real walking calibration.

## Android Companion App

Added a separate Android project:

```text
android_time_sync/
```

The app:

- scans for `KeychainSync`
- connects through BLE GATT
- finds the exact time characteristic
- writes the current phone time
- disconnects

Important expected app log:

```text
Exact time characteristic found
Writing 2026-06-25 16:10:00
Characteristic 11223344-5566-7788-9a49-315b10371343
Time sent successfully
```

Important expected ESP-IDF monitor log:

```text
phone_sync: Phone time received: ...
keychain: Clock synced from phone: ...
motion_state: FLUID -> TIME by TIME_REQUESTED
phone_sync: BLE advertising stopped
```

### Auto Sync Mode

The Android app now has `Start auto sync`.

This starts a foreground service:

- shows a persistent notification
- scans periodically for `KeychainSync`
- syncs time when the keychain is advertising

The app does not need to stay open on screen, but the foreground notification
must remain enabled.

## Build Results

Firmware build passed:

```text
idf.py build: OK
```

Android debug APK build passed:

```text
assembleDebug: OK
```

Current APK path:

```text
android_time_sync/app/build/outputs/apk/debug/app-debug.apk
```

## Current Test Notes

Known confirmed behavior:

- OLED and ADXL345 are detected on I2C.
- FLUID and SLEEP states work.
- Shake can open TIME.
- BLE advertising starts as `KeychainSync`.
- Phone can connect to the keychain.

Still needs hardware confirmation:

- Android app must produce `Exact time characteristic found`.
- ESP32 monitor must show `Phone time received`.
- ESP32 monitor must show `Clock synced from phone`.
- Step counter thresholds need real walking calibration.
- Android background sync should be tested with the phone screen locked.

## Git Commands

Use these commands to commit and push this document:

```powershell
cd D:\key_chain
git status
git add docs/11_2026_06_25_daily_changes.md
git status
git commit -m "docs: summarize 2026-06-25 firmware and app changes"
git push origin main
```

After push, check:

```powershell
git status --short --branch
```

Expected clean result:

```text
## main...origin/main
```

