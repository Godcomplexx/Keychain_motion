package com.smartmotion.keychaintimesync;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.Collections;
import java.util.HashSet;
import java.util.Set;

final class SyncPreferences {
    private static final String FILE_NAME = "keychain_sync";
    private static final String AUTO_SYNC_ENABLED = "auto_sync_enabled";
    private static final String BATTERY_PERCENT = "battery_percent";
    private static final String BATTERY_READ_AT = "battery_read_at";
    private static final String NOTIFICATION_RELAY_ENABLED =
            "notification_relay_enabled";
    /* Package names allowed to reach the keychain's screen. Off (empty) by
     * default: a relay that silently forwards everything, including 2FA
     * codes and private messages, is not a default anyone asked for. */
    private static final String NOTIFICATION_RELAY_APPS =
            "notification_relay_apps";

    private SyncPreferences() {
    }

    static boolean isNotificationRelayEnabled(Context context) {
        return preferences(context)
                .getBoolean(NOTIFICATION_RELAY_ENABLED, false);
    }

    static void setNotificationRelayEnabled(Context context,
                                            boolean enabled) {
        preferences(context).edit()
                .putBoolean(NOTIFICATION_RELAY_ENABLED, enabled)
                .apply();
    }

    /** Package names to forward notifications from. Never the live instance
     * SharedPreferences hands back - callers get their own copy. */
    static Set<String> notificationRelayApps(Context context) {
        Set<String> stored = preferences(context)
                .getStringSet(NOTIFICATION_RELAY_APPS,
                              Collections.emptySet());
        return new HashSet<>(stored);
    }

    static void setNotificationRelayApps(Context context,
                                         Set<String> packageNames) {
        preferences(context).edit()
                .putStringSet(NOTIFICATION_RELAY_APPS,
                              new HashSet<>(packageNames))
                .apply();
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
