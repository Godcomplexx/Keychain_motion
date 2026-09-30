#include "round_orientation.h"

#include <stdio.h>
#include <stdlib.h>

#include "board_config.h"
#include "esp_log.h"
#include "oled_display.h"

#define CENTER 120
#define BALL_TRAVEL 80   /* Ball offset in pixels at 1 g. */
#define BALL_RADIUS 12
#define DOMINANT_G 0.80f /* An axis reading this much is pointing up. */
#define STABLE_MS 1500U  /* A pose must hold this long to be recorded. */

typedef enum {
    STEP_FLAT,
    STEP_UPRIGHT,
    STEP_DONE,
} orientation_step_t;

static const char *TAG = "round_orient";
static orientation_step_t s_step;
static int s_out;
static int s_up;
static int s_right;
static int s_candidate;
static uint32_t s_candidate_ms;

/* The chip axis pointing up (+1..+3 for X/Y/Z, negative if flipped), or 0. */
static int dominant_axis(const round_motion_sample_t *raw)
{
    const float values[3] = {raw->x_g, raw->y_g, raw->z_g};
    for (int index = 0; index < 3; ++index) {
        if (values[index] >= DOMINANT_G) { return index + 1; }
        if (values[index] <= -DOMINANT_G) { return -(index + 1); }
    }
    return 0;
}

/* Screen axes are right-handed (right x up = out), so right = up x out. */
static int cross_axes(int up, int out)
{
    int a[3] = {0, 0, 0};
    int b[3] = {0, 0, 0};
    a[abs(up) - 1] = up > 0 ? 1 : -1;
    b[abs(out) - 1] = out > 0 ? 1 : -1;
    const int c[3] = {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    };
    for (int index = 0; index < 3; ++index) {
        if (c[index] != 0) { return c[index] > 0 ? index + 1 : -(index + 1); }
    }
    return 0;
}

static float along(const round_motion_sample_t *raw, int axis)
{
    const int index = abs(axis);
    const float value = index == 1 ? raw->x_g : (index == 2 ? raw->y_g : raw->z_g);
    return axis < 0 ? -value : value;
}

/* "+X", "-Z" and so on, for the three board_config.h values. */
static void axis_name(int axis, char name[3])
{
    name[0] = axis < 0 ? '-' : '+';
    name[1] = axis == 0 ? '?' : "XYZ"[abs(axis) - 1];
    name[2] = '\0';
}

/* The 5x7 font has no "+": on screen a positive axis is just "X". */
static const char *shown(const char name[3])
{
    return name[0] == '+' ? name + 1 : name;
}

/* Text is 12 px per character on this panel; centre each line. */
static void line(int y, const char *text, uint16_t color)
{
    int length = 0;
    while (text[length] != '\0') { ++length; }
    oled_display_draw_text_rgb(CENTER - length * 6, y, text, color);
}

static void fill_circle(int cx, int cy, int radius, uint16_t color)
{
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= radius * radius) {
                oled_display_set_pixel_rgb(cx + dx, cy + dy, color);
            }
        }
    }
}

void round_orientation_start(uint32_t now_ms)
{
    s_step = STEP_FLAT;
    s_candidate = 0;
    s_candidate_ms = now_ms;
    ESP_LOGI(TAG, "Orientation check: lay flat screen up, then stand upright");
}

esp_err_t round_orientation_render(const round_motion_sample_t *raw,
                                   uint32_t now_ms)
{
    const uint16_t white = oled_display_rgb(230, 230, 230);
    const uint16_t accent = oled_display_rgb(90, 200, 255);
    const uint16_t good = oled_display_rgb(90, 220, 130);
    const uint16_t warn = oled_display_rgb(240, 180, 60);
    char text[24];

    oled_display_clear();
    if (raw == NULL) {
        line(110, "NO SENSOR", warn);
        return oled_display_present();
    }

    /* Record a pose only after it has held still with one axis pointing up. */
    const int axis = dominant_axis(raw);
    if (axis != s_candidate) {
        s_candidate = axis;
        s_candidate_ms = now_ms;
    }
    const uint32_t held_ms = now_ms - s_candidate_ms;
    const bool stable = axis != 0 && held_ms >= STABLE_MS;
    if (s_step == STEP_FLAT && stable) {
        s_out = axis;
        s_step = STEP_UPRIGHT;
    } else if (s_step == STEP_UPRIGHT && stable && abs(axis) != abs(s_out)) {
        s_up = axis;
        s_right = cross_axes(s_up, s_out);
        s_step = STEP_DONE;
        char right[3], up[3], out[3];
        axis_name(s_right, right);
        axis_name(s_up, up);
        axis_name(s_out, out);
        ESP_LOGI(TAG, "Measured: RIGHT %s UP %s OUT %s -> board_config.h "
                 "AXIS_RIGHT %d, AXIS_UP %d, AXIS_OUT %d",
                 right, up, out, s_right, s_up, s_out);
    }

    if (s_step == STEP_DONE) {
        char name[3];
        axis_name(s_right, name);
        snprintf(text, sizeof(text), "RIGHT %s", shown(name));
        line(40, text, white);
        axis_name(s_up, name);
        snprintf(text, sizeof(text), "UP    %s", shown(name));
        line(58, text, white);
        axis_name(s_out, name);
        snprintf(text, sizeof(text), "OUT   %s", shown(name));
        line(76, text, white);
        const bool matches = s_right == ROUND_MPU_AXIS_RIGHT &&
                             s_up == ROUND_MPU_AXIS_UP &&
                             s_out == ROUND_MPU_AXIS_OUT;
        line(180, matches ? "CONFIG OK" : "EDIT CONFIG", matches ? good : warn);

        /* With the measured axes the ball always drops to the real floor. */
        const int bx = CENTER - (int)(along(raw, s_right) * BALL_TRAVEL);
        const int by = CENTER + (int)(along(raw, s_up) * BALL_TRAVEL);
        fill_circle(bx, by, BALL_RADIUS, accent);
        return oled_display_present();
    }

    line(40, s_step == STEP_FLAT ? "1 OF 2 LAY FLAT" : "2 OF 2 STAND UP", accent);
    line(58, s_step == STEP_FLAT ? "SCREEN UP" : "TOP EDGE UP", white);

    /* Which chip axis points up right now, and how long it has held. */
    char name[3];
    axis_name(axis, name);
    snprintf(text, sizeof(text), "UP NOW %s", axis == 0 ? "--" : shown(name));
    line(100, text, white);
    const int progress = axis == 0 ? 0 :
        (int)((held_ms > STABLE_MS ? STABLE_MS : held_ms) * 120U / STABLE_MS);
    for (int y = 122; y < 128; ++y) {
        for (int x = 0; x < progress; ++x) {
            oled_display_set_pixel_rgb(60 + x, y, good);
        }
    }

    snprintf(text, sizeof(text), "X %.2f", raw->x_g);
    line(146, text, white);
    snprintf(text, sizeof(text), "Y %.2f", raw->y_g);
    line(164, text, white);
    snprintf(text, sizeof(text), "Z %.2f", raw->z_g);
    line(182, text, white);
    return oled_display_present();
}
