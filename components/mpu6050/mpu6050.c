#include "mpu6050.h"

#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_log.h"

#include "board_config.h"
#include "i2c_bus.h"

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
#define MPU6050_TRANSACTION_TIMEOUT_MS 100

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

static const char *TAG = "mpu6050";
static i2c_master_dev_handle_t s_device_handle;

static esp_err_t write_register(uint8_t register_address, uint8_t value)
{
    const uint8_t transaction[] = {register_address, value};
    return i2c_master_transmit(s_device_handle,
                               transaction,
                               sizeof(transaction),
                               MPU6050_TRANSACTION_TIMEOUT_MS);
}

static esp_err_t read_registers(uint8_t first_register,
                                uint8_t *data,
                                size_t data_size)
{
    return i2c_master_transmit_receive(s_device_handle,
                                       &first_register,
                                       1,
                                       data,
                                       data_size,
                                       MPU6050_TRANSACTION_TIMEOUT_MS);
}

static int16_t decode_axis(uint8_t high_byte, uint8_t low_byte)
{
    const uint16_t value =
        ((uint16_t)high_byte << 8) | (uint16_t)low_byte;
    return (int16_t)value;
}

static int16_t normalize_axis(int16_t native_value)
{
    return (int16_t)(native_value / MPU6050_NORMALIZATION_DIVISOR);
}

esp_err_t mpu6050_clear_interrupts(void)
{
    if (s_device_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t interrupt_status;
    return read_registers(MPU6050_REG_INT_STATUS,
                          &interrupt_status,
                          sizeof(interrupt_status));
}

esp_err_t mpu6050_set_active_mode(void)
{
    if (s_device_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = write_register(MPU6050_REG_INT_ENABLE, 0);
    if (err == ESP_OK) {
        err = write_register(MPU6050_REG_PWR_MGMT_1,
                             MPU6050_PWR_TEMP_DISABLED);
    }
    if (err == ESP_OK) {
        err = write_register(MPU6050_REG_PWR_MGMT_2,
                             MPU6050_GYROS_STANDBY);
    }
    if (err == ESP_OK) {
        err = write_register(MPU6050_REG_CONFIG, MPU6050_DLPF_44_HZ);
    }
    if (err == ESP_OK) {
        err = write_register(MPU6050_REG_SMPLRT_DIV,
                             MPU6050_SAMPLE_DIV_100_HZ);
    }
    if (err == ESP_OK) {
        err = write_register(MPU6050_REG_ACCEL_CONFIG,
                             MPU6050_ACCEL_2G);
    }
    if (err == ESP_OK) {
        err = mpu6050_clear_interrupts();
    }
    return err;
}

esp_err_t mpu6050_set_motion_wakeup_mode(void)
{
    if (s_device_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = write_register(MPU6050_REG_INT_ENABLE, 0);
    if (err == ESP_OK) {
        err = write_register(MPU6050_REG_PWR_MGMT_1,
                             MPU6050_PWR_TEMP_DISABLED);
    }
    if (err == ESP_OK) {
        err = write_register(MPU6050_REG_PWR_MGMT_2,
                             MPU6050_LP_WAKE_5_HZ |
                                 MPU6050_GYROS_STANDBY);
    }
    if (err == ESP_OK) {
        err = write_register(MPU6050_REG_ACCEL_CONFIG,
                             MPU6050_ACCEL_HPF_5_HZ);
    }
    if (err == ESP_OK) {
        err = write_register(MPU6050_REG_MOT_THR,
                             MPU6050_MOTION_THRESHOLD_80_MG);
    }
    if (err == ESP_OK) {
        err = write_register(MPU6050_REG_MOT_DUR,
                             MPU6050_MOTION_DURATION_MS);
    }
    if (err == ESP_OK) {
        err = write_register(MPU6050_REG_MOT_DETECT_CTRL,
                             MPU6050_ACCEL_ON_DELAY_1_MS |
                                 MPU6050_MOTION_DECREMENT_1);
    }
    if (err == ESP_OK) {
        err = write_register(MPU6050_REG_INT_PIN_CFG,
                             MPU6050_INT_LATCHED_CLEAR_ON_READ);
    }
    if (err == ESP_OK) {
        err = mpu6050_clear_interrupts();
    }
    if (err == ESP_OK) {
        err = write_register(MPU6050_REG_INT_ENABLE,
                             MPU6050_MOTION_INTERRUPT);
    }
    if (err == ESP_OK) {
        err = write_register(MPU6050_REG_PWR_MGMT_1,
                             MPU6050_PWR_CYCLE |
                                 MPU6050_PWR_TEMP_DISABLED);
    }
    return err;
}

esp_err_t mpu6050_init(void)
{
    if (s_device_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    i2c_master_bus_handle_t bus_handle = i2c_bus_get_handle();
    if (bus_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_MPU6050_I2C_ADDRESS,
        .scl_speed_hz = BOARD_I2C_FREQUENCY_HZ,
    };

    esp_err_t err = i2c_master_bus_add_device(bus_handle,
                                               &device_config,
                                               &s_device_handle);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t who_am_i = 0;
    err = read_registers(MPU6050_REG_WHO_AM_I, &who_am_i, 1);
    if (err != ESP_OK) {
        goto init_failed;
    }
    /* 0x68 = genuine MPU-6050; 0x70 = common MPU-6050 clone (GY-521 variants) */
    const uint8_t masked = who_am_i & MPU6050_WHO_AM_I_MASK;
    if (masked != MPU6050_WHO_AM_I_VALUE && masked != 0x70) {
        ESP_LOGE(TAG, "Unexpected WHO_AM_I: 0x%02X", who_am_i);
        err = ESP_ERR_INVALID_RESPONSE;
        goto init_failed;
    }

    err = mpu6050_set_active_mode();
    if (err != ESP_OK) {
        goto init_failed;
    }

    ESP_LOGI(TAG, "MPU-6050 initialized: address=0x%02X, WHO_AM_I=0x%02X",
             BOARD_MPU6050_I2C_ADDRESS, who_am_i);
    return ESP_OK;

init_failed:
    (void)i2c_master_bus_rm_device(s_device_handle);
    s_device_handle = NULL;
    return err;
}

esp_err_t mpu6050_read_accel(mpu6050_accel_data_t *data)
{
    if (s_device_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t raw_bytes[6];
    esp_err_t err = read_registers(MPU6050_REG_ACCEL_XOUT_H,
                                   raw_bytes,
                                   sizeof(raw_bytes));
    if (err != ESP_OK) {
        return err;
    }

    data->x = normalize_axis(decode_axis(raw_bytes[0], raw_bytes[1]));
    data->y = normalize_axis(decode_axis(raw_bytes[2], raw_bytes[3]));
    data->z = normalize_axis(decode_axis(raw_bytes[4], raw_bytes[5]));
    return ESP_OK;
}

float mpu6050_accel_to_g(int16_t value)
{
    return (float)value / MPU6050_APP_COUNTS_PER_G;
}

esp_err_t mpu6050_deinit(void)
{
    if (s_device_handle == NULL) {
        return ESP_OK;
    }

    (void)write_register(MPU6050_REG_INT_ENABLE, 0);
    (void)write_register(MPU6050_REG_PWR_MGMT_1, 0x40);
    esp_err_t err = i2c_master_bus_rm_device(s_device_handle);
    if (err == ESP_OK) {
        s_device_handle = NULL;
        ESP_LOGI(TAG, "MPU-6050 deinitialized");
    }
    return err;
}
