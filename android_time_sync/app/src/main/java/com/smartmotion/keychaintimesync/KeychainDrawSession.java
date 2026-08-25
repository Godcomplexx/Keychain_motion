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
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;

import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.Deque;
import java.util.UUID;

/**
 * A connection that stays open while the drawing pad is on screen.
 *
 * {@link KeychainBleSync} is the wrong shape for this: it connects, writes one
 * payload and hangs up, which is right for a command and wrong for a finger
 * that keeps moving. Reconnecting per stroke would cost a second each time.
 */
@SuppressLint("MissingPermission")
final class KeychainDrawSession {
    interface Listener {
        void onLog(String message);

        /** The keychain is in drawing mode and packets will be delivered. */
        void onReady();

        void onClosed(String reason);
    }

    /* Opcodes shared with components/draw_pad/include/draw_pad.h. */
    static final byte OP_CONTINUE = 0x10;
    static final byte OP_BEGIN = 0x11;
    static final byte OP_CLEAR = 0x20;
    static final byte OP_PEN = 0x21;

    private static final UUID SERVICE_UUID =
            UUID.fromString("11223344-5566-7788-9a49-315b10371342");
    private static final UUID COMMAND_UUID =
            UUID.fromString("11223344-5566-7788-9a49-315b10371343");
    private static final UUID DRAW_UUID =
            UUID.fromString("11223344-5566-7788-9a49-315b10371344");

    private static final long SCAN_TIMEOUT_MS = 20000L;
    private static final int REQUESTED_MTU = 247;
    /* Until the phone and keychain agree on a larger MTU, a write may carry
     * only 20 bytes of payload. */
    private static final int FALLBACK_PAYLOAD = 20;

    private final Context context;
    private final Listener listener;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Deque<byte[]> pending = new ArrayDeque<>();

    private BluetoothLeScanner scanner;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic commandCharacteristic;
    private BluetoothGattCharacteristic drawCharacteristic;
    private boolean scanning;
    private boolean closed;
    private boolean ready;
    private boolean writeInFlight;
    private int payloadLimit = FALLBACK_PAYLOAD;

    private final Runnable scanTimeout = new Runnable() {
        @Override
        public void run() {
            if (!ready) {
                close("Keychain not found");
            }
        }
    };

    private final ScanCallback scanCallback = new ScanCallback() {
        @Override
        public void onScanResult(int callbackType, ScanResult result) {
            if (closed) {
                return;
            }
            String name = result.getDevice() == null
                    ? null : result.getDevice().getName();
            if (name == null && result.getScanRecord() != null) {
                name = result.getScanRecord().getDeviceName();
            }
            if (!KeychainBleSync.DEVICE_NAME.equals(name)) {
                return;
            }
            log("Found the keychain, connecting");
            stopScan();
            gatt = result.getDevice().connectGatt(
                    context, false, gattCallback,
                    BluetoothDevice.TRANSPORT_LE);
        }

        @Override
        public void onScanFailed(int errorCode) {
            close("Scan failed: " + errorCode);
        }
    };

    private final BluetoothGattCallback gattCallback =
            new BluetoothGattCallback() {
                @Override
                public void onConnectionStateChange(BluetoothGatt gatt,
                                                    int status,
                                                    int newState) {
                    if (newState == BluetoothProfile.STATE_CONNECTED) {
                        log("Connected, negotiating packet size");
                        if (!gatt.requestMtu(REQUESTED_MTU)) {
                            gatt.discoverServices();
                        }
                    } else if (newState ==
                               BluetoothProfile.STATE_DISCONNECTED) {
                        close("Keychain disconnected");
                    }
                }

                @Override
                public void onMtuChanged(BluetoothGatt gatt, int mtu,
                                         int status) {
                    if (status == BluetoothGatt.GATT_SUCCESS) {
                        /* Three bytes of the MTU are the ATT write header. */
                        payloadLimit = Math.max(FALLBACK_PAYLOAD, mtu - 3);
                        log("Packet size " + payloadLimit + " bytes");
                    }
                    gatt.discoverServices();
                }

                @Override
                public void onServicesDiscovered(BluetoothGatt gatt,
                                                 int status) {
                    if (status != BluetoothGatt.GATT_SUCCESS) {
                        close("Service discovery failed: " + status);
                        return;
                    }

                    BluetoothGattService service =
                            gatt.getService(SERVICE_UUID);
                    if (service == null) {
                        close("Keychain service not found");
                        return;
                    }

                    commandCharacteristic =
                            service.getCharacteristic(COMMAND_UUID);
                    drawCharacteristic = service.getCharacteristic(DRAW_UUID);
                    if (commandCharacteristic == null ||
                        drawCharacteristic == null) {
                        close("This keychain firmware has no drawing pad");
                        return;
                    }

                    drawCharacteristic.setWriteType(
                            BluetoothGattCharacteristic
                                    .WRITE_TYPE_NO_RESPONSE);
                    writeCommand("DRAW:START");
                }

                @Override
                public void onCharacteristicWrite(
                        BluetoothGatt gatt,
                        BluetoothGattCharacteristic characteristic,
                        int status) {
                    if (COMMAND_UUID.equals(characteristic.getUuid())) {
                        if (status != BluetoothGatt.GATT_SUCCESS) {
                            close("Could not open the drawing pad: " + status);
                            return;
                        }
                        if (!ready) {
                            ready = true;
                            handler.removeCallbacks(scanTimeout);
                            log("Drawing pad open");
                            listener.onReady();
                        }
                        return;
                    }

                    writeInFlight = false;
                    drainQueue();
                }
            };

