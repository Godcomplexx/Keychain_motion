#include "mpu6050.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

#include "board_config.h"
#include "esp_log.h"
#include "mpu6050_optional.h"

#define MPU6050_REG_SMPLRT_DIV       0x19
#define MPU6050_REG_CONFIG           0x1A
#define MPU6050_REG_ACCEL_CONFIG     0x1C
#define MPU6050_REG_MOT_THR          0x1F
#define MPU6050_REG_MOT_DUR          0x20
#define MPU6050_REG_INT_PIN_CFG      0x37
#define MPU6050_REG_INT_ENABLE       0x38
#define MPU6050_REG_INT_STATUS       0x3A
#define MPU6050_REG_ACCEL_XOUT_H     0x3B
#define MPU6050_REG_MOT_DETECT_CTRL  0x69
#define MPU6050_REG_PWR_MGMT_1       0x6B
#define MPU6050_REG_PWR_MGMT_2       0x6C
#define MPU6050_REG_WHO_AM_I         0x75

#define MPU6050_WHO_AM_I_VALUE       0x68
#define MPU6050_WHO_AM_I_MASK        0x7E
#define MPU6050_PWR_TEMP_DISABLED    0x08
#define MPU6050_PWR_CYCLE            0x20
#define MPU6050_GYROS_STANDBY        0x07
#define MPU6050_LP_WAKE_5_HZ         0x40
#define MPU6050_ACCEL_2G             0x00
#define MPU6050_ACCEL_HPF_5_HZ       0x01
#define MPU6050_DLPF_44_HZ           0x03
#define MPU6050_SAMPLE_DIV_100_HZ    9
#define MPU6050_MOTION_THRESHOLD_80_MG 40
#define MPU6050_MOTION_DURATION_MS   1
#define MPU6050_ACCEL_ON_DELAY_1_MS  0x10
#define MPU6050_MOTION_DECREMENT_1   0x01
#define MPU6050_INT_LATCHED_CLEAR_ON_READ 0x30
#define MPU6050_MOTION_INTERRUPT     0x40

#define MPU6050_NATIVE_COUNTS_PER_G  16384
#define MPU6050_APP_COUNTS_PER_G     256
#define MPU6050_NORMALIZATION_DIVISOR \
    (MPU6050_NATIVE_COUNTS_PER_G / MPU6050_APP_COUNTS_PER_G)

#define LIS2DW12_REG_WHO_AM_I         0x0F
#define LIS2DW12_REG_CTRL1            0x20
#define LIS2DW12_REG_CTRL2            0x21
#define LIS2DW12_REG_CTRL3            0x22
#define LIS2DW12_REG_CTRL4_INT1       0x23
#define LIS2DW12_REG_CTRL6            0x25
#define LIS2DW12_REG_OUT_X_L          0x28
#define LIS2DW12_REG_WAKE_UP_THS      0x34
#define LIS2DW12_REG_WAKE_UP_DUR      0x35
#define LIS2DW12_REG_ALL_INT_SRC      0x3B
#define LIS2DW12_WHO_AM_I_VALUE       0x44
#define LIS2DW12_CTRL1_100HZ_HP       0x54
#define LIS2DW12_CTRL1_12HZ5_LP       0x20
#define LIS2DW12_CTRL2_BDU_AUTOINC    0x0C
#define LIS2DW12_CTRL3_LATCHED_INT    0x10
#define LIS2DW12_CTRL4_WAKE_TO_INT1   0x20
#define LIS2DW12_WAKE_THRESHOLD       0x02
#define LIS2DW12_WAKE_DURATION        0x00

typedef enum {
    SENSOR_NONE = 0,
    SENSOR_LIS2DW12,
    SENSOR_MPU6050,
} sensor_kind_t;

static const char *TAG = "mpu6050";
static const struct device *const s_i2c = DEVICE_DT_GET(BOARD_I2C_NODE);
static sensor_kind_t s_sensor_kind;

static esp_err_t zephyr_result(int result)
{
    return result == 0 ? ESP_OK : ESP_FAIL;
}

static uint16_t active_address(void)
{
    return s_sensor_kind == SENSOR_LIS2DW12 ?
           BOARD_LIS2DW12_I2C_ADDRESS : BOARD_MPU6050_I2C_ADDRESS;
}

static esp_err_t write_register_at(uint16_t address,
                                   uint8_t register_address,
                                   uint8_t value)
{
    const uint8_t packet[] = {register_address, value};
    return zephyr_result(i2c_write(s_i2c, packet, sizeof(packet),
                                   address));
}

