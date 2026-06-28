# Keychain Time Sync Android App

This is a minimal Android companion app for the ESP32-C3 keychain firmware.

The app has two modes:

1. Manual sync from the app screen.
2. Auto sync from a foreground service.
3. Start the Breakout game on the keychain.

Each sync attempt does one job:

1. Scan for the BLE device named `KeychainSync`.
2. Connect with BLE GATT.
3. Send the phone's current local time as text:

   ```text
   YYYY-MM-DD HH:MM:SS
   ```

4. Disconnect.

Auto sync keeps a foreground notification visible. It scans continuously until
the keychain's 60-second BLE window appears after a triple shake, sends the
current time, waits 30 seconds, and starts watching again. The preference is
saved, the service remains active when the app screen is closed, and it resumes
after a phone reboot. Background scans use Android's low-power BLE mode in
30-second cycles with 5-second pauses. Manual sync and game commands retain the
faster balanced scan mode.

## Build in Android Studio

1. Open Android Studio.
2. Open this folder:

   ```text
   E:\Projects\keychain\SmartMotion_Keychain\android_time_sync
   ```

3. Let Android Studio sync Gradle.
4. Connect an Android phone by USB.
5. Press Run.

## Test

1. Flash the ESP32 firmware.
2. Keep `idf.py -p COM13 monitor` open.
3. Open the Android app.
4. Tap `Sync now`.

For background sync:

1. Open the app once.
2. Allow Bluetooth and notification permissions.
3. Tap `Start auto sync`.
4. Leave the notification enabled.
5. Allow autostart and unrestricted battery use for `Keychain Sync` if the
   phone firmware provides these settings.
6. Close the app screen if desired.
7. Shake the keychain three times within 2.5 seconds.

The keychain opens the TIME screen only after the phone writes fresh time. If
no phone answers within 60 seconds, the keychain stops advertising and keeps
its previous display state.

## Breakout

Breakout can only be started by the Android app:

1. Tap `Start Breakout`.
2. Shake the keychain three times within 60 seconds.
3. Wait for `Breakout started`.
4. Tilt the keychain left or right to move the paddle.

When auto sync is enabled, its foreground service prioritizes the queued game
command over time synchronization. The ESP32 powers BLE down immediately after
receiving `GAME:START`. Levels are generated locally, become gradually faster,
and the game returns to FLUID after three lost balls or five minutes without
paddle input. Active play has no time limit. To restart an active game, tap
`Start Breakout` and shake three times; the gesture closes the current game,
opens BLE, and lets the queued phone command start a new one.

Expected Android app log:

```text
Scanning for KeychainSync
Found KeychainSync, connecting
Connected, discovering services
Exact time characteristic found
Writing 2026-06-25 14:35:00
Characteristic 11223344-5566-7788-9a49-315b10371343
Time sent successfully
```

Expected ESP-IDF monitor log:

```text
phone_sync: Phone connected
phone_sync: Phone time received: 2026-06-25 14:35:00
keychain: Clock synced from phone: 2026-06-25 14:35:00
phone_sync: BLE advertising stopped
```
