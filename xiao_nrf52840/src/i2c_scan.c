#include "i2c_scan.h"

#include <stdbool.h>
#include <stdint.h>

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
static unsigned int scan_once(const struct device *bus, const char *speed_name)
{
    unsigned int found = 0U;
    /* 0x00-0x07 and 0x78-0x7F are reserved by the I2C specification. */
    for (uint16_t address = 0x08U; address <= 0x77U; ++address) {
        /*
         * Probe both ways. An SSD1306 is a write-only controller and never
         * acknowledges a read, so scanning by reads alone reports the display
         * as missing while it is sitting there working. A zero-length write is
         * the probe that finds it; the read is kept because it distinguishes a
         * device that talks back from one that only listens.
         */
        const bool writes = i2c_write(bus, NULL, 0U, address) == 0;
        uint8_t discard = 0U;
        const bool reads = i2c_read(bus, &discard, 1U, address) == 0;

        if (!writes && !reads) {
            continue;
        }
        ++found;

        const char *name =
            (address == BOARD_OLED_I2C_ADDRESS) ? " (OLED)" :
            (address == BOARD_MPU6050_I2C_ADDRESS) ? " (MPU-6050)" :
            (address == BOARD_LIS2DW12_I2C_ADDRESS) ? " (LIS2DW12)" : "";
        ESP_LOGI(TAG, "%s: found device at 0x%02X%s [%s%s]", speed_name,
                 address, name, writes ? "write" : "", reads ? " read" : "");
    }
    ESP_LOGI(TAG, "%s: scan complete, %u device(s) found", speed_name, found);
    return found;
}

/*
 * Talk to the display the way its driver does, and report the exact error.
 *
 * An address scan uses synthetic transactions, so it can in principle miss a
 * device that only answers a real one. This sends an actual SSD1306 command
 * frame - a 0x00 control byte followed by NOP - which is precisely what the
 * driver's first write looks like. If this is NACKed, the part is not
 * answering, and no amount of driver configuration will change that.
 */
static void probe_oled_directly(const struct device *bus)
{
    const uint8_t command[] = {0x00U, 0xE3U};  /* control byte, then NOP */
    const int result = i2c_write(bus, command, sizeof(command),
                                 BOARD_OLED_I2C_ADDRESS);

    if (result == 0) {
        ESP_LOGI(TAG, "OLED at 0x%02X answered a real command write",
                 BOARD_OLED_I2C_ADDRESS);
    } else {
        ESP_LOGW(TAG, "OLED at 0x%02X did not answer a command write: %d "
                 "(-5 = no acknowledge from the bus)",
                 BOARD_OLED_I2C_ADDRESS, result);
    }
}

void i2c_scan_log(void)
{
    const struct device *const bus = DEVICE_DT_GET(BOARD_I2C_NODE);

    if (!device_is_ready(bus)) {
        ESP_LOGE(TAG, "I2C bus not ready");
        return;
    }

    /*
     * A slave interrupted mid-read can hold SDA low and lock the bus for
     * everyone. Clocking it out costs nothing when the bus is already idle.
     */
    const int recovered = i2c_recover_bus(bus);
    if (recovered != 0) {
        ESP_LOGW(TAG, "Bus recovery returned %d", recovered);
    }

    probe_oled_directly(bus);
    (void)scan_once(bus, "400 kHz");

    /*
     * Repeat slowly. Extra wiring adds bus capacitance, and a device that
     * cannot meet the rise time at 400 kHz will simply not acknowledge while
     * being perfectly healthy at 100 kHz. Finding something only in the second
     * pass means the bus is too fast, not that the device is broken.
     */
    const uint32_t slow = I2C_MODE_CONTROLLER | I2C_SPEED_SET(I2C_SPEED_STANDARD);
    const uint32_t fast = I2C_MODE_CONTROLLER | I2C_SPEED_SET(I2C_SPEED_FAST);

    if (i2c_configure(bus, slow) != 0) {
        ESP_LOGW(TAG, "Could not drop the bus to 100 kHz");
        return;
    }
    (void)scan_once(bus, "100 kHz");

    if (i2c_configure(bus, fast) != 0) {
        ESP_LOGE(TAG, "Could not restore the bus to 400 kHz");
    }
}
