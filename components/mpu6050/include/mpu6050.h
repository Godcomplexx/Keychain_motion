#ifndef MPU6050_H
#define MPU6050_H

#include <stdint.h>

#include "esp_err.h"

/*
 * Acceleration is normalized to 256 counts/g. This preserves the thresholds
 * used by the animation, shake detector, and step counter.
 */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} mpu6050_accel_data_t;

esp_err_t mpu6050_init(void);
esp_err_t mpu6050_read_accel(mpu6050_accel_data_t *data);
float mpu6050_accel_to_g(int16_t value);

/* 100 Hz accelerometer mode with the gyroscope axes kept in standby. */
esp_err_t mpu6050_set_active_mode(void);

/* Low-power accelerometer cycling with latched motion interrupt on INT. */
esp_err_t mpu6050_set_motion_wakeup_mode(void);
esp_err_t mpu6050_clear_interrupts(void);

esp_err_t mpu6050_deinit(void);

#endif /* MPU6050_H */
