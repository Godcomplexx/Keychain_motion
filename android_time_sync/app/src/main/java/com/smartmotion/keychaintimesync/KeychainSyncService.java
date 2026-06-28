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
    private boolean pendingGameRequest;
    private boolean gameAttemptActive;
    private long gameRequestDeadlineMs;

    private final Runnable startNextScan = new Runnable() {
        @Override
        public void run() {
            if (!autoSyncRunning) {
                return;
            }

            if (bleSync != null && !bleSync.isRunning()) {
                if (pendingGameRequest &&
                    SystemClock.elapsedRealtime() <
                            gameRequestDeadlineMs) {
                    gameAttemptActive = true;
                    bleSync.startGame();
                } else {
                    pendingGameRequest = false;
                    gameAttemptActive = false;
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
                boolean completedGameAttempt = gameAttemptActive;
                gameAttemptActive = false;
                Log.i(TAG, success ? "Auto sync finished" :
                        "Auto sync did not find/write time");
                handler.post(() -> {
                    if (!autoSyncRunning) {
                        return;
                    }
                    if (pendingGameRequest) {
                        if (completedGameAttempt && success) {
                            pendingGameRequest = false;
                            updateNotification("Breakout started");
                            scheduleNextScan(RETRY_AFTER_SUCCESS_MS);
                        } else if (SystemClock.elapsedRealtime() >=
                                   gameRequestDeadlineMs) {
                            pendingGameRequest = false;
                            updateNotification("Breakout request expired");
                            scheduleNextScan(RETRY_AFTER_MISS_MS);
                        } else {
                            updateNotification(
                                    "Waiting to start Breakout");
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
        pendingGameRequest = false;
        gameAttemptActive = false;
        handler.removeCallbacks(startNextScan);
        if (bleSync != null) {
            bleSync.cancel();
        }
        stopForeground(STOP_FOREGROUND_REMOVE);
    }

    private void queueGameRequest() {
        pendingGameRequest = true;
        gameRequestDeadlineMs =
                SystemClock.elapsedRealtime() + GAME_REQUEST_WINDOW_MS;
        handler.removeCallbacks(startNextScan);
        updateNotification("Waiting to start Breakout");
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
