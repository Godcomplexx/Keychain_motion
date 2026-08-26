#include "time_animation.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "oled_display.h"

/*
 * The clock, drawn in the same language as the phone app: a hard framed
 * panel, a title in small letter-spaced capitals with a rule under it, and
 * the reading itself large enough to take at a glance.
 *
 * It used to be three lines of 5x7 text stacked in the middle of an empty
 * screen, which is legible but says nothing about what it belongs to. The
 * time is what someone came here to read, so it gets the room.
 */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define FRAME_INSET 3
#define TITLE_Y 7
#define RULE_Y 17
/* Three times the 5x7 font: 21 px tall, which is as much as fits with the
 * title bar and the footer both present. */
#define BIG_SCALE 3
#define BIG_Y 23
#define FOOTER_Y 50
#define PROGRESS_Y 59
#define PROGRESS_HEIGHT 2
/* The gap between the large HH:MM and the small seconds beside it. */
#define SECONDS_GAP 4
/* The 5x7 font's own height, needed to sit the seconds on the same baseline. */
#define FONT_HEIGHT 7

static unsigned int clamp_to_two_digits(uint8_t value)
{
    if (value > 99U) {
        return 99U;
    }

    return (unsigned int)value;
}

static unsigned int clamp_year(uint16_t year)
{
    if (year > 9999U) {
        return 9999U;
    }

    return (unsigned int)year;
}

static unsigned int clamp_month(uint8_t month)
{
    if (month < 1U) {
        return 1U;
    }
    if (month > 12U) {
        return 12U;
    }

    return (unsigned int)month;
}

static unsigned int clamp_day(uint8_t day)
{
    if (day < 1U) {
        return 1U;
    }
    if (day > 31U) {
        return 31U;
    }

    return (unsigned int)day;
}

/*
 * How much of the time on screen is left, as a bar along the bottom edge.
 * The screen closes itself, and a bar draining is the one way to say so
 * without spending a line of text on it.
 */
static void draw_progress_bar(int64_t elapsed_us, int64_t duration_us)
{
    const int x = FRAME_INSET + 3;
    const int width = SCREEN_WIDTH - 2 * x;
    int remaining = width;

    if (duration_us > 0) {
        int64_t used = elapsed_us;
        if (used < 0) {
            used = 0;
        }
        if (used > duration_us) {
            used = duration_us;
        }
        remaining = (int)(((duration_us - used) * width) / duration_us);
    }

    oled_display_fill_rect(x, PROGRESS_Y, remaining, PROGRESS_HEIGHT, true);
}

esp_err_t time_animation_render(const time_animation_view_t *view,
                                int64_t elapsed_us,
                                int64_t duration_us)
{
    if (view == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char big_text[6];
    char seconds_text[3];
    char date_text[11];

    snprintf(big_text, sizeof(big_text), "%02u:%02u",
             clamp_to_two_digits(view->hour),
             clamp_to_two_digits(view->minute));
    snprintf(seconds_text, sizeof(seconds_text), "%02u",
             clamp_to_two_digits(view->second));
    snprintf(date_text, sizeof(date_text), "%04u-%02u-%02u",
             clamp_year(view->year),
             clamp_month(view->month),
             clamp_day(view->day));

    /*
     * SYNCED means a phone time write was accepted. NO SYNC means the screen
     * is still showing the firmware's own count since power-up, which is the
     * difference between a clock and a stopwatch nobody set.
     */
    const char *status = view->clock_synced ? "SYNCED" : "NO SYNC";

    oled_display_clear();

    oled_display_draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, true);
    oled_display_draw_text(FRAME_INSET + 3, TITLE_Y, "CLOCK");
    oled_display_fill_rect(FRAME_INSET + 3, RULE_Y,
                           SCREEN_WIDTH - 2 * (FRAME_INSET + 3), 1, true);

    /* The status sits at the right end of the title bar, as it does in the
     * app's panel. */
    const int status_width = oled_display_text_width(status, 1);
    oled_display_draw_text(SCREEN_WIDTH - FRAME_INSET - 3 - status_width,
                           TITLE_Y, status);

    /*
     * The hours and minutes are centred on the screen and the seconds hang off
     * to the right of them. Centring the pair together instead pushed the part
     * anyone actually reads off to the left.
     */
    const int big_width = oled_display_text_width(big_text, BIG_SCALE);
    const int big_x = (SCREEN_WIDTH - big_width) / 2;

    oled_display_draw_text_scaled(big_x, BIG_Y, big_text, BIG_SCALE);
    /* Seconds ride along the bottom of the big digits rather than their top,
     * so the two read as one reading instead of two numbers. */
    oled_display_draw_text(big_x + big_width + SECONDS_GAP,
                           BIG_Y + FONT_HEIGHT * BIG_SCALE - FONT_HEIGHT,
                           seconds_text);

    const int date_width = oled_display_text_width(date_text, 1);
    oled_display_draw_text((SCREEN_WIDTH - date_width) / 2, FOOTER_Y,
                           date_text);
    draw_progress_bar(elapsed_us, duration_us);

    return oled_display_present();
}
