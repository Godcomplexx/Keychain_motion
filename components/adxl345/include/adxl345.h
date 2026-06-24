#ifndef ADXL345_H
#define ADXL345_H

#include <stdint.h>

#include "esp_err.h"

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} adxl345_raw_data_t;

/* Verify the device ID and place the ADXL345 into measurement mode. */
esp_err_t adxl345_init(void);

/* Read the six little-endian acceleration data registers. */
esp_err_t adxl345_read_raw(adxl345_raw_data_t *data);

/* Convert full-resolution raw data to approximate acceleration in g. */
float adxl345_raw_to_g(int16_t raw_value);

/* Remove the ADXL345 device from the shared I2C bus. */
esp_err_t adxl345_deinit(void);

#endif /* ADXL345_H */
