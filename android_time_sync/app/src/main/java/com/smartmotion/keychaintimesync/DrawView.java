package com.smartmotion.keychaintimesync;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.view.MotionEvent;
import android.view.View;

/**
 * The drawing surface. It keeps a 128x64 bitmap - the size of the keychain's
 * screen - and scales it up unfiltered, so what you see is the pixels the OLED
 * will light rather than a smooth phone-quality line that turns out chunky when
 * it arrives.
 *
 * The rasterization below is deliberately the same Bresenham line and round nib
 * the firmware uses. Letting Android's own path renderer draw the preview would
 * have been shorter, but it anti-aliases and rounds ends differently, so the
 * preview and the keychain would disagree about every stroke.
 */
final class DrawView extends View {
    interface Listener {
        /** One rasterized touch sample, in keychain pixels. */
        void onPoint(int x, int y, boolean startsStroke);
    }

    static final int CANVAS_WIDTH = 128;
    static final int CANVAS_HEIGHT = 64;
    static final int PEN_RADIUS_MAX = 3;

    private final boolean[] canvas = new boolean[CANVAS_WIDTH * CANVAS_HEIGHT];
    /*
     * Reused rather than allocated per point. A finger produces around a
     * hundred samples a second and this array is 32 KB, so allocating it in
     * the touch path handed the collector three megabytes a second to clean up
     * while the person was trying to draw a smooth line.
     */
    private final int[] pixels = new int[CANVAS_WIDTH * CANVAS_HEIGHT];
    private final Bitmap bitmap = Bitmap.createBitmap(
            CANVAS_WIDTH, CANVAS_HEIGHT, Bitmap.Config.ARGB_8888);
    private final Paint paint = new Paint();
    private final Rect source =
            new Rect(0, 0, CANVAS_WIDTH, CANVAS_HEIGHT);
    private final Rect destination = new Rect();

    private Listener listener;
    private int penRadius;
    private int lastX = -1;
    private int lastY = -1;

    DrawView(Context context) {
        super(context);
        paint.setFilterBitmap(false);
        paint.setAntiAlias(false);
        setBackgroundColor(Color.BLACK);
        clear();
    }

    void setListener(Listener listener) {
        this.listener = listener;
    }

    void setPenRadius(int radius) {
        penRadius = Math.max(0, Math.min(PEN_RADIUS_MAX, radius));
    }

    int getPenRadius() {
        return penRadius;
    }

    void clear() {
        java.util.Arrays.fill(canvas, false);
        lastX = -1;
        lastY = -1;
        pushBitmap();
        invalidate();
    }

    @Override
    protected void onMeasure(int widthSpec, int heightSpec) {
        /* Locked to the screen's 2:1 shape. A square view would stretch every
         * circle into an ellipse on the way to the keychain. */
        final int width = MeasureSpec.getSize(widthSpec);
        setMeasuredDimension(width, width * CANVAS_HEIGHT / CANVAS_WIDTH);
    }

    @Override
    protected void onDraw(Canvas viewCanvas) {
        destination.set(0, 0, getWidth(), getHeight());
        viewCanvas.drawBitmap(bitmap, source, destination, paint);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        final int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) {
            getParent().requestDisallowInterceptTouchEvent(true);
            lastX = -1;
            lastY = -1;
            handleTouch(event.getX(), event.getY(), true);
            return true;
        }

        if (action == MotionEvent.ACTION_MOVE) {
            /*
             * Historical samples matter here: Android batches touch points
             * between frames, and taking only the latest turns a fast stroke
             * into a series of long straight jumps.
             */
            for (int index = 0; index < event.getHistorySize(); ++index) {
                handleTouch(event.getHistoricalX(index),
                            event.getHistoricalY(index), false);
            }
            handleTouch(event.getX(), event.getY(), false);
            return true;
        }

        if (action == MotionEvent.ACTION_UP ||
            action == MotionEvent.ACTION_CANCEL) {
            lastX = -1;
            lastY = -1;
            return true;
        }

        return super.onTouchEvent(event);
    }

    private void handleTouch(float viewX, float viewY, boolean startsStroke) {
        if (getWidth() <= 0 || getHeight() <= 0) {
            return;
        }

        final int x = clamp((int) (viewX * CANVAS_WIDTH / getWidth()),
                            0, CANVAS_WIDTH - 1);
        final int y = clamp((int) (viewY * CANVAS_HEIGHT / getHeight()),
                            0, CANVAS_HEIGHT - 1);

        if (!startsStroke && x == lastX && y == lastY) {
            return;
        }

        if (lastX < 0) {
            stampNib(x, y);
        } else {
            stampLine(lastX, lastY, x, y);
        }
        lastX = x;
        lastY = y;

        if (listener != null) {
            listener.onPoint(x, y, startsStroke);
        }
        pushBitmap();
        invalidate();
    }

    private static int clamp(int value, int low, int high) {
        if (value < low) {
            return low;
        }
        return value > high ? high : value;
    }

    private void setPixel(int x, int y) {
        if (x < 0 || x >= CANVAS_WIDTH || y < 0 || y >= CANVAS_HEIGHT) {
            return;
        }
        canvas[y * CANVAS_WIDTH + x] = true;
    }

    private void stampNib(int x, int y) {
        if (penRadius <= 0) {
            setPixel(x, y);
            return;
        }
        for (int dy = -penRadius; dy <= penRadius; ++dy) {
            for (int dx = -penRadius; dx <= penRadius; ++dx) {
                if (dx * dx + dy * dy <= penRadius * penRadius) {
                    setPixel(x + dx, y + dy);
                }
            }
        }
    }

    private void stampLine(int x0, int y0, int x1, int y1) {
        final int dx = Math.abs(x1 - x0);
        final int dy = Math.abs(y1 - y0);
        final int stepX = x0 < x1 ? 1 : -1;
        final int stepY = y0 < y1 ? 1 : -1;
        int error = dx - dy;

        while (true) {
            stampNib(x0, y0);
            if (x0 == x1 && y0 == y1) {
                return;
            }
            final int doubled = error * 2;
            if (doubled > -dy) {
                error -= dy;
                x0 += stepX;
            }
            if (doubled < dx) {
                error += dx;
                y0 += stepY;
            }
        }
    }

    private void pushBitmap() {
        for (int index = 0; index < pixels.length; ++index) {
            /* The OLED is blue-white on black; the preview says so too. */
            pixels[index] = canvas[index] ? 0xFF7FDBFF : 0xFF000000;
        }
        bitmap.setPixels(pixels, 0, CANVAS_WIDTH, 0, 0,
                         CANVAS_WIDTH, CANVAS_HEIGHT);
    }
}
