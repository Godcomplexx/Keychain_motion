#ifndef TIME_ANIMATION_H
#define TIME_ANIMATION_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint32_t steps_today;
    bool clock_synced;
} time_animation_view_t;

esp_err_t time_animation_render(const time_animation_view_t *view,
                                int64_t elapsed_us,
                                int64_t duration_us);

#endif /* TIME_ANIMATION_H */
