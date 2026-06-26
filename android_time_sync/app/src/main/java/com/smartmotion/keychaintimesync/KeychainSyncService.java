package com.smartmotion.keychaintimesync;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;

public class KeychainSyncService extends Service {
    static final String ACTION_START =
            "com.smartmotion.keychaintimesync.START";
    static final String ACTION_STOP =
            "com.smartmotion.keychaintimesync.STOP";

    private static final String TAG = "KeychainSyncService";
    private static final String CHANNEL_ID = "keychain_sync";
    private static final int NOTIFICATION_ID = 1001;
    /*
     * The keychain starts BLE advertising when it opens the TIME screen, so the
     * phone must scan often enough to answer while that screen is still shown
     * (TIME lasts about 60 s). A shorter interval makes TIME show fresh time
     * faster; a longer interval saves phone battery. 15 s is a balance.
     */
    private static final long AUTO_SYNC_INTERVAL_MS = 15000L;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private KeychainBleSync bleSync;
    private boolean autoSyncRunning;

    private final Runnable syncLoop = new Runnable() {
        @Override
        public void run() {
            if (!autoSyncRunning) {
                return;
            }

            if (bleSync != null && !bleSync.isRunning()) {
                bleSync.start();
            }

            handler.postDelayed(this, AUTO_SYNC_INTERVAL_MS);
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
            }

            @Override
            public void onFinished(boolean success) {
                Log.i(TAG, success ? "Auto sync finished" :
                        "Auto sync did not find/write time");
            }
        });
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent == null ? ACTION_START : intent.getAction();
        if (ACTION_STOP.equals(action)) {
            stopAutoSync();
            return START_NOT_STICKY;
        }

        startAutoSync();
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        stopAutoSync();
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
        handler.post(syncLoop);
    }

    private void stopAutoSync() {
        autoSyncRunning = false;
        handler.removeCallbacks(syncLoop);
        if (bleSync != null) {
            bleSync.cancel();
        }
        stopForeground(STOP_FOREGROUND_REMOVE);
        stopSelf();
    }

    private Notification buildNotification() {
        Notification.Builder builder = Build.VERSION.SDK_INT >=
                Build.VERSION_CODES.O
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);

        return builder
                .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
                .setContentTitle("Keychain auto sync")
                .setContentText("Watching for KeychainSync")
                .setOngoing(true)
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
