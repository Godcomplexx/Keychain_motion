package com.smartmotion.keychaintimesync;

import android.app.Notification;
import android.content.Intent;
import android.os.Build;
import android.service.notification.NotificationListenerService;
import android.service.notification.StatusBarNotification;
import android.util.Log;

import java.util.Set;

/**
 * Mirrors selected phone notifications onto the keychain's screen.
 *
 * Android grants this class notification access as a whole - there is no
 * per-notification permission dialog - so the allowlist in SyncPreferences is
 * the only thing standing between "off" and every app on the phone reaching
 * the keychain, including banking codes and private messages. Off by default,
 * and nothing is forwarded unless both the master switch and that
 * notification's package are explicitly turned on in NotificationAppsActivity.
 */
public class NotificationRelayService extends NotificationListenerService {
    private static final String TAG = "NotificationRelay";

    @Override
    public void onNotificationPosted(StatusBarNotification sbn) {
        if (sbn == null) {
            return;
        }

        final String packageName = sbn.getPackageName();
        /* Never react to this app's own foreground-service notification -
         * relaying "Keychain auto sync: Watching..." back to the keychain
         * would be a pointless loop even if someone allowlisted it. */
        if (getPackageName().equals(packageName)) {
            return;
        }

        if (!SyncPreferences.isNotificationRelayEnabled(this)) {
            return;
        }

        final Set<String> allowedApps =
                SyncPreferences.notificationRelayApps(this);
        if (!allowedApps.contains(packageName)) {
            return;
        }

        final Notification notification = sbn.getNotification();
        if (notification == null) {
            return;
        }
        /*
         * A group summary ("3 new messages") arrives alongside the individual
         * notifications it summarises. Forwarding both would show the same
         * thing on the keychain twice in a row for one real event.
         */
        if ((notification.flags & Notification.FLAG_GROUP_SUMMARY) != 0) {
            return;
        }

        final CharSequence titleSeq =
                notification.extras.getCharSequence(Notification.EXTRA_TITLE);
        final CharSequence textSeq =
                notification.extras.getCharSequence(Notification.EXTRA_TEXT);
        final String title = titleSeq == null ? "" : titleSeq.toString().trim();
        final String text = textSeq == null ? "" : textSeq.toString().trim();

        final String combined;
        if (!title.isEmpty() && !text.isEmpty()) {
            combined = title + ": " + text;
        } else if (!title.isEmpty()) {
            combined = title;
        } else {
            combined = text;
        }
        if (combined.isEmpty()) {
            return;
        }

        final String note = combined.length() > KeychainBleSync.MESSAGE_MAX_LENGTH
                ? combined.substring(0, KeychainBleSync.MESSAGE_MAX_LENGTH)
                : combined;

        Log.i(TAG, "Forwarding notification from " + packageName);
        forward(note);
    }

    private void forward(String note) {
        if (!SyncPreferences.isAutoSyncEnabled(this)) {
            /*
             * ACTION_SEND_MESSAGE only does anything while the background
             * watcher is running - there is no other channel this service can
             * reach the keychain through on its own. NotificationAppsActivity
             * warns about this dependency rather than the note silently going
             * nowhere without explanation.
             */
            Log.i(TAG, "Auto sync is off, dropping notification");
            return;
        }

        Intent intent = new Intent(this, KeychainSyncService.class);
        intent.setAction(KeychainSyncService.ACTION_SEND_MESSAGE);
        intent.putExtra(KeychainSyncService.EXTRA_MESSAGE, note);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(intent);
        } else {
            startService(intent);
        }
    }
}
