package com.smartmotion.keychaintimesync;

import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;

import java.nio.charset.StandardCharsets;
import java.time.LocalDateTime;
import java.time.ZonedDateTime;
import java.time.format.DateTimeFormatter;
import java.util.List;
import java.util.Collections;
import java.util.UUID;

final class KeychainBleSync {
    private enum Operation {
        TIME_SYNC,
        START_GAME
    }

    interface Listener {
        void onLog(String message);

        void onFinished(boolean success);
    }

    static final String DEVICE_NAME = "KeychainSync";

    private static final long TIME_SCAN_TIMEOUT_MS = 12000L;
    private static final long TIME_OPERATION_TIMEOUT_MS = 25000L;
    private static final long BACKGROUND_SCAN_TIMEOUT_MS = 30000L;
    private static final long BACKGROUND_OPERATION_TIMEOUT_MS = 35000L;
    private static final long GAME_SCAN_TIMEOUT_MS = 60000L;
    private static final long GAME_OPERATION_TIMEOUT_MS = 70000L;
    private static final UUID TIME_SERVICE_UUID =
            UUID.fromString("11223344-5566-7788-9a49-315b10371342");
    private static final UUID TIME_CHARACTERISTIC_UUID =
            UUID.fromString("11223344-5566-7788-9a49-315b10371343");

    private final Context context;
    private final Listener listener;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    private BluetoothLeScanner scanner;
    private BluetoothGatt gatt;
    private boolean running;
    private boolean scanning;
    private boolean finished;
    private boolean writeStarted;
    private int attemptId;
    private Operation operation = Operation.TIME_SYNC;
    private boolean lowPowerScan;

    private final ScanCallback scanCallback = new ScanCallback() {
        @Override
        public void onScanResult(int callbackType, ScanResult result) {
            BluetoothDevice device = result.getDevice();
            String name = scanName(result);

            if (DEVICE_NAME.equals(name)) {
                log("Found " + DEVICE_NAME + ", connecting");
                stopScan();
                connect(device);
            }
        }

        @Override
        public void onScanFailed(int errorCode) {
            log("Scan failed: " + errorCode);
            finish(false);
        }
    };

    private final BluetoothGattCallback gattCallback =
            new BluetoothGattCallback() {
                @Override
                public void onConnectionStateChange(BluetoothGatt gatt,
                                                    int status,
                                                    int newState) {
                    if (finished) {
                        return;
                    }

                    if (status != BluetoothGatt.GATT_SUCCESS) {
                        log("Connection error: " + status);
                        finish(false);
                        return;
                    }

                    if (newState == BluetoothProfile.STATE_CONNECTED) {
                        log("Connected, discovering services");
                        mainHandler.postDelayed(gatt::discoverServices,
                                250L);
                    } else if (newState ==
                               BluetoothProfile.STATE_DISCONNECTED) {
                        log(writeStarted
                                ? "Disconnected after write request"
                                : "Disconnected before command write");
                        finish(false);
                    }
                }

                @Override
                public void onServicesDiscovered(BluetoothGatt gatt,
                                                 int status) {
                    if (status != BluetoothGatt.GATT_SUCCESS) {
                        log("Service discovery failed: " + status);
                        finish(false);
                        return;
                    }

                    log("Services discovered: " + gatt.getServices().size());

                    BluetoothGattCharacteristic characteristic =
                            findCommandCharacteristic(gatt);
                    if (characteristic == null) {
                        log("Writable command characteristic not found");
                        logDiscoveredServices(gatt);
                        finish(false);
                        return;
                    }

                    log("Exact command characteristic found");
                    writePayload(gatt, characteristic);
                }

                @Override
                public void onCharacteristicWrite(
                        BluetoothGatt gatt,
                        BluetoothGattCharacteristic characteristic,
                        int status) {
                    if (status == BluetoothGatt.GATT_SUCCESS) {
                        log(operation == Operation.START_GAME
                                ? "Game command acknowledged by Android"
                                : "Time write acknowledged by Android");
                        mainHandler.postDelayed(() -> finish(true), 500L);
                    } else {
                        log("Write failed: " + status);
                        finish(false);
                    }
                }
            };

