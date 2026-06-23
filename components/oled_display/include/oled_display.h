#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <stdbool.h>

#include "esp_err.h"

/* Initialize the connected OLED using the SSD1306-compatible test driver. */
esp_err_t oled_display_init(void);

/* Clear the framebuffer and show the Milestone 3 startup text. */
esp_err_t oled_display_show_startup(void);

/* Clear the private framebuffer without sending it to the OLED yet. */
void oled_display_clear(void);

/* Change one framebuffer pixel. Out-of-range coordinates are ignored. */
void oled_display_set_pixel(int x, int y, bool on);

/* Send the complete private framebuffer to the OLED. */
esp_err_t oled_display_present(void);

/* Release display resources before the shared I2C bus is deleted. */
esp_err_t oled_display_deinit(void);

#endif /* OLED_DISPLAY_H */
