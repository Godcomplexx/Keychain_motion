#include "round_motion.h"

#include <stdint.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#define MPU_ADDR_LOW  0x68
#define MPU_ADDR_HIGH 0x69
#define MPU_WHO_AM_I  0x75
#define MPU_PWR_MGMT_1 0x6B
#define MPU_ACCEL_XOUT_H 0x3B
#define I2C_HALF_PERIOD_US 5

static const char *TAG = "round_motion";
static uint8_t s_address;

/* An open-drain GPIO drives low and releases high; the pull-ups make I2C safe. */
static void set_sda(int level)
{
    gpio_set_level(ROUND_MPU_SDA_GPIO, level);
    esp_rom_delay_us(I2C_HALF_PERIOD_US);
}

static bool set_scl_high(void)
{
    gpio_set_level(ROUND_MPU_SCL_GPIO, 1);
    for (unsigned int i = 0; i < 100; ++i) {
        if (gpio_get_level(ROUND_MPU_SCL_GPIO)) {
            esp_rom_delay_us(I2C_HALF_PERIOD_US);
            return true;
        }
        esp_rom_delay_us(1);
    }
    return false; /* A stuck-low clock must not hang the main loop. */
}

static void set_scl_low(void)
{
    gpio_set_level(ROUND_MPU_SCL_GPIO, 0);
    esp_rom_delay_us(I2C_HALF_PERIOD_US);
}

static bool start_condition(void)
{
    set_sda(1);
    if (!set_scl_high() || !gpio_get_level(ROUND_MPU_SDA_GPIO)) {
        return false;
    }
    set_sda(0);
    set_scl_low();
    return true;
}

static void stop_condition(void)
{
    set_sda(0);
    (void)set_scl_high();
    set_sda(1); /* Release SDA even if a disconnected clock stays low. */
}

static bool write_byte(uint8_t value)
{
    for (unsigned int bit = 0; bit < 8; ++bit) {
        set_sda((value & 0x80U) != 0);
        if (!set_scl_high()) {
            return false;
        }
        set_scl_low();
        value <<= 1;
    }
    set_sda(1); /* The slave owns SDA for the ninth, acknowledge clock. */
    if (!set_scl_high()) {
        return false;
    }
    const bool ack = gpio_get_level(ROUND_MPU_SDA_GPIO) == 0;
    set_scl_low();
    return ack;
}

static bool read_byte(bool acknowledge, uint8_t *result)
{
    uint8_t value = 0;
    set_sda(1); /* Release SDA while the MPU sends its eight bits. */
    for (unsigned int bit = 0; bit < 8; ++bit) {
        value <<= 1;
        if (!set_scl_high()) {
            return false;
        }
        value |= gpio_get_level(ROUND_MPU_SDA_GPIO) ? 1U : 0U;
        set_scl_low();
    }
    set_sda(acknowledge ? 0 : 1);
    if (!set_scl_high()) {
        return false;
    }
    set_scl_low();
    set_sda(1);
    *result = value;
    return true;
}

/* I2C bus clear. Before this driver owns GPIO20/21 they serve UART0 during
 * boot, and that activity leaves the MPU mid-transfer: on hardware the first
 * probe after reset NACKed while the next one succeeded. Nine clocks let the
 * MPU finish any byte it thinks is in progress; the STOP returns it to idle. */
static void bus_clear(void)
{
    set_sda(1);
    for (unsigned int i = 0; i < 9; ++i) {
        set_scl_low();
        (void)set_scl_high();
    }
    set_scl_low();
    stop_condition();
}

static bool read_registers(uint8_t address, uint8_t reg,
                           uint8_t *bytes, unsigned int length)
{
    bool ok = start_condition();
    if (ok) { ok = write_byte((uint8_t)(address << 1)); }
    if (ok) { ok = write_byte(reg); }
    if (ok) { ok = start_condition(); } /* Repeated START keeps the register selected. */
    if (ok) { ok = write_byte((uint8_t)((address << 1) | 1U)); }
    for (unsigned int i = 0; ok && i < length; ++i) {
        ok = read_byte(i + 1U < length, &bytes[i]);
    }
    stop_condition();
    return ok;
}

static bool write_register(uint8_t address, uint8_t reg, uint8_t value)
{
    bool ok = start_condition();
    if (ok) { ok = write_byte((uint8_t)(address << 1)); }
    if (ok) { ok = write_byte(reg); }
    if (ok) { ok = write_byte(value); }
    stop_condition();
    return ok;
}

bool round_motion_init(void)
{
    const gpio_config_t lines = {
        .pin_bit_mask = (1ULL << ROUND_MPU_SDA_GPIO) |
                        (1ULL << ROUND_MPU_SCL_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    if (gpio_config(&lines) != ESP_OK) {
        return false;
    }
    set_sda(1);
    gpio_set_level(ROUND_MPU_SCL_GPIO, 1);
    bus_clear(); /* Without it the first probe after boot is lost. */

    /* Probe both AD0 variants before enabling the accelerometer. */
    for (uint8_t address = MPU_ADDR_LOW; address <= MPU_ADDR_HIGH; ++address) {
        uint8_t id = 0;
        if (read_registers(address, MPU_WHO_AM_I, &id, 1) &&
            ((id & 0x7EU) == 0x68U || (id & 0x7EU) == 0x70U) &&
            write_register(address, MPU_PWR_MGMT_1, 0x00)) {
            s_address = address;
            ESP_LOGI(TAG, "MPU-6050 found at 0x%02x, WHO_AM_I=0x%02x", address, id);
            return true;
        }
    }
    ESP_LOGW(TAG, "No MPU-6050 on GPIO%d/%d; eyes stay neutral",
             ROUND_MPU_SDA_GPIO, ROUND_MPU_SCL_GPIO);
    return false;
}

/* One screen axis from the chip axes: 1..3 picks X/Y/Z, negative flips it. */
static float along(const round_motion_sample_t *raw, int axis)
{
    const int index = axis < 0 ? -axis : axis;
    const float value = index == 1 ? raw->x_g : (index == 2 ? raw->y_g : raw->z_g);
    return axis < 0 ? -value : value;
}

bool round_motion_read(round_motion_sample_t *sample)
{
    round_motion_sample_t raw;
    if (sample == NULL || !round_motion_read_raw(&raw)) {
        return false;
    }
    sample->x_g = along(&raw, ROUND_MPU_AXIS_RIGHT);
    sample->y_g = along(&raw, ROUND_MPU_AXIS_UP);
    sample->z_g = along(&raw, ROUND_MPU_AXIS_OUT);
    return true;
}

bool round_motion_read_raw(round_motion_sample_t *sample)
{
    if (s_address == 0 || sample == NULL) {
        return false;
    }
    uint8_t raw[6];
    if (!read_registers(s_address, MPU_ACCEL_XOUT_H, raw, sizeof(raw))) {
        return false;
    }
    /* Default ±2 g range has 16384 raw counts per g. */
    sample->x_g = (int16_t)((uint16_t)raw[0] << 8 | raw[1]) / 16384.0f;
    sample->y_g = (int16_t)((uint16_t)raw[2] << 8 | raw[3]) / 16384.0f;
    sample->z_g = (int16_t)((uint16_t)raw[4] << 8 | raw[5]) / 16384.0f;
    return true;
}
