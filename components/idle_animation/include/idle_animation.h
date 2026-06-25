#ifndef IDLE_ANIMATION_H
#define IDLE_ANIMATION_H

#include <stdint.h>

#include "esp_err.h"

/* Return the number of frames exported from the Aseprite sprite sheet. */
uint8_t idle_animation_get_frame_count(void);

/* Draw one idle frame to the OLED. The frame index wraps automatically. */
esp_err_t idle_animation_render_frame(uint8_t frame_index);

#endif /* IDLE_ANIMATION_H */
