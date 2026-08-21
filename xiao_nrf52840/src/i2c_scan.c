#include "i2c_scan.h"

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

#include "board_config.h"
#include "esp_log.h"

static const char *TAG = "i2c_scan";

/*
 * Walk the bus and report what answers.
 *
 * Two devices share one bus here, and when one of them stops working the useful
 * question is whether it is still electrically present at all. A scan separates
 * "the wiring came loose" from "the driver is misconfigured", which guessing
 * from a failed init cannot do.
 */
void i2c_scan_log(void)
{
    const struct device *const bus = DEVICE_DT_GET(BOARD_I2C_NODE);

    if (!device_is_ready(bus)) {
        ESP_LOGE(TAG, "I2C bus not ready");
        return;
    }

    unsigned int found = 0U;
    /* 0x00-0x07 and 0x78-0x7F are reserved by the I2C specification. */
    for (uint16_t address = 0x08U; address <= 0x77U; ++address) {
        uint8_t discard = 0U;
        /* A zero-length write is the polite probe, but not every device
         * acknowledges one; a one-byte read is answered by all of ours. */
        if (i2c_read(bus, &discard, 1U, address) == 0) {
            ++found;
            const char *name =
                (address == BOARD_OLED_I2C_ADDRESS) ? " (OLED)" :
                (address == BOARD_MPU6050_I2C_ADDRESS) ? " (MPU-6050)" :
                (address == BOARD_LIS2DW12_I2C_ADDRESS) ? " (LIS2DW12)" : "";
            ESP_LOGI(TAG, "Found device at 0x%02X%s", address, name);
        }
    }
    ESP_LOGI(TAG, "Scan complete: %u device(s) found", found);
}
