#ifndef STEP_COUNTER_H
#define STEP_COUNTER_H

#include <stdbool.h>
#include <stdint.h>

#include "adxl345.h"

typedef struct {
    uint32_t steps_today;
    uint32_t date_key;
    int fast_magnitude;
    int slow_magnitude;
    bool has_previous_sample;
    bool peak_active;
    int64_t last_step_us;
} step_counter_t;

void step_counter_init(step_counter_t *counter, uint32_t date_key);

void step_counter_update(step_counter_t *counter,
                         const adxl345_raw_data_t *data,
                         uint32_t date_key,
                         int64_t now_us);

uint32_t step_counter_get_steps_today(const step_counter_t *counter);

uint32_t step_counter_get_date_key(const step_counter_t *counter);

#endif /* STEP_COUNTER_H */