    KeychainBleSync(Context context, Listener listener) {
        this.context = context.getApplicationContext();
        this.listener = listener;
    }

    @SuppressLint("MissingPermission")
    void start() {
        startOperation(Operation.TIME_SYNC, false);
    }

    @SuppressLint("MissingPermission")
    void startBackgroundSync() {
        startOperation(Operation.TIME_SYNC, true);
    }

    @SuppressLint("MissingPermission")
    void startGame() {
        startOperation(Operation.START_GAME, false);
    }

    @SuppressLint("MissingPermission")
    private void startOperation(Operation requestedOperation,
                                boolean requestedLowPowerScan) {
        if (running) {
            log("Sync already running");
            return;
        }

        BluetoothManager manager =
                (BluetoothManager) context.getSystemService(
                        Context.BLUETOOTH_SERVICE);
        BluetoothAdapter adapter =
                manager == null ? null : manager.getAdapter();
        if (adapter == null) {
            log("Bluetooth is not available on this phone");
            finish(false);
            return;
        }
        if (!adapter.isEnabled()) {
            log("Turn Bluetooth on first");
            finish(false);
            return;
        }

        scanner = adapter.getBluetoothLeScanner();
        if (scanner == null) {
            log("BLE scanner is not available");
            finish(false);
            return;
        }

        running = true;
        finished = false;
        scanning = true;
        writeStarted = false;
        operation = requestedOperation;
        lowPowerScan = requestedLowPowerScan;
        int currentAttemptId = ++attemptId;
        log((operation == Operation.START_GAME
                ? "Scanning to start Breakout on "
                : "Scanning for ") + DEVICE_NAME);

        ScanFilter nameFilter = new ScanFilter.Builder()
                .setDeviceName(DEVICE_NAME)
                .build();
        ScanSettings settings = new ScanSettings.Builder()
                .setScanMode(lowPowerScan
                        ? ScanSettings.SCAN_MODE_LOW_POWER
                        : ScanSettings.SCAN_MODE_BALANCED)
                .build();
        scanner.startScan(Collections.singletonList(nameFilter),
                          settings,
                          scanCallback);
        long scanTimeoutMs = lowPowerScan
                ? BACKGROUND_SCAN_TIMEOUT_MS
                : (operation == Operation.START_GAME
                        ? GAME_SCAN_TIMEOUT_MS
                        : TIME_SCAN_TIMEOUT_MS);
        long operationTimeoutMs = lowPowerScan
                ? BACKGROUND_OPERATION_TIMEOUT_MS
                : (operation == Operation.START_GAME
                        ? GAME_OPERATION_TIMEOUT_MS
                        : TIME_OPERATION_TIMEOUT_MS);
        mainHandler.postDelayed(() -> {
            if (attemptId == currentAttemptId &&
                scanning && !finished) {
                log("Scan timeout");
                finish(false);
            }
        }, scanTimeoutMs);
        mainHandler.postDelayed(() -> {
            if (attemptId == currentAttemptId && !finished) {
                log("BLE operation timeout");
                finish(false);
            }
        }, operationTimeoutMs);
    }

    void cancel() {
        finish(false);
    }

    boolean isRunning() {
        return running;
    }

    @SuppressLint("MissingPermission")
    private void connect(BluetoothDevice device) {
        gatt = device.connectGatt(context,
                                  false,
                                  gattCallback,
                                  BluetoothDevice.TRANSPORT_LE);
    }

