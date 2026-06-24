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

#endif /* FLIP_ANIMATION_H */
