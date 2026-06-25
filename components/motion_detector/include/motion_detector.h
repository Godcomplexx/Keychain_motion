#ifndef MOTION_DETECTOR_H
#define MOTION_DETECTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "adxl345.h"

typedef struct {
    bool movement_detected;
    bool shake_detected;
    bool stillness_timeout;
    int raw_delta;
    int64_t stillness_us;
} motion_detector_result_t;

typedef struct {
    int movement_delta_threshold;
    int shake_delta_threshold;
    int64_t stillness_timeout_us;
    bool has_previous_raw_data;
    int64_t last_movement_us;
    adxl345_raw_data_t previous_raw_data;
} motion_detector_t;

void motion_detector_init(motion_detector_t *detector,
                          int64_t now_us,
                          int movement_delta_threshold,
                          int shake_delta_threshold,
                          int64_t stillness_timeout_us);

motion_detector_result_t motion_detector_update(motion_detector_t *detector,
                                                const adxl345_raw_data_t *data,
                                                int64_t now_us);

bool motion_detector_has_recent_movement(const motion_detector_t *detector,
                                         int64_t now_us);

#endif /* MOTION_DETECTOR_H */
