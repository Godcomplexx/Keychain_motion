#include "adxl345.h"

#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_log.h"

#include "board_config.h"
#include "i2c_bus.h"

#define ADXL345_REGISTER_DEVID       0x00
#define ADXL345_REGISTER_BW_RATE     0x2C
#define ADXL345_REGISTER_POWER_CTL   0x2D
#define ADXL345_REGISTER_DATA_FORMAT 0x31
#define ADXL345_REGISTER_DATAX0      0x32

#define ADXL345_EXPECTED_DEVICE_ID   0xE5
#define ADXL345_RATE_100_HZ           0x0A
#define ADXL345_FULL_RESOLUTION_2G    0x08
#define ADXL345_MEASUREMENT_MODE      0x08
#define ADXL345_TRANSACTION_TIMEOUT_MS 100
#define ADXL345_COUNTS_PER_G          256.0f

static const char *TAG = "adxl345";
static i2c_master_dev_handle_t s_device_handle;

static esp_err_t adxl345_write_register(uint8_t register_address,
                                        uint8_t value)
{
    const uint8_t transaction[] = {register_address, value};
    return i2c_master_transmit(s_device_handle, transaction,
                               sizeof(transaction),
                               ADXL345_TRANSACTION_TIMEOUT_MS);
}

static esp_err_t adxl345_read_registers(uint8_t first_register,
                                        uint8_t *data, size_t data_size)
{
    return i2c_master_transmit_receive(s_device_handle,
                                       &first_register, 1,
                                       data, data_size,
                                       ADXL345_TRANSACTION_TIMEOUT_MS);
}

static int16_t adxl345_decode_axis(uint8_t low_byte, uint8_t high_byte)
{
    const uint16_t unsigned_value =
        ((uint16_t)high_byte << 8) | (uint16_t)low_byte;
    return (int16_t)unsigned_value;
}

esp_err_t adxl345_init(void)
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
        .device_address = BOARD_ADXL345_I2C_ADDRESS,
        .scl_speed_hz = BOARD_I2C_FREQUENCY_HZ,
    };

    esp_err_t err = i2c_master_bus_add_device(bus_handle, &device_config,
                                               &s_device_handle);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t device_id = 0;
    err = adxl345_read_registers(ADXL345_REGISTER_DEVID, &device_id, 1);
    if (err != ESP_OK) {
        goto init_failed;
    }

    if (device_id != ADXL345_EXPECTED_DEVICE_ID) {
        ESP_LOGE(TAG, "Unexpected device ID: 0x%02X", device_id);
        err = ESP_ERR_INVALID_RESPONSE;
        goto init_failed;
    }

    err = adxl345_write_register(ADXL345_REGISTER_DATA_FORMAT,
                                  ADXL345_FULL_RESOLUTION_2G);
    if (err != ESP_OK) {
        goto init_failed;
    }

    err = adxl345_write_register(ADXL345_REGISTER_BW_RATE,
                                  ADXL345_RATE_100_HZ);
    if (err != ESP_OK) {
        goto init_failed;
    }

    err = adxl345_write_register(ADXL345_REGISTER_POWER_CTL,
                                  ADXL345_MEASUREMENT_MODE);
    if (err != ESP_OK) {
        goto init_failed;
    }

    ESP_LOGI(TAG, "ADXL345 initialized: address=0x%02X, DEVID=0x%02X",
             BOARD_ADXL345_I2C_ADDRESS, device_id);
    return ESP_OK;

init_failed:
    {
        esp_err_t cleanup_err = i2c_master_bus_rm_device(s_device_handle);
        if (cleanup_err != ESP_OK) {
            ESP_LOGW(TAG, "Device cleanup failed: %s",
                     esp_err_to_name(cleanup_err));
        }
    }
    s_device_handle = NULL;
    return err;
}

esp_err_t adxl345_read_raw(adxl345_raw_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_device_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t raw_bytes[6];
    esp_err_t err = adxl345_read_registers(ADXL345_REGISTER_DATAX0,
                                            raw_bytes,
                                            sizeof(raw_bytes));
    if (err != ESP_OK) {
        return err;
    }

    data->x = adxl345_decode_axis(raw_bytes[0], raw_bytes[1]);
    data->y = adxl345_decode_axis(raw_bytes[2], raw_bytes[3]);
    data->z = adxl345_decode_axis(raw_bytes[4], raw_bytes[5]);
    return ESP_OK;
}

float adxl345_raw_to_g(int16_t raw_value)
{
    return (float)raw_value / ADXL345_COUNTS_PER_G;
}

esp_err_t adxl345_deinit(void)
{
    if (s_device_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = i2c_master_bus_rm_device(s_device_handle);
    if (err == ESP_OK) {
        s_device_handle = NULL;
        ESP_LOGI(TAG, "ADXL345 deinitialized");
    }
    return err;
}
