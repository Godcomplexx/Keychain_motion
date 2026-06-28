#include "step_counter.h"

#include <stddef.h>

#define STEP_SIGNAL_THRESHOLD 2600
#define STEP_SIGNAL_RELEASE_THRESHOLD 900
#define STEP_MIN_INTERVAL_US 350000
#define FAST_FILTER_SHIFT 2
#define SLOW_FILTER_SHIFT 5

static int raw_magnitude_estimate(const mpu6050_accel_data_t *data)
{
    return (int)data->x * (int)data->x +
           (int)data->y * (int)data->y +
           (int)data->z * (int)data->z;
}

static void step_counter_reset_for_day(step_counter_t *counter,
                                       uint32_t date_key)
{
    counter->steps_today = 0;
    counter->date_key = date_key;
    counter->has_previous_sample = false;
    counter->peak_active = false;
    counter->last_step_us = 0;
    counter->fast_magnitude = 0;
    counter->slow_magnitude = 0;
}

void step_counter_init(step_counter_t *counter, uint32_t date_key)
{
    if (counter == NULL) {
        return;
    }

    step_counter_reset_for_day(counter, date_key);
}

void step_counter_update(step_counter_t *counter,
                         const mpu6050_accel_data_t *data,
                         uint32_t date_key,
                         int64_t now_us)
{
    if (counter == NULL || data == NULL) {
        return;
    }

    if (counter->date_key != date_key) {
        /* A new software-clock day starts a new daily step total. */
        step_counter_reset_for_day(counter, date_key);
    }

    const int magnitude = raw_magnitude_estimate(data);
    if (!counter->has_previous_sample) {
        counter->fast_magnitude = magnitude;
        counter->slow_magnitude = magnitude;
        counter->has_previous_sample = true;
        return;
    }

    /*
     * Pedometers usually work with filtered acceleration magnitude, not a
     * single raw delta. Fast minus slow filtering removes much of gravity and
     * keeps the walking pulse.
     */
    counter->fast_magnitude +=
        (magnitude - counter->fast_magnitude) >> FAST_FILTER_SHIFT;
    counter->slow_magnitude +=
        (magnitude - counter->slow_magnitude) >> SLOW_FILTER_SHIFT;

    const int step_signal =
        counter->fast_magnitude - counter->slow_magnitude;

    if (step_signal < STEP_SIGNAL_RELEASE_THRESHOLD) {
        counter->peak_active = false;
    }

    if (step_signal > STEP_SIGNAL_THRESHOLD &&
        !counter->peak_active &&
        now_us - counter->last_step_us >= STEP_MIN_INTERVAL_US) {
        ++counter->steps_today;
        counter->peak_active = true;
        counter->last_step_us = now_us;
    }
}

uint32_t step_counter_get_steps_today(const step_counter_t *counter)
{
    if (counter == NULL) {
        return 0;
    }

    return counter->steps_today;
}

uint32_t step_counter_get_date_key(const step_counter_t *counter)
{
    if (counter == NULL) {
        return 0;
    }

    return counter->date_key;
}
