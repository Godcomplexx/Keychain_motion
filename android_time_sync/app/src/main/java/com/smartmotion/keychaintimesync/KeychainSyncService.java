package com.smartmotion.keychaintimesync;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;

public class KeychainSyncService extends Service {
    static final String ACTION_START =
            "com.smartmotion.keychaintimesync.START";
    static final String ACTION_STOP =
            "com.smartmotion.keychaintimesync.STOP";
    static final String ACTION_START_GAME =
            "com.smartmotion.keychaintimesync.START_GAME";
    static final String ACTION_SHOW_TIME =
            "com.smartmotion.keychaintimesync.SHOW_TIME";
    /*
     * The keychain accepts one connection at a time. While the drawing pad
     * holds it, background scanning would only fight over the radio, so the
     * service steps aside without forgetting that auto sync is switched on.
     */
    static final String ACTION_PAUSE =
            "com.smartmotion.keychaintimesync.PAUSE";
    static final String ACTION_RESUME =
            "com.smartmotion.keychaintimesync.RESUME";

    private static final String TAG = "KeychainSyncService";
    private static final String CHANNEL_ID = "keychain_sync";
    private static final int NOTIFICATION_ID = 1001;
    private static final long RETRY_AFTER_MISS_MS = 5000L;
    private static final long RETRY_AFTER_SUCCESS_MS = 30000L;
    private static final long GAME_RETRY_MS = 250L;
    private static final long GAME_REQUEST_WINDOW_MS = 60000L;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private KeychainBleSync bleSync;
    private boolean autoSyncRunning;
    /*
     * A request the person made in the app, waiting for the keychain's next
     * BLE window. One field rather than a flag per command: they are mutually
     * exclusive, and a second flag would only make it possible for two to be
     * pending at once.
     */
    private enum PendingRequest { NONE, START_GAME, SHOW_TIME }

    private boolean paused;
    private PendingRequest pendingRequest = PendingRequest.NONE;
    private boolean requestAttemptActive;
    private long requestDeadlineMs;

