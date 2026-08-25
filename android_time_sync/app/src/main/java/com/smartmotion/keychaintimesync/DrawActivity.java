package com.smartmotion.keychaintimesync;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.SeekBar;
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

    private LinearLayout createLayout() {
        int padding = dp(16);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(padding, padding, padding, padding);
        root.setBackgroundColor(0xFF13201F);

        TextView title = new TextView(this);
        title.setText("Draw pad");
        title.setTextSize(24);
        title.setTextColor(0xFFF7F7F3);
        root.addView(title, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        statusText = new TextView(this);
        statusText.setTextSize(15);
        statusText.setTextColor(0xFF9FB3B1);
        statusText.setPadding(0, dp(6), 0, dp(12));
        root.addView(statusText, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        drawView = new DrawView(this);
        drawView.setListener(this);
        drawView.setPenRadius(0);
        root.addView(drawView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        TextView penLabel = new TextView(this);
        penLabel.setText("Pen size");
        penLabel.setTextSize(15);
        penLabel.setTextColor(0xFF9FB3B1);
        penLabel.setPadding(0, dp(16), 0, 0);
        root.addView(penLabel, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        SeekBar penSize = new SeekBar(this);
        penSize.setMax(DrawView.PEN_RADIUS_MAX);
        penSize.setProgress(0);
        penSize.setOnSeekBarChangeListener(
                new SeekBar.OnSeekBarChangeListener() {
                    @Override
                    public void onProgressChanged(SeekBar bar, int progress,
                                                  boolean fromUser) {
                        drawView.setPenRadius(progress);
                        if (session != null && session.isReady()) {
                            /* Pen size must land before the next stroke, so
                             * anything already buffered goes out first. */
                            flushBuffer();
                            session.sendPenRadius(progress);
                        }
                    }

                    @Override
                    public void onStartTrackingTouch(SeekBar bar) {
                    }

                    @Override
                    public void onStopTrackingTouch(SeekBar bar) {
                    }
                });
        root.addView(penSize, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        Button clearButton = new Button(this);
        clearButton.setText("Clear");
        clearButton.setAllCaps(false);
        clearButton.setOnClickListener(view -> {
            bufferedPoints = 0;
            drawView.clear();
            if (session != null) {
                session.sendClear();
            }
        });
        root.addView(clearButton, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(52)));

        Button doneButton = new Button(this);
        doneButton.setText("Done");
        doneButton.setAllCaps(false);
        doneButton.setOnClickListener(view -> finish());
        root.addView(doneButton, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                dp(52)));

        TextView hint = new TextView(this);
        hint.setText("The picture stays on the keychain until you clear it or "
                     + "close the pad.");
        hint.setTextSize(13);
        hint.setTextColor(0xFF6E8482);
        hint.setGravity(Gravity.START);
        hint.setPadding(0, dp(12), 0, 0);
        root.addView(hint, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        return root;
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
