#ifndef ROUND_DISPLAY_H
#define ROUND_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t round_display_init(void);
void round_display_touch_marker(uint16_t x, uint16_t y, bool visible);

#endif
