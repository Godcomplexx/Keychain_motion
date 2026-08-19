#ifndef XIAO_MPU6050_OPTIONAL_H
#define XIAO_MPU6050_OPTIONAL_H

#include <stdbool.h>

/* Lets the app remain interactive while the external sensor is disconnected. */
bool mpu6050_is_available(void);

#endif /* XIAO_MPU6050_OPTIONAL_H */
