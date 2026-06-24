#ifndef FLUID_ANIMATION_H
#define FLUID_ANIMATION_H

#include "esp_err.h"

/* Reset all 96 particles to a centered 12 by 8 test grid. */
void fluid_animation_reset(void);

/*
 * Advance a cellular sand grid toward tilt and draw all occupied cells.
 * tilt_x and tilt_y use the normalized range -1.0 to 1.0.
 */
esp_err_t fluid_animation_render(float tilt_x, float tilt_y,
                                 float delta_seconds);

#endif /* FLUID_ANIMATION_H */
