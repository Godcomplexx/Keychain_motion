package com.smartmotion.keychaintimesync;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.List;

public class MainActivity extends Activity {
    private static final int PERMISSION_REQUEST_CODE = 42;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    private TextView statusText;
    private TextView logText;
    private ScrollView logWindow;
    private Button syncButton;
    private Button autoButton;
    private Button timeButton;
    private Button gameButton;
    private Button drawButton;
    private KeychainBleSync bleSync;
    private boolean autoSyncEnabled;
    /* What to say when the current manual operation succeeds. */
    private String manualSuccessStatus = "Synced";
    private boolean manualOperationRunning;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(createLayout());

        bleSync = new KeychainBleSync(this, new KeychainBleSync.Listener() {
            @Override
            public void onLog(String message) {
                log(message);
            }

            @Override
            public void onFinished(boolean success) {
                setBusy(false);
                setStatus(success
                        ? manualSuccessStatus
                        : "Ready");
                manualSuccessStatus = "Synced";
            }
        });

        syncButton.setOnClickListener(view -> startManualSync());
        autoButton.setOnClickListener(view -> toggleAutoSync());
        timeButton.setOnClickListener(view -> showTime());
        gameButton.setOnClickListener(view -> startBreakout());
        drawButton.setOnClickListener(view -> openDrawPad());
        autoSyncEnabled = SyncPreferences.isAutoSyncEnabled(this);
        updateAutoSyncUi();
        setStatus(autoSyncEnabled ? "Watching in background" : "Ready");

        if (!hasRequiredPermissions()) {
            requestRequiredPermissions();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        autoSyncEnabled = SyncPreferences.isAutoSyncEnabled(this);
        updateAutoSyncUi();
    }

    @Override
    protected void onDestroy() {
        if (bleSync != null) {
            bleSync.cancel();
        }
        super.onDestroy();
    }

    private View createLayout() {
        /*
         * A panel floating on a dark backdrop, the way the reference lays it
         * out: title bar, then labelled groups of pills, then a recessed area
         * for the running log.
         */
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setBackgroundColor(RetroUi.BACKDROP);
        page.setClipToPadding(false);
        RetroUi.insetForSystemBars(page, RetroUi.dp(this, 14));

        LinearLayout panel = RetroUi.panel(this);

        TextView version = RetroUi.body(this, "V0.5", 12, RetroUi.MUTED);
        version.setLetterSpacing(0.16f);
        panel.addView(RetroUi.titleBar(this, "Keychain Sync", version));
        panel.addView(RetroUi.rule(this));

        panel.addView(RetroUi.sectionLabel(this, "Status"));
        statusText = RetroUi.body(this, "", 14, RetroUi.INK);
        panel.addView(statusText, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        panel.addView(RetroUi.sectionLabel(this, "Clock"));
        syncButton = RetroUi.pill(this, "Sync now");
        autoButton = RetroUi.pill(this, "Auto sync");
        panel.addView(RetroUi.buttonRow(this, syncButton, autoButton));

        panel.addView(RetroUi.sectionLabel(this, "On the keychain"));
        timeButton = RetroUi.pill(this, "Show time");
        gameButton = RetroUi.pill(this, "Breakout");
        panel.addView(RetroUi.buttonRow(this, timeButton, gameButton));

        drawButton = RetroUi.pill(this, "Draw pad");
        LinearLayout.LayoutParams drawParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        drawParams.topMargin = RetroUi.dp(this, 8);
        panel.addView(RetroUi.buttonRow(this, drawButton), drawParams);

        panel.addView(RetroUi.sectionLabel(this, "Log"));
        logText = RetroUi.body(this, "", 11, RetroUi.INK);
        logText.setGravity(Gravity.START);

        logWindow = new ScrollView(this);
        logWindow.setBackground(
                RetroUi.wellBackground(this, RetroUi.PANEL_SUNK));
        final int pad = RetroUi.dp(this, 10);
        logWindow.setPadding(pad, pad, pad, pad);
        logWindow.addView(logText, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        /* The log takes whatever height is left, so the panel reaches the
         * bottom of the screen instead of floating with dead space under it. */
        panel.addView(logWindow, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1.0f));

        page.addView(panel, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        return page;
    }

    private void startManualSync() {
        if (!hasRequiredPermissions()) {
            requestRequiredPermissions();
            return;
        }

        if (autoSyncEnabled) {
            log("Stop auto sync before starting a manual scan");
            return;
        }

        setBusy(true);
        manualSuccessStatus = "Synced";
        setStatus("Scanning");
        bleSync.start();
    }

    private void openDrawPad() {
        if (!hasRequiredPermissions()) {
            requestRequiredPermissions();
            return;
        }

        /* The pad keeps its own connection open, so it does not go through the
         * one-shot command path the other buttons use. */
        startActivity(DrawActivity.intent(this));
    }

    private void showTime() {
        if (!hasRequiredPermissions()) {
            requestRequiredPermissions();
            return;
        }

        setStatus("Waiting for keychain");
        if (autoSyncEnabled) {
            Intent intent = new Intent(this, KeychainSyncService.class);
            intent.setAction(KeychainSyncService.ACTION_SHOW_TIME);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                startForegroundService(intent);
            } else {
                startService(intent);
            }
            log("Clock request queued for the next BLE window");
            return;
        }

        setBusy(true);
        manualSuccessStatus = "Clock shown";
        bleSync.showTime();
    }

    private void startBreakout() {
        if (!hasRequiredPermissions()) {
            requestRequiredPermissions();
            return;
        }

        setStatus("Waiting for keychain");
        if (autoSyncEnabled) {
            Intent intent = new Intent(this, KeychainSyncService.class);
            intent.setAction(KeychainSyncService.ACTION_START_GAME);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                startForegroundService(intent);
            } else {
                startService(intent);
            }
            log("Breakout request queued for the next BLE window");
            return;
        }

        setBusy(true);
        manualSuccessStatus = "Breakout started";
        bleSync.startGame();
    }

