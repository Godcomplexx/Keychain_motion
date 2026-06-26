#include "time_animation.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "oled_display.h"

#define PROGRESS_X 12
#define PROGRESS_Y 56
#define PROGRESS_WIDTH 104
#define PROGRESS_HEIGHT 3
#define TIME_TEXT_X 21
#define DATE_TEXT_X 21
#define STEPS_TEXT_X 21
#define STATUS_TEXT_X 52
#define TIME_TEXT_Y 12
#define DATE_TEXT_Y 28
#define STEPS_TEXT_Y 43
#define STATUS_TEXT_Y 0

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

static void draw_filled_rect(int x, int y, int width, int height, bool on)
{
    for (int py = y; py < y + height; ++py) {
        for (int px = x; px < x + width; ++px) {
            oled_display_set_pixel(px, py, on);
        }
    }
}

static void draw_progress_bar(int64_t elapsed_us, int64_t duration_us)
{
    int filled_width = 0;

    if (duration_us > 0 && elapsed_us > 0) {
        filled_width = (int)((elapsed_us * PROGRESS_WIDTH) / duration_us);
        if (filled_width > PROGRESS_WIDTH) {
            filled_width = PROGRESS_WIDTH;
        }
    }

    draw_filled_rect(PROGRESS_X, PROGRESS_Y,
                     PROGRESS_WIDTH, PROGRESS_HEIGHT, false);
    draw_filled_rect(PROGRESS_X, PROGRESS_Y,
                     filled_width, PROGRESS_HEIGHT, true);
}

esp_err_t time_animation_render(const time_animation_view_t *view,
                                int64_t elapsed_us,
                                int64_t duration_us)
{
    if (view == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char time_text[9];
    char date_text[11];
    char steps_text[18];

    snprintf(time_text, sizeof(time_text),
             "%02u:%02u:%02u",
             clamp_to_two_digits(view->hour),
             clamp_to_two_digits(view->minute),
             clamp_to_two_digits(view->second));
    snprintf(date_text, sizeof(date_text),
             "%04u-%02u-%02u",
             clamp_year(view->year),
             clamp_month(view->month),
             clamp_day(view->day));
    snprintf(steps_text, sizeof(steps_text),
             "STEPS %lu",
             (unsigned long)view->steps_today);

    oled_display_clear();

    /*
     * NO means the screen is still using the firmware fallback clock.
     * OK means a phone time write was accepted and applied.
     */
    oled_display_draw_text(STATUS_TEXT_X, STATUS_TEXT_Y,
                           view->clock_synced ? "OK" : "NO");

    /* TIME state MVP: software clock plus approximate daily step count. */
    oled_display_draw_text(TIME_TEXT_X, TIME_TEXT_Y, time_text);
    oled_display_draw_text(DATE_TEXT_X, DATE_TEXT_Y, date_text);
    oled_display_draw_text(STEPS_TEXT_X, STEPS_TEXT_Y, steps_text);
    draw_progress_bar(elapsed_us, duration_us);

    return oled_display_present();
}