    private final Runnable startNextScan = new Runnable() {
        @Override
        public void run() {
            if (!autoSyncRunning || paused) {
                return;
            }

            if (bleSync != null && !bleSync.isRunning()) {
                if (pendingRequest != PendingRequest.NONE &&
                    SystemClock.elapsedRealtime() < requestDeadlineMs) {
                    requestAttemptActive = true;
                    if (pendingRequest == PendingRequest.START_GAME) {
                        bleSync.startGame();
                    } else {
                        bleSync.showTime();
                    }
                } else {
                    pendingRequest = PendingRequest.NONE;
                    requestAttemptActive = false;
                    bleSync.startBackgroundSync();
                }
            }
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
        bleSync = new KeychainBleSync(this, new KeychainBleSync.Listener() {
            @Override
            public void onLog(String message) {
                Log.i(TAG, message);
                updateNotification(message);
            }

            @Override
            public void onFinished(boolean success) {
                boolean completedRequest = requestAttemptActive;
                PendingRequest attempted = pendingRequest;
                requestAttemptActive = false;
                Log.i(TAG, success ? "Auto sync finished" :
                        "Auto sync did not find/write time");
                handler.post(() -> {
                    if (!autoSyncRunning || paused) {
                        return;
                    }
                    if (pendingRequest != PendingRequest.NONE) {
                        String what = attempted == PendingRequest.SHOW_TIME
                                ? "clock" : "Breakout";
                        if (completedRequest && success) {
                            pendingRequest = PendingRequest.NONE;
                            updateNotification(what + " sent");
                            scheduleNextScan(RETRY_AFTER_SUCCESS_MS);
                        } else if (SystemClock.elapsedRealtime() >=
                                   requestDeadlineMs) {
                            pendingRequest = PendingRequest.NONE;
                            updateNotification(what + " request expired");
                            scheduleNextScan(RETRY_AFTER_MISS_MS);
                        } else {
                            updateNotification("Waiting to send " + what);
                            scheduleNextScan(GAME_RETRY_MS);
                        }
                        return;
                    }
                    updateNotification(success
                            ? "Time synchronized"
                            : "Watching for KeychainSync");
                    scheduleNextScan(success
                            ? RETRY_AFTER_SUCCESS_MS
                            : RETRY_AFTER_MISS_MS);
                });
            }
        });
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent == null ? ACTION_START : intent.getAction();
        if (ACTION_STOP.equals(action)) {
            SyncPreferences.setAutoSyncEnabled(this, false);
            stopAutoSync();
            return START_NOT_STICKY;
        }

        if (ACTION_PAUSE.equals(action)) {
            paused = true;
            handler.removeCallbacks(startNextScan);
            if (bleSync != null) {
                bleSync.cancel();
            }
            updateNotification("Paused while the drawing pad is open");
            return START_STICKY;
        }

        if (ACTION_RESUME.equals(action)) {
            paused = false;
            if (autoSyncRunning) {
                scheduleNextScan(RETRY_AFTER_MISS_MS);
                updateNotification("Watching for KeychainSync");
            }
            return START_STICKY;
        }

        if (ACTION_SHOW_TIME.equals(action)) {
            if (!SyncPreferences.isAutoSyncEnabled(this)) {
                stopSelf();
                return START_NOT_STICKY;
            }
            startAutoSync();
            queueShowTimeRequest();
            return START_STICKY;
        }

        if (ACTION_START_GAME.equals(action)) {
            if (!SyncPreferences.isAutoSyncEnabled(this)) {
                stopSelf();
                return START_NOT_STICKY;
            }
            startAutoSync();
            queueGameRequest();
            return START_STICKY;
        }

        if (intent == null &&
            !SyncPreferences.isAutoSyncEnabled(this)) {
            stopSelf();
            return START_NOT_STICKY;
        }

        SyncPreferences.setAutoSyncEnabled(this, true);
        startAutoSync();
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        cleanupAutoSync();
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void startAutoSync() {
        if (autoSyncRunning) {
            return;
        }

        autoSyncRunning = true;
        startForeground(NOTIFICATION_ID, buildNotification());
        handler.post(startNextScan);
    }

    private void stopAutoSync() {
        cleanupAutoSync();
        stopSelf();
    }

    private void cleanupAutoSync() {
        autoSyncRunning = false;
        paused = false;
        pendingRequest = PendingRequest.NONE;
        requestAttemptActive = false;
        handler.removeCallbacks(startNextScan);
        if (bleSync != null) {
            bleSync.cancel();
        }
        stopForeground(STOP_FOREGROUND_REMOVE);
    }

    private void queueShowTimeRequest() {
        queueRequest(PendingRequest.SHOW_TIME, "Waiting to send clock");
    }

    private void queueGameRequest() {
        queueRequest(PendingRequest.START_GAME, "Waiting to start Breakout");
    }

    private void queueRequest(PendingRequest request, String notice) {
        paused = false;
        pendingRequest = request;
        requestDeadlineMs =
                SystemClock.elapsedRealtime() + GAME_REQUEST_WINDOW_MS;
        handler.removeCallbacks(startNextScan);
        updateNotification(notice);
        if (bleSync != null && bleSync.isRunning()) {
            bleSync.cancel();
        } else {
            handler.post(startNextScan);
        }
    }

    private void scheduleNextScan(long delayMs) {
        handler.removeCallbacks(startNextScan);
        handler.postDelayed(startNextScan, delayMs);
    }

    private Notification buildNotification() {
        return buildNotification("Watching for KeychainSync");
    }

    private void updateNotification(String text) {
        if (!autoSyncRunning) {
            return;
        }

        Notification notification = buildNotification(text);
        NotificationManager manager =
                getSystemService(NotificationManager.class);
        if (manager != null) {
            manager.notify(NOTIFICATION_ID, notification);
        }
    }

    private Notification buildNotification(String text) {
        Intent openIntent = new Intent(this, MainActivity.class);
        PendingIntent contentIntent = PendingIntent.getActivity(
                this,
                0,
                openIntent,
                PendingIntent.FLAG_UPDATE_CURRENT |
                        PendingIntent.FLAG_IMMUTABLE);

        Notification.Builder builder = Build.VERSION.SDK_INT >=
                Build.VERSION_CODES.O
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);

        return builder
                .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
                .setContentTitle("Keychain auto sync")
                .setContentText(text)
                .setContentIntent(contentIntent)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .build();
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            return;
        }

        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                "Keychain sync",
                NotificationManager.IMPORTANCE_LOW);
        NotificationManager manager =
                getSystemService(NotificationManager.class);
        if (manager != null) {
            manager.createNotificationChannel(channel);
        }
    }
}
