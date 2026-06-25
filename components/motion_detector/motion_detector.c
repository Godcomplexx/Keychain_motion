#include "motion_detector.h"

#include <stddef.h>

static int raw_axis_delta(int16_t current, int16_t previous)
{
    const int delta = (int)current - (int)previous;
    return delta < 0 ? -delta : delta;
}

static int raw_motion_delta(const adxl345_raw_data_t *current,
                            const adxl345_raw_data_t *previous)
{
    return raw_axis_delta(current->x, previous->x) +
           raw_axis_delta(current->y, previous->y) +
           raw_axis_delta(current->z, previous->z);
}

void motion_detector_init(motion_detector_t *detector,
                          int64_t now_us,
                          int movement_delta_threshold,
                          int shake_delta_threshold,
                          int64_t stillness_timeout_us)
{
    if (detector == NULL) {
        return;
    }

    detector->movement_delta_threshold = movement_delta_threshold;
    detector->shake_delta_threshold = shake_delta_threshold;
    detector->stillness_timeout_us = stillness_timeout_us;
    detector->has_previous_raw_data = false;
    detector->last_movement_us = now_us;
    detector->previous_raw_data = (adxl345_raw_data_t){0};
}

motion_detector_result_t motion_detector_update(motion_detector_t *detector,
                                                const adxl345_raw_data_t *data,
                                                int64_t now_us)
{
    motion_detector_result_t result = {0};

    if (detector == NULL || data == NULL) {
        return result;
    }

    if (!detector->has_previous_raw_data) {
        /* The first sample is only a baseline for later deltas. */
        detector->previous_raw_data = *data;
        detector->has_previous_raw_data = true;
        detector->last_movement_us = now_us;
        return result;
    }

    result.raw_delta = raw_motion_delta(data, &detector->previous_raw_data);
    result.movement_detected =
        result.raw_delta > detector->movement_delta_threshold;
    result.shake_detected =
        result.raw_delta > detector->shake_delta_threshold;

    if (result.movement_detected) {
        detector->last_movement_us = now_us;
    }

    result.stillness_us = now_us - detector->last_movement_us;
    result.stillness_timeout =
        result.stillness_us >= detector->stillness_timeout_us;

    detector->previous_raw_data = *data;
    return result;
}

bool motion_detector_has_recent_movement(const motion_detector_t *detector,
                                         int64_t now_us)
{
    if (detector == NULL) {
        return true;
    }

    return now_us - detector->last_movement_us <
           detector->stillness_timeout_us;
}
