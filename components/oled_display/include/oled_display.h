#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "esp_err.h"

/* Initialize the connected OLED using the SSD1306-compatible test driver. */
esp_err_t oled_display_init(void);

/* Clear the framebuffer and show the Milestone 3 startup text. */
esp_err_t oled_display_show_startup(void);

/* Release display resources before the shared I2C bus is deleted. */
esp_err_t oled_display_deinit(void);

#endif /* OLED_DISPLAY_H */
