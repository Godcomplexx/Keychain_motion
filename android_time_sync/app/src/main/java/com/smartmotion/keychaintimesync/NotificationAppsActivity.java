package com.smartmotion.keychaintimesync;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.provider.Settings;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.CompoundButton;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Which apps' notifications reach the keychain, and the master switch for the
 * whole feature.
 *
 * Off by default and per-app: Android's notification access is all-or-nothing
 * at the OS level (one permission grants this service every notification on
 * the phone), so the allowlist here is the only thing standing between "off"
 * and everything - 2FA codes, private messages - reaching a screen anyone
 * nearby can glance at.
 */
public final class NotificationAppsActivity extends Activity {
    private LinearLayout appList;
    private TextView warningText;

    static Intent intent(Context context) {
        return new Intent(context, NotificationAppsActivity.class);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(createLayout());
    }

    @Override
    protected void onResume() {
        super.onResume();
        /* System notification access, and the auto-sync dependency, are both
         * changed outside this screen - re-check every time it's shown. */
        updateWarning();
    }

    private View createLayout() {
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setBackgroundColor(RetroUi.BACKDROP);
        page.setClipToPadding(false);
        RetroUi.insetForSystemBars(page, RetroUi.dp(this, 14));

        LinearLayout panel = RetroUi.panel(this);

        panel.addView(RetroUi.titleBar(this, "Notifications",
                RetroUi.closeButton(this, this::finish)));
        panel.addView(RetroUi.rule(this));

        panel.addView(RetroUi.sectionLabel(this, "Forward to keychain"));
        LinearLayout masterRow = new LinearLayout(this);
        masterRow.setOrientation(LinearLayout.HORIZONTAL);
        masterRow.setGravity(Gravity.CENTER_VERTICAL);
        TextView masterLabel = RetroUi.body(this, "Enabled", 14, RetroUi.INK);
        masterRow.addView(masterLabel, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f));
        Switch masterSwitch = new Switch(this);
        masterSwitch.setChecked(
                SyncPreferences.isNotificationRelayEnabled(this));
        masterSwitch.setOnCheckedChangeListener((button, checked) ->
                SyncPreferences.setNotificationRelayEnabled(this, checked));
        masterRow.addView(masterSwitch);
        panel.addView(masterRow);

        warningText = RetroUi.body(this, "", 12, RetroUi.ACCENT);
        warningText.setPadding(0, RetroUi.dp(this, 6), 0, 0);
        panel.addView(warningText);

        android.widget.Button accessButton =
                RetroUi.pill(this, "Notification access settings");
        LinearLayout.LayoutParams accessParams =
                new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT);
        accessParams.topMargin = RetroUi.dp(this, 8);
        accessButton.setOnClickListener(view -> startActivity(
                new Intent(
                        Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)));
        panel.addView(RetroUi.buttonRow(this, accessButton), accessParams);

        panel.addView(RetroUi.sectionLabel(this, "From these apps"));
        appList = new LinearLayout(this);
        appList.setOrientation(LinearLayout.VERTICAL);
        populateAppList();

        ScrollView scroll = new ScrollView(this);
        scroll.setBackground(
                RetroUi.wellBackground(this, RetroUi.PANEL_SUNK));
        final int pad = RetroUi.dp(this, 10);
        scroll.setPadding(pad, pad, pad, pad);
        scroll.addView(appList, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        panel.addView(scroll, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1.0f));

        page.addView(panel, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        return page;
    }

    private void updateWarning() {
        if (warningText == null) {
            return;
        }

        boolean systemAccessGranted = hasNotificationAccess();
        boolean autoSyncOn = SyncPreferences.isAutoSyncEnabled(this);

        if (!systemAccessGranted) {
            warningText.setText(
                    "Android also needs notification access granted below, " +
                    "or nothing will be seen to forward.");
        } else if (!autoSyncOn) {
            warningText.setText(
                    "Auto sync (on the main screen) is off - forwarded " +
                    "notifications have no background connection to travel " +
                    "over until it's on.");
        } else {
            warningText.setText("");
        }
    }

    private boolean hasNotificationAccess() {
        String enabledListeners = Settings.Secure.getString(
                getContentResolver(), "enabled_notification_listeners");
        return enabledListeners != null &&
               enabledListeners.contains(getPackageName());
    }

    /** One app: name, package, and a switch bound straight to the stored
     * allowlist so a kill mid-session cannot lose a change. */
    private void populateAppList() {
        appList.removeAllViews();

        final Set<String> allowed =
                SyncPreferences.notificationRelayApps(this);
        final PackageManager pm = getPackageManager();
        final List<ApplicationInfo> installed =
                pm.getInstalledApplications(PackageManager.GET_META_DATA);

        final List<ApplicationInfo> launchable = new ArrayList<>();
        for (ApplicationInfo info : installed) {
            if (info.packageName.equals(getPackageName())) {
                continue;
            }
            if (pm.getLaunchIntentForPackage(info.packageName) == null) {
                /* No launcher entry means nothing a person opens directly -
                 * skip the long tail of system/background packages. */
                continue;
            }
            launchable.add(info);
        }
        Collections.sort(launchable, Comparator.comparing(
                info -> String.valueOf(info.loadLabel(pm)).toLowerCase()));

        for (ApplicationInfo info : launchable) {
            appList.addView(buildAppRow(pm, info, allowed));
        }
    }

    private View buildAppRow(PackageManager pm, ApplicationInfo info,
                             Set<String> allowed) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, RetroUi.dp(this, 6), 0, RetroUi.dp(this, 6));

        String label = String.valueOf(info.loadLabel(pm));
        if (TextUtils.isEmpty(label)) {
            label = info.packageName;
        }
        TextView name = RetroUi.body(this, label, 13, RetroUi.INK);
        row.addView(name, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f));

        Switch toggle = new Switch(this);
        toggle.setChecked(allowed.contains(info.packageName));
        toggle.setOnCheckedChangeListener(
                new CompoundButton.OnCheckedChangeListener() {
                    @Override
                    public void onCheckedChanged(CompoundButton button,
                                                 boolean checked) {
                        Set<String> current = new HashSet<>(
                                SyncPreferences.notificationRelayApps(
                                        NotificationAppsActivity.this));
                        if (checked) {
                            current.add(info.packageName);
                        } else {
                            current.remove(info.packageName);
                        }
                        SyncPreferences.setNotificationRelayApps(
                                NotificationAppsActivity.this, current);
                    }
                });
        row.addView(toggle);
        return row;
    }
}
