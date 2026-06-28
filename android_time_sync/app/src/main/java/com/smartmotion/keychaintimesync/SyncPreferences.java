package com.smartmotion.keychaintimesync;

import android.content.Context;
import android.content.SharedPreferences;

final class SyncPreferences {
    private static final String FILE_NAME = "keychain_sync";
    private static final String AUTO_SYNC_ENABLED = "auto_sync_enabled";

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

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences(
                FILE_NAME, Context.MODE_PRIVATE);
    }
}
