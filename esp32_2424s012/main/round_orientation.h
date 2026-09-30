#ifndef ROUND_ORIENTATION_H
#define ROUND_ORIENTATION_H

#include <stdint.h>

#include "esp_err.h"
#include "round_motion.h"

/* A two-pose check that finds how the MPU is mounted behind the screen. */
void round_orientation_start(uint32_t now_ms);

/* Draw one frame. raw holds the chip's own axes, or NULL without a sensor. */
esp_err_t round_orientation_render(const round_motion_sample_t *raw,
                                   uint32_t now_ms);

#endif