    private BluetoothGattCharacteristic findCommandCharacteristic(
            BluetoothGatt gatt) {
        BluetoothGattService exactService =
                gatt.getService(TIME_SERVICE_UUID);
        if (exactService != null) {
            BluetoothGattCharacteristic exactCharacteristic =
                    exactService.getCharacteristic(
                            TIME_CHARACTERISTIC_UUID);
            if (exactCharacteristic != null) {
                return exactCharacteristic;
            }
        }

        List<BluetoothGattService> services = gatt.getServices();
        for (BluetoothGattService service : services) {
            for (BluetoothGattCharacteristic characteristic :
                    service.getCharacteristics()) {
                if (TIME_CHARACTERISTIC_UUID.equals(
                        characteristic.getUuid())) {
                    log("Command characteristic found under cached service UUID "
                            + service.getUuid());
                    return characteristic;
                }
            }
        }

        for (BluetoothGattService service : services) {
            if (isStandardGattService(service.getUuid())) {
                continue;
            }
            for (BluetoothGattCharacteristic characteristic :
                    service.getCharacteristics()) {
                int properties = characteristic.getProperties();
                if ((properties &
                        (BluetoothGattCharacteristic.PROPERTY_WRITE |
                         BluetoothGattCharacteristic
                                 .PROPERTY_WRITE_NO_RESPONSE)) != 0) {
                    log("Using writable characteristic fallback "
                            + characteristic.getUuid());
                    return characteristic;
                }
            }
        }
        return null;
    }

    private boolean isStandardGattService(UUID uuid) {
        String value = uuid.toString();
        return value.startsWith("00001800-") ||
               value.startsWith("00001801-");
    }

    @SuppressLint("MissingPermission")
    private void writePayload(BluetoothGatt gatt,
                              BluetoothGattCharacteristic characteristic) {
        String text;
        if (operation == Operation.START_GAME) {
            text = "GAME:START";
            log("Writing Breakout start command");
        } else {
            ZonedDateTime phoneTime = ZonedDateTime.now();
            LocalDateTime localPhoneTime = phoneTime.toLocalDateTime();
            text = localPhoneTime.format(
                    DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss"));
            log("Writing local phone time " + text +
                    " zone=" + phoneTime.getZone() +
                    " offset=" + phoneTime.getOffset());
        }
        byte[] value = text.getBytes(StandardCharsets.UTF_8);

        log("Characteristic " + characteristic.getUuid());

        characteristic.setWriteType(
                BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
        boolean started;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            int result = gatt.writeCharacteristic(
                    characteristic,
                    value,
                    BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
            started = result == BluetoothGatt.GATT_SUCCESS;
        } else {
            characteristic.setValue(value);
            started = gatt.writeCharacteristic(characteristic);
        }

        if (!started) {
            log("Write could not start");
            finish(false);
            return;
        }

        writeStarted = true;
        log("Write request started");
    }

    @SuppressLint("MissingPermission")
    private void finish(boolean success) {
        if (finished) {
            return;
        }

        finished = true;
        running = false;
        stopScan();
        closeGatt();
        listener.onFinished(success);
    }

    @SuppressLint("MissingPermission")
    private void stopScan() {
        if (scanner != null && scanning) {
            scanner.stopScan(scanCallback);
        }
        scanning = false;
    }

    @SuppressLint("MissingPermission")
    private void closeGatt() {
        if (gatt != null) {
            gatt.disconnect();
            gatt.close();
            gatt = null;
        }
    }

    private String scanName(ScanResult result) {
        if (result.getScanRecord() != null &&
            result.getScanRecord().getDeviceName() != null) {
            return result.getScanRecord().getDeviceName();
        }

        return result.getDevice().getName();
    }

    private void logDiscoveredServices(BluetoothGatt gatt) {
        for (BluetoothGattService service : gatt.getServices()) {
            log("Service: " + service.getUuid());
            for (BluetoothGattCharacteristic characteristic :
                    service.getCharacteristics()) {
                log("  Characteristic: " + characteristic.getUuid());
            }
        }
    }

    private void log(String message) {
        listener.onLog(message);
    }
}
