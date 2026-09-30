#ifndef ROUND_TOUCH_H
#define ROUND_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t gesture;
} round_touch_point_t;

esp_err_t round_touch_init(void);
bool round_touch_read(round_touch_point_t *point);

#endif