static esp_err_t read_registers_at(uint16_t address,
                                   uint8_t first_register,
                                   uint8_t *data,
                                   size_t data_size)
{
    return zephyr_result(i2c_write_read(s_i2c,
                                        address,
                                        &first_register,
                                        sizeof(first_register),
                                        data,
                                        data_size));
}

static esp_err_t write_register(uint8_t register_address, uint8_t value)
{
    return write_register_at(active_address(), register_address, value);
}

static esp_err_t read_registers(uint8_t first_register,
                                uint8_t *data,
                                size_t data_size)
{
    return read_registers_at(active_address(), first_register,
                             data, data_size);
}

static int16_t normalized_axis(uint8_t high, uint8_t low)
{
    const int16_t native_value = (int16_t)(((uint16_t)high << 8) | low);
    return (int16_t)(native_value / MPU6050_NORMALIZATION_DIVISOR);
}

bool mpu6050_is_available(void)
{
    return s_sensor_kind != SENSOR_NONE;
}

esp_err_t mpu6050_clear_interrupts(void)
{
    if (s_sensor_kind == SENSOR_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t interrupt_status = 0;
    const uint8_t status_register = s_sensor_kind == SENSOR_LIS2DW12 ?
                                    LIS2DW12_REG_ALL_INT_SRC :
                                    MPU6050_REG_INT_STATUS;
    return read_registers(status_register, &interrupt_status, 1);
}

esp_err_t mpu6050_set_active_mode(void)
{
    if (s_sensor_kind == SENSOR_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_sensor_kind == SENSOR_LIS2DW12) {
        esp_err_t err = write_register(LIS2DW12_REG_CTRL2,
                                       LIS2DW12_CTRL2_BDU_AUTOINC);
        if (err == ESP_OK) err = write_register(LIS2DW12_REG_CTRL6, 0x00);
        if (err == ESP_OK) err = write_register(LIS2DW12_REG_CTRL1,
                                                 LIS2DW12_CTRL1_100HZ_HP);
        return err;
    }

    esp_err_t err = write_register(MPU6050_REG_INT_ENABLE, 0);
    if (err == ESP_OK) err = write_register(MPU6050_REG_PWR_MGMT_1,
                                             MPU6050_PWR_TEMP_DISABLED);
    if (err == ESP_OK) err = write_register(MPU6050_REG_PWR_MGMT_2,
                                             MPU6050_GYROS_STANDBY);
    if (err == ESP_OK) err = write_register(MPU6050_REG_CONFIG,
                                             MPU6050_DLPF_44_HZ);
    if (err == ESP_OK) err = write_register(MPU6050_REG_SMPLRT_DIV,
                                             MPU6050_SAMPLE_DIV_100_HZ);
    if (err == ESP_OK) err = write_register(MPU6050_REG_ACCEL_CONFIG,
                                             MPU6050_ACCEL_2G);
    if (err == ESP_OK) err = mpu6050_clear_interrupts();
    return err;
}

esp_err_t mpu6050_set_motion_wakeup_mode(void)
{
    if (s_sensor_kind == SENSOR_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_sensor_kind == SENSOR_LIS2DW12) {
        esp_err_t err = write_register(LIS2DW12_REG_CTRL1,
                                       LIS2DW12_CTRL1_12HZ5_LP);
        if (err == ESP_OK) err = write_register(LIS2DW12_REG_CTRL3,
                                                 LIS2DW12_CTRL3_LATCHED_INT);
        if (err == ESP_OK) err = write_register(
            LIS2DW12_REG_WAKE_UP_THS, LIS2DW12_WAKE_THRESHOLD);
        if (err == ESP_OK) err = write_register(
            LIS2DW12_REG_WAKE_UP_DUR, LIS2DW12_WAKE_DURATION);
        if (err == ESP_OK) err = write_register(
            LIS2DW12_REG_CTRL4_INT1, LIS2DW12_CTRL4_WAKE_TO_INT1);
        return err;
    }

    esp_err_t err = write_register(MPU6050_REG_INT_ENABLE, 0);
    if (err == ESP_OK) err = write_register(MPU6050_REG_PWR_MGMT_1,
                                             MPU6050_PWR_TEMP_DISABLED);
    if (err == ESP_OK) err = write_register(
        MPU6050_REG_PWR_MGMT_2,
        MPU6050_LP_WAKE_5_HZ | MPU6050_GYROS_STANDBY);
    if (err == ESP_OK) err = write_register(MPU6050_REG_ACCEL_CONFIG,
                                             MPU6050_ACCEL_HPF_5_HZ);
    if (err == ESP_OK) err = write_register(MPU6050_REG_MOT_THR,
                                             MPU6050_MOTION_THRESHOLD_80_MG);
    if (err == ESP_OK) err = write_register(MPU6050_REG_MOT_DUR,
                                             MPU6050_MOTION_DURATION_MS);
    if (err == ESP_OK) err = write_register(
        MPU6050_REG_MOT_DETECT_CTRL,
        MPU6050_ACCEL_ON_DELAY_1_MS | MPU6050_MOTION_DECREMENT_1);
    if (err == ESP_OK) err = write_register(
        MPU6050_REG_INT_PIN_CFG, MPU6050_INT_LATCHED_CLEAR_ON_READ);
    if (err == ESP_OK) err = mpu6050_clear_interrupts();
    if (err == ESP_OK) err = write_register(MPU6050_REG_INT_ENABLE,
                                             MPU6050_MOTION_INTERRUPT);
    if (err == ESP_OK) err = write_register(
        MPU6050_REG_PWR_MGMT_1,
        MPU6050_PWR_CYCLE | MPU6050_PWR_TEMP_DISABLED);
    return err;
}

esp_err_t mpu6050_init(void)
{
    if (s_sensor_kind != SENSOR_NONE) {
        return ESP_OK;
    }
    if (!device_is_ready(s_i2c)) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t who_am_i = 0;
    esp_err_t err = read_registers_at(BOARD_LIS2DW12_I2C_ADDRESS,
                                      LIS2DW12_REG_WHO_AM_I,
                                      &who_am_i, 1);
    if (err == ESP_OK && who_am_i == LIS2DW12_WHO_AM_I_VALUE) {
        s_sensor_kind = SENSOR_LIS2DW12;
        err = mpu6050_set_active_mode();
        if (err != ESP_OK) {
            s_sensor_kind = SENSOR_NONE;
            return err;
        }
        ESP_LOGI(TAG, "LIS2DW12 ready at 0x%02X, WHO_AM_I=0x%02X",
                 BOARD_LIS2DW12_I2C_ADDRESS, who_am_i);
        return ESP_OK;
    }

    err = read_registers_at(BOARD_MPU6050_I2C_ADDRESS,
                            MPU6050_REG_WHO_AM_I, &who_am_i, 1);
    if (err != ESP_OK) return ESP_ERR_NOT_FOUND;
    const uint8_t masked = who_am_i & MPU6050_WHO_AM_I_MASK;
    if (masked != MPU6050_WHO_AM_I_VALUE && masked != 0x70) {
        ESP_LOGE(TAG, "Unexpected WHO_AM_I: 0x%02X", who_am_i);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Set the flag before configuration because register helpers require it. */
    s_sensor_kind = SENSOR_MPU6050;
    err = mpu6050_set_active_mode();
    if (err != ESP_OK) {
        s_sensor_kind = SENSOR_NONE;
        return err;
    }

    ESP_LOGI(TAG, "MPU-6050 ready at 0x%02X, WHO_AM_I=0x%02X",
             BOARD_MPU6050_I2C_ADDRESS, who_am_i);
    return ESP_OK;
}

esp_err_t mpu6050_read_accel(mpu6050_accel_data_t *data)
{
    if (s_sensor_kind == SENSOR_NONE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t bytes[6];
    const bool lis2dw12 = s_sensor_kind == SENSOR_LIS2DW12;
    const uint8_t first_register = lis2dw12 ? LIS2DW12_REG_OUT_X_L :
                                             MPU6050_REG_ACCEL_XOUT_H;
    esp_err_t err = read_registers(first_register,
                                   bytes, sizeof(bytes));
    if (err != ESP_OK) {
        s_sensor_kind = SENSOR_NONE;
        return err;
    }

    if (lis2dw12) {
        /* LIS2DW12 data is little-endian and left-aligned at +/-2 g. */
        data->x = normalized_axis(bytes[1], bytes[0]);
        data->y = normalized_axis(bytes[3], bytes[2]);
        data->z = normalized_axis(bytes[5], bytes[4]);
    } else {
        data->x = normalized_axis(bytes[0], bytes[1]);
        data->y = normalized_axis(bytes[2], bytes[3]);
        data->z = normalized_axis(bytes[4], bytes[5]);
    }
    return ESP_OK;
}

float mpu6050_accel_to_g(int16_t value)
{
    return (float)value / MPU6050_APP_COUNTS_PER_G;
}

esp_err_t mpu6050_deinit(void)
{
    if (s_sensor_kind == SENSOR_NONE) {
        return ESP_OK;
    }

    if (s_sensor_kind == SENSOR_LIS2DW12) {
        (void)write_register(LIS2DW12_REG_CTRL1, 0x00);
    } else {
        (void)write_register(MPU6050_REG_INT_ENABLE, 0);
        (void)write_register(MPU6050_REG_PWR_MGMT_1, 0x40);
    }
    s_sensor_kind = SENSOR_NONE;
    return ESP_OK;
}
