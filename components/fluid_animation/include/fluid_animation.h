#ifndef FLUID_ANIMATION_H
#define FLUID_ANIMATION_H

#include "esp_err.h"

/* Reset the simulated liquid state to the center of the display. */
void fluid_animation_reset(void);

/*
 * Advance and draw one frame.
 * tilt_x and tilt_y use the normalized range -1.0 to 1.0.
 */
esp_err_t fluid_animation_render(float tilt_x, float tilt_y,
                                 float delta_seconds);

#endif /* FLUID_ANIMATION_H */
