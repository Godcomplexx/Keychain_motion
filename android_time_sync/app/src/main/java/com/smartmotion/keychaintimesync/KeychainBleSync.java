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
import android.bluetooth.le.ScanResult;
import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;

import java.nio.charset.StandardCharsets;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.UUID;

final class KeychainBleSync {
    interface Listener {
        void onLog(String message);

        void onFinished(boolean success);
    }

    static final String DEVICE_NAME = "KeychainSync";

    private static final long SCAN_TIMEOUT_MS = 15000L;
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
                        gatt.discoverServices();
                    } else if (newState ==
                               BluetoothProfile.STATE_DISCONNECTED) {
                        log("Disconnected before time write");
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

                    BluetoothGattService service =
                            gatt.getService(TIME_SERVICE_UUID);
                    if (service == null) {
                        log("Exact time service not found");
                        logDiscoveredServices(gatt);
                        finish(false);
                        return;
                    }

                    BluetoothGattCharacteristic characteristic =
                            service.getCharacteristic(
                                    TIME_CHARACTERISTIC_UUID);
                    if (characteristic == null) {
                        log("Exact time characteristic not found");
                        logDiscoveredServices(gatt);
                        finish(false);
                        return;
                    }

                    log("Exact time characteristic found");
                    writeCurrentTime(gatt, characteristic);
                }

                @Override
                public void onCharacteristicWrite(
                        BluetoothGatt gatt,
                        BluetoothGattCharacteristic characteristic,
                        int status) {
                    if (status == BluetoothGatt.GATT_SUCCESS) {
                        log("Time sent successfully");
                        finish(true);
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
        log("Scanning for " + DEVICE_NAME);
        scanner.startScan(scanCallback);
        mainHandler.postDelayed(() -> {
            if (scanning && !finished) {
                log("Scan timeout");
                finish(false);
            }
        }, SCAN_TIMEOUT_MS);
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

    @SuppressLint("MissingPermission")
    private void writeCurrentTime(BluetoothGatt gatt,
                                  BluetoothGattCharacteristic characteristic) {
        String text = LocalDateTime.now()
                .format(DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss"));
        byte[] value = text.getBytes(StandardCharsets.UTF_8);

        log("Writing " + text);
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
        }
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