    KeychainDrawSession(Context context, Listener listener) {
        this.context = context.getApplicationContext();
        this.listener = listener;
    }

    void start() {
        BluetoothManager manager =
                context.getSystemService(BluetoothManager.class);
        BluetoothAdapter adapter =
                manager == null ? null : manager.getAdapter();
        if (adapter == null || !adapter.isEnabled()) {
            close("Bluetooth is off");
            return;
        }

        scanner = adapter.getBluetoothLeScanner();
        if (scanner == null) {
            close("No BLE scanner available");
            return;
        }

        ScanSettings settings = new ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .build();
        scanning = true;
        log("Looking for the keychain");
        scanner.startScan(null, settings, scanCallback);
        handler.postDelayed(scanTimeout, SCAN_TIMEOUT_MS);
    }

    boolean isReady() {
        return ready && !closed;
    }

    /** Largest number of points one packet can carry, after the opcode. */
    int pointsPerPacket() {
        return (payloadLimit - 1) / 2;
    }

    void sendPoints(boolean startsStroke, int[] coordinates, int count) {
        if (!isReady() || count <= 0) {
            return;
        }

        byte[] packet = new byte[1 + count * 2];
        packet[0] = startsStroke ? OP_BEGIN : OP_CONTINUE;
        for (int index = 0; index < count * 2; ++index) {
            packet[1 + index] = (byte) coordinates[index];
        }
        enqueue(packet);
    }

    void sendClear() {
        enqueue(new byte[] { OP_CLEAR });
    }

    void sendPenRadius(int radius) {
        enqueue(new byte[] { OP_PEN, (byte) radius });
    }

    void stop() {
        if (closed) {
            return;
        }
        if (ready && commandCharacteristic != null && gatt != null) {
            /*
             * Best effort. If it does not land, the keychain notices the
             * disconnect below and leaves drawing mode on its own.
             */
            writeCommand("DRAW:STOP");
        }
        close(null);
    }

    private void enqueue(byte[] packet) {
        if (!isReady()) {
            return;
        }
        /*
         * Android accepts one outstanding write at a time even without a
         * response, so packets queue here and go out as the previous one is
         * acknowledged. A finger outruns the radio on a long fast stroke; the
         * queue is capped so the drawing stays behind the finger by a fraction
         * of a second rather than by however long the stroke lasted.
         */
        if (pending.size() >= 24) {
            pending.pollFirst();
        }
        pending.addLast(packet);
        drainQueue();
    }

    private void drainQueue() {
        if (writeInFlight || pending.isEmpty() || gatt == null ||
            drawCharacteristic == null || closed) {
            return;
        }

        byte[] packet = pending.pollFirst();
        writeInFlight = true;
        boolean queued;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            queued = gatt.writeCharacteristic(
                    drawCharacteristic, packet,
                    BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE)
                    == BluetoothGatt.GATT_SUCCESS;
        } else {
            drawCharacteristic.setValue(packet);
            queued = gatt.writeCharacteristic(drawCharacteristic);
        }

        if (!queued) {
            writeInFlight = false;
        }
    }

    private void writeCommand(String text) {
        if (gatt == null || commandCharacteristic == null) {
            return;
        }

        byte[] payload = text.getBytes(StandardCharsets.US_ASCII);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            gatt.writeCharacteristic(
                    commandCharacteristic, payload,
                    BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
        } else {
            commandCharacteristic.setWriteType(
                    BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
            commandCharacteristic.setValue(payload);
            gatt.writeCharacteristic(commandCharacteristic);
        }
    }

    private void close(String reason) {
        if (closed) {
            return;
        }
        closed = true;
        ready = false;
        handler.removeCallbacks(scanTimeout);
        stopScan();
        pending.clear();

        if (gatt != null) {
            gatt.disconnect();
            gatt.close();
            gatt = null;
        }

        if (reason != null) {
            listener.onClosed(reason);
        } else {
            listener.onClosed("Drawing pad closed");
        }
    }

    private void stopScan() {
        if (scanning && scanner != null) {
            scanner.stopScan(scanCallback);
        }
        scanning = false;
    }

    private void log(String message) {
        listener.onLog(message);
    }
}
