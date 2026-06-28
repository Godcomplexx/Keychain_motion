#include "i2c_bus.h"

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_log.h"

#include "board_config.h"

#define I2C_SCAN_FIRST_ADDRESS 0x08
#define I2C_SCAN_LAST_ADDRESS  0x77
#define I2C_PROBE_TIMEOUT_MS    50

static const char *TAG = "i2c_bus";
static i2c_master_bus_handle_t s_bus_handle;

esp_err_t i2c_bus_init(void)
{
    if (s_bus_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const i2c_master_bus_config_t bus_config = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "I2C bus initialized: SDA=%d, SCL=%d",
                 BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO);
    }

    return err;
}

esp_err_t i2c_bus_scan(void)
{
    if (s_bus_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t device_count = 0;
    ESP_LOGW(TAG, "Starting I2C scan");

    for (uint16_t address = I2C_SCAN_FIRST_ADDRESS;
         address <= I2C_SCAN_LAST_ADDRESS;
         ++address) {
        esp_err_t err = i2c_master_probe(s_bus_handle, address,
                                         I2C_PROBE_TIMEOUT_MS);

        if (err == ESP_OK) {
            ESP_LOGW(TAG, "Found I2C device at address 0x%02X",
                     (unsigned int)address);
            ++device_count;
        } else if (err != ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Probe failed at address 0x%02X: %s",
                     (unsigned int)address, esp_err_to_name(err));
            return err;
        }
    }

    if (device_count == 0) {
        ESP_LOGW(TAG, "I2C scan complete: no devices found");
    } else {
        ESP_LOGW(TAG, "I2C scan complete: %u device(s) found",
                 (unsigned int)device_count);
    }

    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_get_handle(void)
{
    /* The bus component remains the owner; callers only borrow this handle. */
    return s_bus_handle;
}

esp_err_t i2c_bus_deinit(void)
{
    if (s_bus_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = i2c_del_master_bus(s_bus_handle);
    if (err == ESP_OK) {
        s_bus_handle = NULL;
        ESP_LOGI(TAG, "I2C bus deinitialized");
    }

    return err;
}
