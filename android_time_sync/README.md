# Keychain Time Sync Android App

This is a minimal Android companion app for the ESP32-C3 keychain firmware.

The app has two modes:

1. Manual sync from the app screen.
2. Auto sync from a foreground service.

Each sync attempt does one job:

1. Scan for the BLE device named `KeychainSync`.
2. Connect with BLE GATT.
3. Send the phone's current local time as text:

   ```text
   YYYY-MM-DD HH:MM:SS
   ```

4. Disconnect.

Auto sync keeps a foreground notification visible and tries once per minute.
This is intentional: Android can stop hidden background BLE scans, while a
foreground service is the reliable beginner-friendly option.

## Build in Android Studio

1. Open Android Studio.
2. Open this folder:

   ```text
   D:\key_chain\android_time_sync
   ```

3. Let Android Studio sync Gradle.
4. Connect an Android phone by USB.
5. Press Run.

## Test

1. Flash the ESP32 firmware.
2. Keep `idf.py -p COM20 monitor` open.
3. Open the Android app.
4. Tap `Sync now`.

For background sync:

1. Open the app once.
2. Allow Bluetooth and notification permissions.
3. Tap `Start auto sync`.
4. Leave the notification enabled.

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
