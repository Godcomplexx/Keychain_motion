package com.smartmotion.keychaintimesync;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * Draw on the phone, watch it appear on the keychain.
 *
 * Points are buffered and flushed on a timer rather than sent one per touch
 * sample: a finger produces samples far faster than BLE will carry single-point
 * writes, and one packet holding a whole flush is both faster and smoother.
 */
public final class DrawActivity extends Activity
        implements DrawView.Listener, KeychainDrawSession.Listener {
    /* Roughly a screen refresh. Longer batches more points per packet but the
     * stroke visibly trails the finger. */
    private static final long FLUSH_INTERVAL_MS = 40L;

    private final Handler handler = new Handler(Looper.getMainLooper());

    private DrawView drawView;
    private TextView statusText;
    private KeychainDrawSession session;

    private int[] buffer = new int[512];
    private int bufferedPoints;
    private boolean bufferStartsStroke;
    private boolean flushScheduled;

    private final Runnable flush = new Runnable() {
        @Override
        public void run() {
            flushScheduled = false;
            flushBuffer();
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(createLayout());

        /* Auto sync and the drawing pad cannot both hold the one connection
         * the keychain allows, so the background service stands down first. */
        sendServiceAction(KeychainSyncService.ACTION_PAUSE);
        session = new KeychainDrawSession(this, this);
        setStatus("Connecting");
        session.start();
    }

    @Override
    protected void onDestroy() {
        handler.removeCallbacks(flush);
        if (session != null) {
            session.stop();
        }
        sendServiceAction(KeychainSyncService.ACTION_RESUME);
        super.onDestroy();
    }

    private View createLayout() {
        ScrollView page = new ScrollView(this);
        page.setBackgroundColor(RetroUi.BACKDROP);
        page.setClipToPadding(false);
        RetroUi.insetForSystemBars(page, RetroUi.dp(this, 14));

        LinearLayout panel = RetroUi.panel(this);
        panel.addView(RetroUi.titleBar(this, "Draw pad",
                                       RetroUi.closeButton(this, this::finish)));
        panel.addView(RetroUi.rule(this));

        panel.addView(RetroUi.sectionLabel(this, "Link"));
        statusText = RetroUi.body(this, "", 13, RetroUi.INK);
        panel.addView(statusText, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        panel.addView(RetroUi.sectionLabel(this, "Canvas"));
        drawView = new DrawView(this);
        drawView.setListener(this);
        drawView.setPenRadius(0);

        /* The canvas sits in its own recessed frame, like the preview pane in
         * the reference, so it reads as the keychain's screen rather than as
         * part of the panel. */
        FrameLayout frame = new FrameLayout(this);
        frame.setBackground(RetroUi.wellBackground(this, RetroUi.SCREEN));
        final int inset = RetroUi.dp(this, 4);
        frame.setPadding(inset, inset, inset, inset);
        frame.addView(drawView, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        panel.addView(frame, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        TextView caption = RetroUi.body(
                this,
                DrawView.CANVAS_WIDTH + " x " + DrawView.CANVAS_HEIGHT
                        + "  ·  LIVE MIRROR",
                10, RetroUi.MUTED);
        caption.setLetterSpacing(0.18f);
        caption.setGravity(Gravity.CENTER);
        caption.setPadding(0, RetroUi.dp(this, 6), 0, 0);
        panel.addView(caption, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        panel.addView(RetroUi.sectionLabel(this, "Pen size"));
        /* Four discrete widths, so a row of pills says more than a slider
         * would: you can see every choice and which one is active. */
        panel.addView(RetroUi.segmented(
                this, new String[] {"1PX", "3PX", "5PX", "7PX"}, 0,
                index -> {
                    drawView.setPenRadius(index);
                    if (session != null && session.isReady()) {
                        /* Pen size must land before the next stroke, so
                         * anything already buffered goes out first. */
                        flushBuffer();
                        session.sendPenRadius(index);
                    }
                }));

        panel.addView(RetroUi.sectionLabel(this, "Action"));
        Button clearButton = RetroUi.pill(this, "Clear");
        clearButton.setOnClickListener(view -> {
            bufferedPoints = 0;
            drawView.clear();
            if (session != null) {
                session.sendClear();
            }
        });
        Button doneButton = RetroUi.pill(this, "Done");
        doneButton.setOnClickListener(view -> finish());
        panel.addView(RetroUi.buttonRow(this, clearButton, doneButton));

        TextView hint = RetroUi.body(
                this,
                "The picture stays on the keychain until you clear it or "
                        + "close the pad.",
                11, RetroUi.MUTED);
        hint.setPadding(0, RetroUi.dp(this, 14), 0, 0);
        panel.addView(hint, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        page.addView(panel, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        return page;
    }

    @Override
    public void onPoint(int x, int y, boolean startsStroke) {
        if (startsStroke) {
            /* A new stroke must not be appended to the previous packet, or the
             * keychain joins them with a line across the picture. */
            flushBuffer();
            bufferStartsStroke = true;
        }

        if (bufferedPoints * 2 + 2 > buffer.length) {
            flushBuffer();
        }

        buffer[bufferedPoints * 2] = x;
        buffer[bufferedPoints * 2 + 1] = y;
        ++bufferedPoints;

        if (!flushScheduled) {
            flushScheduled = true;
            handler.postDelayed(flush, FLUSH_INTERVAL_MS);
        }
    }

    private void flushBuffer() {
        if (bufferedPoints == 0 || session == null || !session.isReady()) {
            bufferedPoints = 0;
            bufferStartsStroke = false;
            return;
        }

        final int perPacket = Math.max(1, session.pointsPerPacket());
        int sent = 0;
        boolean startsStroke = bufferStartsStroke;
        while (sent < bufferedPoints) {
            final int count = Math.min(perPacket, bufferedPoints - sent);
            int[] slice = new int[count * 2];
            System.arraycopy(buffer, sent * 2, slice, 0, count * 2);
            session.sendPoints(startsStroke, slice, count);
            startsStroke = false;
            sent += count;
        }

        bufferedPoints = 0;
        bufferStartsStroke = false;
    }

    @Override
    public void onReady() {
        setStatus("Connected - draw away");
        session.sendClear();
        session.sendPenRadius(drawView.getPenRadius());
    }

    @Override
    public void onLog(String message) {
        setStatus(message);
    }

    @Override
    public void onClosed(String reason) {
        setStatus(reason);
    }

    private void setStatus(String text) {
        runOnUiThread(() -> {
            if (statusText != null) {
                statusText.setText(text);
            }
        });
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density);
    }

    static Intent intent(android.content.Context context) {
        return new Intent(context, DrawActivity.class);
    }

    private void sendServiceAction(String action) {
        if (!SyncPreferences.isAutoSyncEnabled(this)) {
            return;
        }
        Intent intent = new Intent(this, KeychainSyncService.class);
        intent.setAction(action);
        startService(intent);
    }
}
