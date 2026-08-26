package com.smartmotion.keychaintimesync;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.LayerDrawable;
import android.graphics.drawable.StateListDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * The look: a cream panel with a hard black edge and an offset shadow, chunky
 * outlined pills, and small letter-spaced capitals for labels.
 *
 * It lives in code rather than in XML because the whole app is built in code
 * already, and splitting the styling across two places would mean every screen
 * needs both a layout file and its Java to stay in step.
 *
 * On the typeface: the reference uses a bitmap pixel font. Shipping one means
 * shipping a binary asset with its own licence, so this uses bold monospace,
 * upper case and wide letter spacing instead - the same rhythm without the
 * blob. Dropping a real pixel font in later is one change, in TYPEFACE.
 */
final class RetroUi {
    static final int INK = 0xFF1B1A16;
    static final int PANEL = 0xFFEAE6D9;
    static final int PANEL_SUNK = 0xFFDBD6C4;
    static final int MUTED = 0xFF6E6858;
    static final int ACCENT = 0xFFD8402F;
    static final int BACKDROP = 0xFF201F1B;
    static final int SCREEN = 0xFF0B0B0A;

    static final Typeface TYPEFACE =
            Typeface.create(Typeface.MONOSPACE, Typeface.BOLD);

    private RetroUi() {
    }

