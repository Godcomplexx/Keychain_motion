#include "motion_detector.h"

#include <stddef.h>

#define SHAKE_PEAK_REFRACTORY_US 250000

static int raw_axis_delta(int16_t current, int16_t previous)
{
    const int delta = (int)current - (int)previous;
    return delta < 0 ? -delta : delta;
}

static int raw_motion_delta(const mpu6050_accel_data_t *current,
                            const mpu6050_accel_data_t *previous)
{
    return raw_axis_delta(current->x, previous->x) +
           raw_axis_delta(current->y, previous->y) +
           raw_axis_delta(current->z, previous->z);
}

void motion_detector_init(motion_detector_t *detector,
                          int64_t now_us,
                          int movement_delta_threshold,
                          int shake_delta_threshold,
                          int shake_count_required,
                          int64_t shake_window_us,
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
    detector->previous_raw_data = (mpu6050_accel_data_t){0};

    /* At least one peak is always required, even if the caller passes 0. */
    detector->shake_count_required =
        shake_count_required > 0 ? shake_count_required : 1;
    detector->shake_window_us = shake_window_us;
    detector->shake_count = 0;
    detector->shake_window_start_us = now_us;
    detector->last_shake_peak_us = 0;
}

motion_detector_result_t motion_detector_update(motion_detector_t *detector,
                                                const mpu6050_accel_data_t *data,
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

    /*
     * Rendering and I2C limit the application loop to a few samples per
     * second. Requiring the signal to fall below the threshold between samples
     * loses quick MPU-6050 shake reversals, so use a short refractory interval
     * to separate peaks instead.
     */
    const bool above_shake_threshold =
        result.raw_delta > detector->shake_delta_threshold;
    if (above_shake_threshold &&
        (detector->last_shake_peak_us == 0 ||
         now_us - detector->last_shake_peak_us >=
             SHAKE_PEAK_REFRACTORY_US)) {
        /* Start a fresh window if there were no recent peaks or it expired. */
        if (detector->shake_count == 0 ||
            now_us - detector->shake_window_start_us >
                detector->shake_window_us) {
            detector->shake_count = 0;
            detector->shake_window_start_us = now_us;
        }

        detector->last_shake_peak_us = now_us;
        ++detector->shake_count;
        if (detector->shake_count >= detector->shake_count_required) {
            result.shake_detected = true;
            /* Require a brand new gesture before the next TIME trigger. */
            detector->shake_count = 0;
        }
    }

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
