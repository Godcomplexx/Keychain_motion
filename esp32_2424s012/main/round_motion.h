#ifndef ROUND_MOTION_H
#define ROUND_MOTION_H

#include <stdbool.h>

typedef struct {
    float x_g;
    float y_g;
    float z_g;
} round_motion_sample_t;

/* Uses GPIO I2C because the ESP32-C3 hardware I2C port serves the display touch. */
bool round_motion_init(void);
/* Screen axes: x to the right, y to the top edge, z out toward the viewer. */
bool round_motion_read(round_motion_sample_t *sample);
/* The chip's own X/Y/Z, before board_config.h maps them to the screen. */
bool round_motion_read_raw(round_motion_sample_t *sample);

#endif
