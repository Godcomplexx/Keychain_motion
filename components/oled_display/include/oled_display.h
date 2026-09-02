#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* Initialize the connected OLED using the SSD1306-compatible test driver. */
esp_err_t oled_display_init(void);

/* Clear the framebuffer and show the Milestone 3 startup text. */
esp_err_t oled_display_show_startup(void);

/* Clear the private framebuffer without sending it to the OLED yet. */
void oled_display_clear(void);

/* Change one framebuffer pixel. Out-of-range coordinates are ignored. */
void oled_display_set_pixel(int x, int y, bool on);

/* Draw small 5x7 text into the framebuffer without sending it yet. */
void oled_display_draw_text(int x, int y, const char *text);

/* The same 5x7 glyphs as blocks, for headline text like the clock. */
void oled_display_draw_text_scaled(int x, int y, const char *text, int scale);

/* Width in pixels the text above will occupy, for centring it. */
int oled_display_text_width(const char *text, int scale);

/* Outline and solid rectangles, without sending anything yet. */
void oled_display_draw_rect(int x, int y, int width, int height, bool on);

void oled_display_fill_rect(int x, int y, int width, int height, bool on);

/* Send the complete private framebuffer to the OLED. */
esp_err_t oled_display_present(void);

/* Set SSD1306 contrast. Lower values reduce brightness and power draw. */
esp_err_t oled_display_set_contrast(uint8_t contrast);

/* Turn the OLED panel output on or off without deleting its driver. */
esp_err_t oled_display_set_power(bool on);

/* Release display resources before the shared I2C bus is deleted. */
esp_err_t oled_display_deinit(void);

/*
 * True on hardware that can show real per-pixel colour (the bodge-wired round
 * GC9107 panel). False on the standard monochrome SSD1306 OLED. A renderer
 * that wants colour, or wants to draw across the panel's full physical shape
 * rather than the shared 128x64 canvas, should check this first.
 */
bool oled_display_is_color(void);

/*
 * The panel's own physical resolution - NOT the shared 128x64 logical canvas
 * every renderer draws into via oled_display_set_pixel(). Only meaningful to
 * code that deliberately wants the full panel, such as pet_face drawing into
 * the round display's actual 128x115 shape instead of the letterboxed canvas.
 */
int oled_display_panel_width(void);
int oled_display_panel_height(void);

/*
 * Set one pixel directly in panel-native coordinates (0,0 at the physical
 * top-left of the panel, ignoring the 128x64 canvas' centring) with a 16-bit
 * RGB565 colour. On the monochrome OLED this collapses to on/off - any
 * non-black colour lights the pixel - within its own 128x64 panel.
 */
void oled_display_set_pixel_rgb(int x, int y, uint16_t color565);

/* Same text drawing as oled_display_draw_text(), but in panel-native
 * coordinates and colour, for code that already used oled_display_set_pixel_rgb
 * for the rest of its frame. On the monochrome OLED the colour is ignored. */
void oled_display_draw_text_rgb(int x, int y, const char *text,
                                uint16_t color565);

/* Pack 8-bit channels into RGB565. */
static inline uint16_t oled_display_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8U) << 8) |
                      ((uint16_t)(g & 0xFCU) << 3) | (b >> 3));
}

#endif /* OLED_DISPLAY_H */