    static int dp(Context context, float value) {
        return Math.round(TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, value,
                context.getResources().getDisplayMetrics()));
    }

    /**
     * Pushes the content clear of the status and navigation bars.
     *
     * The panel is a light surface that reaches the top of the window, so
     * without this the title sits underneath the system clock and the two
     * overlap. Insets arrive after layout, hence the listener rather than a
     * fixed padding.
     */
    static void insetForSystemBars(View page, int extra) {
        page.setOnApplyWindowInsetsListener((view, insets) -> {
            final android.graphics.Insets bars = insets.getInsets(
                    android.view.WindowInsets.Type.systemBars());
            view.setPadding(extra + bars.left, extra + bars.top,
                            extra + bars.right, extra + bars.bottom);
            return insets;
        });
        page.requestApplyInsets();
    }

    /* ---------- surfaces ---------- */

    /** The cream card everything sits on, with its hard offset shadow. */
    static LinearLayout panel(Context context) {
        LinearLayout panel = new LinearLayout(context);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setBackground(panelBackground(context));
        final int inset = dp(context, 14);
        panel.setPadding(inset, dp(context, 10), inset, inset);
        return panel;
    }

    private static Drawable panelBackground(Context context) {
        final int radius = dp(context, 10);
        final int offset = dp(context, 5);

        GradientDrawable shadow = new GradientDrawable();
        shadow.setColor(INK);
        shadow.setCornerRadius(radius);

        GradientDrawable face = new GradientDrawable();
        face.setColor(PANEL);
        face.setCornerRadius(radius);
        face.setStroke(dp(context, 2.5f), INK);

        LayerDrawable layers = new LayerDrawable(new Drawable[] {shadow, face});
        /* The shadow is a solid shape pushed down and right, not a blur:
         * blurred shadows read as material design, which is the opposite of
         * what this is. */
        layers.setLayerInset(0, offset, offset, 0, 0);
        layers.setLayerInset(1, 0, 0, offset, offset);
        return layers;
    }

    /** A recessed area: the log, the canvas frame. */
    static Drawable wellBackground(Context context, int fill) {
        GradientDrawable well = new GradientDrawable();
        well.setColor(fill);
        well.setCornerRadius(dp(context, 6));
        well.setStroke(dp(context, 2), INK);
        return well;
    }

    /* ---------- text ---------- */

    /** The panel's own title bar: name on the left, a tag or close on the right. */
    static LinearLayout titleBar(Context context, String name, View trailing) {
        LinearLayout bar = new LinearLayout(context);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);

        TextView title = new TextView(context);
        title.setText(name.toUpperCase());
        title.setTypeface(TYPEFACE);
        title.setTextSize(17);
        title.setLetterSpacing(0.16f);
        title.setTextColor(INK);
        bar.addView(title, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f));

        if (trailing != null) {
            bar.addView(trailing);
        }
        return bar;
    }

    /** The red square in the corner of the reference panel. */
    static TextView closeButton(Context context, Runnable onClose) {
        TextView close = new TextView(context);
        close.setText("X");
        close.setTypeface(TYPEFACE);
        close.setTextSize(14);
        close.setTextColor(PANEL);
        close.setGravity(Gravity.CENTER);

        GradientDrawable face = new GradientDrawable();
        face.setColor(ACCENT);
        face.setCornerRadius(dp(context, 5));
        face.setStroke(dp(context, 2), INK);
        close.setBackground(face);

        final int size = dp(context, 30);
        close.setLayoutParams(new LinearLayout.LayoutParams(size, size));
        close.setOnClickListener(view -> onClose.run());
        return close;
    }

    /** ASPECT RATIO, WINDOW, FORMAT - the small capitals above each group. */
    static TextView sectionLabel(Context context, String text) {
        TextView label = new TextView(context);
        label.setText(text.toUpperCase());
        label.setTypeface(TYPEFACE);
        label.setTextSize(11);
        label.setLetterSpacing(0.22f);
        label.setTextColor(MUTED);
        label.setPadding(dp(context, 2), dp(context, 14), 0, dp(context, 6));
        return label;
    }

    static TextView body(Context context, String text, int size, int colour) {
        TextView view = new TextView(context);
        view.setText(text);
        view.setTypeface(TYPEFACE);
        view.setTextSize(size);
        view.setTextColor(colour);
        return view;
    }

    /** The hairline under a title bar. */
    static View rule(Context context) {
        View rule = new View(context);
        rule.setBackgroundColor(INK);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(context, 2));
        params.topMargin = dp(context, 8);
        rule.setLayoutParams(params);
        return rule;
    }

    /* ---------- controls ---------- */

    static Button pill(Context context, String text) {
        Button button = new Button(context);
        button.setText(text.toUpperCase());
        button.setAllCaps(false);
        button.setTypeface(TYPEFACE);
        button.setTextSize(13);
        button.setLetterSpacing(0.10f);
        button.setTextColor(pillTextColours());
        button.setBackground(pillBackground(context));
        button.setStateListAnimator(null);
        button.setPadding(dp(context, 14), 0, dp(context, 14), 0);
        button.setMinWidth(0);
        button.setMinimumWidth(0);
        return button;
    }

    static Drawable pillBackground(Context context) {
        StateListDrawable states = new StateListDrawable();
        states.addState(new int[] {android.R.attr.state_pressed},
                        pillFace(context, INK, INK));
        states.addState(new int[] {android.R.attr.state_selected},
                        pillFace(context, INK, INK));
        states.addState(new int[] {-android.R.attr.state_enabled},
                        pillFace(context, PANEL_SUNK, MUTED));
        states.addState(new int[] {}, pillFace(context, PANEL, INK));
        return states;
    }

    /**
     * Selected and pressed invert to ink-on-cream, so the text colour has to
     * follow the same states or a chosen pill reads as black on black.
     */
    static ColorStateList pillTextColours() {
        return new ColorStateList(
                new int[][] {
                    new int[] {android.R.attr.state_pressed},
                    new int[] {android.R.attr.state_selected},
                    new int[] {-android.R.attr.state_enabled},
                    new int[] {},
                },
                new int[] {PANEL, PANEL, MUTED, INK});
    }

    private static GradientDrawable pillFace(Context context, int fill,
                                             int edge) {
        GradientDrawable face = new GradientDrawable();
        face.setColor(fill);
        face.setCornerRadius(dp(context, 18));
        face.setStroke(dp(context, 2), edge);
        return face;
    }

    /** A row of pills that behaves like one control: exactly one is chosen. */
    static LinearLayout segmented(Context context, String[] options,
                                  int selectedIndex,
                                  SegmentListener listener) {
        LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.HORIZONTAL);

        final Button[] buttons = new Button[options.length];
        for (int index = 0; index < options.length; ++index) {
            final int choice = index;
            Button button = pill(context, options[index]);
            button.setSelected(index == selectedIndex);
            button.setOnClickListener(view -> {
                for (Button other : buttons) {
                    other.setSelected(other == view);
                }
                listener.onSelected(choice);
            });
            buttons[index] = button;

            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                    0, dp(context, 40), 1.0f);
            if (index > 0) {
                params.leftMargin = dp(context, 6);
            }
            row.addView(button, params);
        }
        return row;
    }

    interface SegmentListener {
        void onSelected(int index);
    }

    /** A row of independent buttons, evenly divided. */
    static LinearLayout buttonRow(Context context, Button... buttons) {
        LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.HORIZONTAL);
        for (int index = 0; index < buttons.length; ++index) {
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                    0, dp(context, 46), 1.0f);
            if (index > 0) {
                params.leftMargin = dp(context, 8);
            }
            row.addView(buttons[index], params);
        }
        return row;
    }
}
