/*
 * Prints the clock screen as ASCII, so its layout can be judged without
 * flashing a keychain and squinting at a 128x64 panel.
 *
 * It implements the oled_display API against a plain array instead of a real
 * panel; the glyph table is the firmware's own, so the shapes and spacing here
 * are the shapes and spacing on the device.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "oled_display.h"
#include "time_animation.h"

#include "font_5x7.h"

#define WIDTH 128
#define HEIGHT 64
#define FONT_WIDTH 5
#define FONT_HEIGHT 7
#define FONT_ADVANCE 6

static bool s_pixels[WIDTH * HEIGHT];

static const uint8_t *glyph(char character)
{
    for (unsigned int index = 0; index < FONT_5X7_GLYPH_COUNT; ++index) {
        if (FONT_5X7_GLYPHS[index].character == character) {
            return FONT_5X7_GLYPHS[index].columns;
        }
    }
    return NULL;
}

void oled_display_clear(void)
{
    memset(s_pixels, 0, sizeof(s_pixels));
}

void oled_display_set_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) {
        return;
    }
    s_pixels[y * WIDTH + x] = on;
}

static void draw_glyph(int x, int y, char character, int scale)
{
    const uint8_t *columns = glyph(character);
    if (columns == NULL) {
        return;
    }
    for (int column = 0; column < FONT_WIDTH; ++column) {
        for (int row = 0; row < FONT_HEIGHT; ++row) {
            if ((columns[column] & (1U << row)) == 0U) {
                continue;
            }
            for (int by = 0; by < scale; ++by) {
                for (int bx = 0; bx < scale; ++bx) {
                    oled_display_set_pixel(x + column * scale + bx,
                                           y + row * scale + by, true);
                }
            }
        }
    }
}

void oled_display_draw_text(int x, int y, const char *text)
{
    while (*text != 0) {
        draw_glyph(x, y, *text, 1);
        x += FONT_ADVANCE;
        ++text;
    }
}

void oled_display_draw_text_scaled(int x, int y, const char *text, int scale)
{
    if (scale < 1) {
        scale = 1;
    }
    while (*text != 0) {
        draw_glyph(x, y, *text, scale);
        x += FONT_ADVANCE * scale;
        ++text;
    }
}

int oled_display_text_width(const char *text, int scale)
{
    if (scale < 1) {
        scale = 1;
    }
    const int characters = (int)strlen(text);
    if (characters == 0) {
        return 0;
    }
    return characters * FONT_ADVANCE * scale - scale;
}

void oled_display_draw_rect(int x, int y, int width, int height, bool on)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    for (int px = x; px < x + width; ++px) {
        oled_display_set_pixel(px, y, on);
        oled_display_set_pixel(px, y + height - 1, on);
    }
    for (int py = y; py < y + height; ++py) {
        oled_display_set_pixel(x, py, on);
        oled_display_set_pixel(x + width - 1, py, on);
    }
}

void oled_display_fill_rect(int x, int y, int width, int height, bool on)
{
    for (int py = y; py < y + height; ++py) {
        for (int px = x; px < x + width; ++px) {
            oled_display_set_pixel(px, py, on);
        }
    }
}

esp_err_t oled_display_present(void)
{
    for (int y = 0; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH; ++x) {
            putchar(s_pixels[y * WIDTH + x] ? '#' : '.');
        }
        putchar('\n');
    }
    return ESP_OK;
}

int main(void)
{
    const time_animation_view_t view = {
        .year = 2026,
        .month = 8,
        .day = 26,
        .hour = 14,
        .minute = 51,
        .second = 7,
        .clock_synced = true,
    };

    /* A third of the way through the screen's life, so the bar is visible as
     * a bar rather than as a full or empty line. */
    return time_animation_render(&view, 5000000, 15000000) == ESP_OK ? 0 : 1;
}
