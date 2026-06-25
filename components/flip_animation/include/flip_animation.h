#ifndef FLIP_ANIMATION_H
#define FLIP_ANIMATION_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint16_t particle_count;
    uint16_t grid_cell_count;
    uint8_t grid_width;
    uint8_t grid_height;
    size_t static_memory_bytes;
} flip_animation_stats_t;

/*
 * Prepare and validate the experimental FLIP data model.
 * This checkpoint does not render or replace the active cellular animation.
 */
esp_err_t flip_animation_prepare(flip_animation_stats_t *stats);

/* Return all FLIP particles to their known startup positions. */
void flip_animation_reset(void);

/* Advance the FLIP physics and send the resulting frame to the OLED. */
esp_err_t flip_animation_render(float tilt_x, float tilt_y,
                                float delta_seconds);

#endif /* FLIP_ANIMATION_H */
