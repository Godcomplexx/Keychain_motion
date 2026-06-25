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

/* Send the complete private framebuffer to the OLED. */
esp_err_t oled_display_present(void);

/* Set SSD1306 contrast. Lower values reduce brightness and power draw. */
esp_err_t oled_display_set_contrast(uint8_t contrast);

/* Release display resources before the shared I2C bus is deleted. */
esp_err_t oled_display_deinit(void);

#endif /* OLED_DISPLAY_H */