    private void toggleAutoSync() {
        if (!hasRequiredPermissions()) {
            requestRequiredPermissions();
            return;
        }

        Intent intent = new Intent(this, KeychainSyncService.class);
        if (autoSyncEnabled) {
            intent.setAction(KeychainSyncService.ACTION_STOP);
            startService(intent);
            autoSyncEnabled = false;
            SyncPreferences.setAutoSyncEnabled(this, false);
            updateAutoSyncUi();
            setStatus("Ready");
            log("Background auto sync stopped");
        } else {
            intent.setAction(KeychainSyncService.ACTION_START);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                startForegroundService(intent);
            } else {
                startService(intent);
            }
            autoSyncEnabled = true;
            SyncPreferences.setAutoSyncEnabled(this, true);
            updateAutoSyncUi();
            setStatus("Watching in background");
            log("Background auto sync started");
        }
    }

    private void updateAutoSyncUi() {
        if (autoButton == null || syncButton == null ||
            gameButton == null || timeButton == null ||
            drawButton == null) {
            return;
        }
        /*
         * A filled pill means "on", the way a chosen option is filled in the
         * rest of the panel. It used to swap the label between Start and Stop,
         * which made the button change width and read as a different control.
         */
        autoButton.setSelected(autoSyncEnabled);
        syncButton.setEnabled(!autoSyncEnabled &&
                              !manualOperationRunning);
        autoButton.setEnabled(!manualOperationRunning);
        timeButton.setEnabled(!manualOperationRunning);
        gameButton.setEnabled(!manualOperationRunning);
        drawButton.setEnabled(!manualOperationRunning);
    }

    private boolean hasRequiredPermissions() {
        for (String permission : requiredPermissions()) {
            if (checkSelfPermission(permission) !=
                PackageManager.PERMISSION_GRANTED) {
                return false;
            }
        }
        return true;
    }

    private void requestRequiredPermissions() {
        requestPermissions(requiredPermissions(), PERMISSION_REQUEST_CODE);
    }

    private String[] requiredPermissions() {
        List<String> permissions = new ArrayList<>();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            permissions.add(Manifest.permission.BLUETOOTH_SCAN);
            permissions.add(Manifest.permission.BLUETOOTH_CONNECT);
        } else {
            permissions.add(Manifest.permission.ACCESS_FINE_LOCATION);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            permissions.add(Manifest.permission.POST_NOTIFICATIONS);
        }
        return permissions.toArray(new String[0]);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode,
                                           String[] permissions,
                                           int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != PERMISSION_REQUEST_CODE) {
            return;
        }

        if (hasRequiredPermissions()) {
            log("Permissions granted");
        } else {
            log("Bluetooth permissions are required");
        }
    }

    private void setBusy(boolean busy) {
        manualOperationRunning = busy;
        mainHandler.post(() -> {
            syncButton.setEnabled(!busy && !autoSyncEnabled);
            autoButton.setEnabled(!busy);
            timeButton.setEnabled(!busy);
            gameButton.setEnabled(!busy);
            drawButton.setEnabled(!busy);
        });
    }

    private void setStatus(String status) {
        /* The section above it already says STATUS. */
        mainHandler.post(() -> statusText.setText(status));
    }

    private void log(String message) {
        mainHandler.post(() -> {
            logText.append(message + "\n");
            int scrollAmount = logText.getLayout() == null
                    ? 0
                    : logText.getLayout().getLineTop(logText.getLineCount()) -
                      logText.getHeight();
            if (scrollAmount > 0) {
                logText.scrollTo(0, scrollAmount);
            }
        });
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density);
    }
}
