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
import android.os.SystemClock;

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

        /**
         * The keychain is in drawing mode and packets will be delivered.
         * Called again after a reconnection, with {@code resumed} true, so the
         * caller can put the picture back rather than start from blank.
         */
        void onReady(boolean resumed);

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
    /*
     * The link drops for reasons that have nothing to do with the person
     * drawing - the background sync letting go of the radio, the keychain
     * ending a game, a stray supervision timeout. While the pad is on screen,
     * every one of those should be a pause rather than the end.
     */
    private static final long RETRY_BASE_MS = 500L;
    private static final long RETRY_MAX_MS = 4000L;
    /* How long the stack is given to finish tearing down someone else's link
     * before this one starts scanning. */
    private static final long RADIO_SETTLE_MS = 400L;
    private static final long RADIO_WAIT_TIMEOUT_MS = 5000L;
    private static final int REQUESTED_MTU = 247;
    /* Until the phone and keychain agree on a larger MTU, a write may carry
     * only 20 bytes of payload. */
    private static final int FALLBACK_PAYLOAD = 20;
    /* Roughly half a second of drawing at the flush rate. */
    private static final int LIVE_QUEUE_LIMIT = 24;

    private final Context context;
    private final Listener listener;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Deque<byte[]> pending = new ArrayDeque<>();

    private BluetoothLeScanner scanner;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic commandCharacteristic;
    private BluetoothGattCharacteristic drawCharacteristic;
    private boolean scanning;
    /** Set once the pad is dismissed for good; retries stop honouring it. */
    private boolean stopped;
    private boolean ready;
    private boolean everReady;
    private boolean writeInFlight;
    /*
     * Tearing an attempt down disconnects, and the disconnect callback is
     * another reason to retry - so without this the first failure would
     * schedule two attempts, and each of those two more.
     */
    private boolean retryPending;
    private int attempt;
    private int payloadLimit = FALLBACK_PAYLOAD;

    private final Runnable scanTimeout = new Runnable() {
        @Override
        public void run() {
            if (!ready) {
                retry("Keychain not found");
            }
        }
    };

    private final ScanCallback scanCallback = new ScanCallback() {
        @Override
        public void onScanResult(int callbackType, ScanResult result) {
            String scanned = result.getDevice() == null
                    ? null : result.getDevice().getName();
            if (scanned == null && result.getScanRecord() != null) {
                scanned = result.getScanRecord().getDeviceName();
            }
            if (!KeychainBleSync.DEVICE_NAME.equals(scanned)) {
                return;
            }

            final BluetoothDevice device = result.getDevice();
            handler.post(() -> {
                /* Several results can arrive before the scan actually stops,
                 * so the second one must not open a second connection. */
                if (stopped || gatt != null) {
                    return;
                }
                log("Found the keychain, connecting");
                stopScan();
                gatt = device.connectGatt(context, false, gattCallback,
                                          BluetoothDevice.TRANSPORT_LE);
            });
        }

        @Override
        public void onScanFailed(int errorCode) {
            handler.post(() -> retry("Scan failed: " + errorCode));
        }
    };

    private final BluetoothGattCallback gattCallback =
            new BluetoothGattCallback() {
                @Override
                public void onConnectionStateChange(BluetoothGatt source,
                                                    int status,
                                                    int newState) {
                    handler.post(() -> {
                        if (stopped || source != gatt) {
                            return;
                        }
                        if (newState == BluetoothProfile.STATE_CONNECTED) {
                            log("Connected, negotiating packet size");
                            if (!source.requestMtu(REQUESTED_MTU)) {
                                source.discoverServices();
                            }
                        } else if (newState ==
                                   BluetoothProfile.STATE_DISCONNECTED) {
                            retry("Keychain disconnected");
                        }
                    });
                }

                @Override
                public void onMtuChanged(BluetoothGatt source, int mtu,
                                         int status) {
                    handler.post(() -> {
                        if (stopped || source != gatt) {
                            return;
                        }
                        if (status == BluetoothGatt.GATT_SUCCESS) {
                            /* Three of the MTU are the ATT write header. */
                            payloadLimit = Math.max(FALLBACK_PAYLOAD, mtu - 3);
                            log("Packet size " + payloadLimit + " bytes");
                        }
                        source.discoverServices();
                    });
                }

                @Override
                public void onServicesDiscovered(BluetoothGatt source,
                                                 int status) {
                    handler.post(() -> onServicesReady(source, status));
                }

                @Override
                public void onCharacteristicWrite(
                        BluetoothGatt source,
                        BluetoothGattCharacteristic characteristic,
                        int status) {
                    final boolean isCommand =
                            COMMAND_UUID.equals(characteristic.getUuid());
                    handler.post(() -> onWriteComplete(source, isCommand,
                                                       status));
                }
            };

    /*
     * Everything below runs on the main thread.
     *
     * The GATT callbacks arrive on a binder thread while the activity queues
     * packets from the main one, and they share the pending deque, the
     * in-flight flag and the connection itself. ArrayDeque is not thread safe,
     * and a stale read of writeInFlight would stall the queue for good - the
     * drawing would simply stop with no error anywhere. Marshalling every
     * callback onto one thread removes the whole class of problem instead of
     * sprinkling synchronisation over it.
     */

    private void onServicesReady(BluetoothGatt source, int status) {
        if (stopped || source != gatt) {
            return;
        }
        if (status != BluetoothGatt.GATT_SUCCESS) {
            retry("Service discovery failed: " + status);
            return;
        }

        BluetoothGattService service = gatt.getService(SERVICE_UUID);
        if (service == null) {
            retry("Keychain service not found");
            return;
        }

        commandCharacteristic = service.getCharacteristic(COMMAND_UUID);
        drawCharacteristic = service.getCharacteristic(DRAW_UUID);
        if (commandCharacteristic == null || drawCharacteristic == null) {
            /* Not a transient fault: retrying would never help. */
            close("This keychain firmware has no drawing pad");
            return;
        }

        drawCharacteristic.setWriteType(
                BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE);
        writeCommand("DRAW:START");
    }

    private void onWriteComplete(BluetoothGatt source, boolean isCommand,
                                 int status) {
        if (stopped || source != gatt) {
            return;
        }

        if (isCommand) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                retry("Could not open the drawing pad: " + status);
                return;
            }
            if (!ready) {
                ready = true;
                attempt = 0;
                handler.removeCallbacks(scanTimeout);
                final boolean resumed = everReady;
                everReady = true;
                log(resumed ? "Reconnected" : "Drawing pad open");
                listener.onReady(resumed);
            }
            return;
        }

        writeInFlight = false;
        drainQueue();
    }

    KeychainDrawSession(Context context, Listener listener) {
        this.context = context.getApplicationContext();
        this.listener = listener;
    }

    void start() {
        if (stopped) {
            return;
        }
        waitForRadio(SystemClock.elapsedRealtime() + RADIO_WAIT_TIMEOUT_MS);
    }

    // aislop-ignore-next-line ai-slop/narrative-comment -- records a race found on hardware; without it the next reader deletes waitForRadio as pointless
    /**
     * Holds off until the background sync has let go of the radio.
     *
     * The keychain accepts one connection, and the service's teardown finishes
     * after its close() call returns. Connecting inside that window produced a
     * link that came up and immediately dropped again.
     */
    private void waitForRadio(long deadlineMs) {
        if (stopped) {
            return;
        }
        if (KeychainSyncService.isRadioSettled(RADIO_SETTLE_MS) ||
            SystemClock.elapsedRealtime() >= deadlineMs) {
            beginScan();
            return;
        }
        log("Waiting for background sync to release the radio");
        handler.postDelayed(() -> waitForRadio(deadlineMs), 100L);
    }

    private void beginScan() {
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
        log(everReady ? "Reconnecting" : "Looking for the keychain");
        scanner.startScan(null, settings, scanCallback);
        handler.postDelayed(scanTimeout, SCAN_TIMEOUT_MS);
    }

    /**
     * A setback rather than the end: drop this attempt and start another.
     * Backs off so a keychain that is off, or busy playing Breakout with its
     * radio down, is not scanned for continuously.
     */
    private void retry(String reason) {
        if (stopped || retryPending) {
            return;
        }

        retryPending = true;
        releaseConnection();
        final long delay = Math.min(RETRY_MAX_MS,
                                    RETRY_BASE_MS * (1L << Math.min(attempt, 3)));
        ++attempt;
        log(reason + " - retrying");
        handler.postDelayed(() -> {
            retryPending = false;
            start();
        }, delay);
    }

    boolean isReady() {
        return ready && !stopped;
    }

    /** Largest number of points one packet can carry, after the opcode. */
    int pointsPerPacket() {
        return (payloadLimit - 1) / 2;
    }

    void sendPoints(boolean startsStroke, int[] coordinates, int count) {
        sendPoints(startsStroke, coordinates, count, true);
    }

    /**
     * @param droppable whether these points may be thrown away when the queue
     *     backs up. True while a finger is drawing - losing the middle of a
     *     fast stroke costs less than falling seconds behind it. False when
     *     replaying a saved picture, where a dropped packet would leave a hole
     *     in a drawing nobody is watching being made.
     */
    void sendPoints(boolean startsStroke, int[] coordinates, int count,
                    boolean droppable) {
        if (!isReady() || count <= 0) {
            return;
        }

        byte[] packet = new byte[1 + count * 2];
        packet[0] = startsStroke ? OP_BEGIN : OP_CONTINUE;
        for (int index = 0; index < count * 2; ++index) {
            packet[1 + index] = (byte) coordinates[index];
        }
        enqueue(packet, droppable);
    }

    /* Never droppable: these change what later points mean. A lost clear
     * leaves the old picture underneath, a lost pen size draws the rest of the
     * drawing at the wrong width. */
    void sendClear() {
        enqueue(new byte[] { OP_CLEAR }, false);
    }

    void sendPenRadius(int radius) {
        enqueue(new byte[] { OP_PEN, (byte) radius }, false);
    }

    void stop() {
        if (stopped) {
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

    private void enqueue(byte[] packet, boolean droppable) {
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
        if (droppable && pending.size() >= LIVE_QUEUE_LIMIT) {
            pending.pollFirst();
        }
        pending.addLast(packet);
        drainQueue();
    }

    private void drainQueue() {
        if (writeInFlight || pending.isEmpty() || gatt == null ||
            drawCharacteristic == null || stopped) {
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

    /** For good. Nothing here is retried afterwards. */
    private void close(String reason) {
        if (stopped) {
            return;
        }
        stopped = true;
        releaseConnection();
        listener.onClosed(reason != null ? reason : "Drawing pad closed");
    }

    /** Tears down one attempt, leaving the session able to make another. */
    private void releaseConnection() {
        ready = false;
        writeInFlight = false;
        payloadLimit = FALLBACK_PAYLOAD;
        commandCharacteristic = null;
        drawCharacteristic = null;
        handler.removeCallbacks(scanTimeout);
        stopScan();
        pending.clear();

        if (gatt != null) {
            gatt.disconnect();
            gatt.close();
            gatt = null;
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
