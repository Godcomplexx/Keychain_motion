package com.smartmotion.keychaintimesync;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.method.ScrollingMovementMethod;
import android.view.Gravity;
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

    private LinearLayout createLayout() {
        int padding = dp(20);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(padding, padding, padding, padding);
        root.setBackgroundColor(0xFFF7F7F3);

        TextView title = new TextView(this);
        title.setText("Keychain Sync 0.5");
        title.setTextSize(26);
        title.setTextColor(0xFF13201F);
        root.addView(title, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        statusText = new TextView(this);
        statusText.setTextSize(16);
        statusText.setTextColor(0xFF3D4B49);
        statusText.setPadding(0, dp(8), 0, dp(16));
        root.addView(statusText, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        syncButton = new Button(this);
        syncButton.setText("Sync now");
        syncButton.setAllCaps(false);
        root.addView(syncButton, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(52)));

        autoButton = new Button(this);
        autoButton.setText("Start auto sync");
        autoButton.setAllCaps(false);
        root.addView(autoButton, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(52)));

        timeButton = new Button(this);
        timeButton.setText("Show time");
        timeButton.setAllCaps(false);
        root.addView(timeButton, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        gameButton = new Button(this);
        gameButton.setText("Start Breakout");
        gameButton.setAllCaps(false);
        root.addView(gameButton, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(52)));

        drawButton = new Button(this);
        drawButton.setText("Draw pad");
        drawButton.setAllCaps(false);
        root.addView(drawButton, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(52)));

        logText = new TextView(this);
        logText.setTextSize(14);
        logText.setTextColor(0xFF162321);
        logText.setPadding(0, dp(16), 0, 0);
        logText.setGravity(Gravity.START);
        logText.setMovementMethod(new ScrollingMovementMethod());

        ScrollView scrollView = new ScrollView(this);
        scrollView.addView(logText, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        root.addView(scrollView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                0,
                1.0f));

        return root;
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
        autoButton.setText(autoSyncEnabled
                ? "Stop auto sync"
                : "Start auto sync");
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
        mainHandler.post(() -> statusText.setText("Status: " + status));
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
