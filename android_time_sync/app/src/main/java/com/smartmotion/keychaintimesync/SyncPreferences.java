package com.smartmotion.keychaintimesync;

import android.content.Context;
import android.content.SharedPreferences;

final class SyncPreferences {
    private static final String FILE_NAME = "keychain_sync";
    private static final String AUTO_SYNC_ENABLED = "auto_sync_enabled";
    private static final String BATTERY_PERCENT = "battery_percent";
    private static final String BATTERY_READ_AT = "battery_read_at";

    private SyncPreferences() {
    }

    static boolean isAutoSyncEnabled(Context context) {
        return preferences(context).getBoolean(AUTO_SYNC_ENABLED, false);
    }

    static void setAutoSyncEnabled(Context context, boolean enabled) {
        preferences(context).edit()
                .putBoolean(AUTO_SYNC_ENABLED, enabled)
                .apply();
    }

    /*
     * The charge is read whenever the app happens to be talking to the
     * keychain, which may have been hours ago. Storing when it was read lets
     * the panel say so, instead of presenting a stale number as current.
     */
    static void setBatteryLevel(Context context, int percent) {
        preferences(context).edit()
                .putInt(BATTERY_PERCENT, percent)
                .putLong(BATTERY_READ_AT, System.currentTimeMillis())
                .apply();
    }

    static int batteryLevel(Context context) {
        return preferences(context).getInt(BATTERY_PERCENT, -1);
    }

    static long batteryReadAt(Context context) {
        return preferences(context).getLong(BATTERY_READ_AT, 0L);
    }

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences(
                FILE_NAME, Context.MODE_PRIVATE);
    }
}
