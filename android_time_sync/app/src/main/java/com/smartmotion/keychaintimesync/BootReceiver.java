package com.smartmotion.keychaintimesync;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.util.Log;

public class BootReceiver extends BroadcastReceiver {
    private static final String TAG = "KeychainBootReceiver";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null ||
            !Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction()) ||
            !SyncPreferences.isAutoSyncEnabled(context)) {
            return;
        }

        Intent serviceIntent =
                new Intent(context, KeychainSyncService.class);
        serviceIntent.setAction(KeychainSyncService.ACTION_START);
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(serviceIntent);
            } else {
                context.startService(serviceIntent);
            }
        } catch (RuntimeException error) {
            Log.e(TAG, "Could not restore background sync", error);
        }
    }
}
