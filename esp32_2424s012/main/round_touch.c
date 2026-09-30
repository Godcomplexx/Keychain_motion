#include "round_touch.h"

#include "board_config.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "round_touch";
static i2c_master_dev_handle_t s_touch;
static i2c_master_bus_handle_t s_bus;
static volatile bool s_irq_pending;
static volatile uint32_t s_irq_count;
static uint32_t s_last_poll_ms;
static uint32_t s_last_diag_ms;
static uint32_t s_ack_count;
static uint32_t s_read_count;
static uint32_t s_last_report_ms;
static uint16_t s_last_x;
static uint16_t s_last_y;

static void IRAM_ATTR touch_interrupt(void *argument)
{
    /* The ISR only records that a touch occurred; I2C runs in the main loop. */
    s_irq_pending = true;
    ++s_irq_count;
}

esp_err_t round_touch_init(void)
{
    const gpio_config_t reset = {
        .pin_bit_mask = 1ULL << ROUND_TOUCH_RST_GPIO,
        /* Input stays enabled so the boot diagnostic reads the real level. */
        .mode = GPIO_MODE_INPUT_OUTPUT,
    };
    const gpio_config_t interrupt_startup = {
        .pin_bit_mask = 1ULL << ROUND_TOUCH_INT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    const gpio_config_t interrupt = {
        .pin_bit_mask = 1ULL << ROUND_TOUCH_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE, /* Catch pulse polarity variants. */
    };
    esp_err_t err = gpio_config(&reset);
    if (err != ESP_OK) { return err; }
    err = gpio_config(&interrupt_startup);
    if (err != ESP_OK) { return err; }

    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = ROUND_TOUCH_SDA_GPIO,
        .scl_io_num = ROUND_TOUCH_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&bus_config, &s_bus);
    if (err != ESP_OK) { return err; }

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ROUND_TOUCH_I2C_ADDRESS,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(s_bus, &device_config, &s_touch);
    if (err != ESP_OK) { return err; }

    /* The board vendor's CST816D demo primes INT before releasing reset.
     * This is board initialization; after reset INT returns to an input. */
    err = gpio_set_level(ROUND_TOUCH_INT_GPIO, 1);
    if (err != ESP_OK) { return err; }
    esp_rom_delay_us(1000); /* One millisecond even with a 100 Hz RTOS tick. */
    err = gpio_set_level(ROUND_TOUCH_INT_GPIO, 0);
    if (err != ESP_OK) { return err; }
    esp_rom_delay_us(1000);
    err = gpio_set_level(ROUND_TOUCH_RST_GPIO, 0);
    if (err != ESP_OK) { return err; }
    vTaskDelay(pdMS_TO_TICKS(10));
    err = gpio_set_level(ROUND_TOUCH_RST_GPIO, 1);
    if (err != ESP_OK) { return err; }
    vTaskDelay(pdMS_TO_TICKS(300));

    /* The vendor demo writes 0xFF to 0xFE to keep the controller awake.
     * A missing touch chip must not prevent the eyes from starting. */
    const uint8_t keep_awake[] = {0xFE, 0xFF};
    err = i2c_master_transmit(s_touch, keep_awake, sizeof(keep_awake), 25);
    ESP_LOGI(TAG, "vendor touch wake: %s", esp_err_to_name(err));

    err = gpio_config(&interrupt);
    if (err != ESP_OK) { return err; }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK) { return err; }
    err = gpio_isr_handler_add(ROUND_TOUCH_INT_GPIO, touch_interrupt, NULL);
    if (err != ESP_OK) { return err; }

    /* Scan once to distinguish a different touch address from a missing or
     * unpowered controller. Some revisions may still ACK only after a touch. */
    unsigned int devices = 0;
    for (uint8_t address = 0x08; address <= 0x77; ++address) {
        if (i2c_master_probe(s_bus, address, 10) == ESP_OK) {
            ESP_LOGI(TAG, "I2C device at 0x%02x", address);
            ++devices;
        }
    }
    ESP_LOGI(TAG, "I2C scan: %u device(s); SDA=%d SCL=%d IRQ=%d RST=%d",
             devices, gpio_get_level(ROUND_TOUCH_SDA_GPIO),
             gpio_get_level(ROUND_TOUCH_SCL_GPIO),
             gpio_get_level(ROUND_TOUCH_INT_GPIO),
             gpio_get_level(ROUND_TOUCH_RST_GPIO));

    ESP_LOGI(TAG, "CST816 driver configured on GPIO%d/GPIO%d, IRQ GPIO%d",
             ROUND_TOUCH_SDA_GPIO, ROUND_TOUCH_SCL_GPIO, ROUND_TOUCH_INT_GPIO);
    return ESP_OK;
}

bool round_touch_read(round_touch_point_t *point)
{
    if (point == NULL) {
        return false;
    }

    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if ((uint32_t)(now_ms - s_last_diag_ms) >= 5000U) {
        s_last_diag_ms = now_ms;
        ESP_LOGI(TAG, "diag irq=%lu ack=%lu reads=%lu IRQ level=%d",
                 (unsigned long)s_irq_count, (unsigned long)s_ack_count,
                 (unsigned long)s_read_count,
                 gpio_get_level(ROUND_TOUCH_INT_GPIO));
    }
    const bool had_irq = s_irq_pending;
    s_irq_pending = false;
    if (!had_irq && (uint32_t)(now_ms - s_last_poll_ms) < 100U) {
        return false;
    }
    s_last_poll_ms = now_ms;

    /* Polling catches revisions without a usable IRQ; a NACK simply means
     * no touch data is available right now. */
    const esp_err_t probe = i2c_master_probe(s_bus, ROUND_TOUCH_I2C_ADDRESS, 25);
    if (probe != ESP_OK) {
        return false;
    }
    ++s_ack_count;

    /* CST816S/D: gesture at 0x01, finger count at 0x02, XY at 0x03..0x06.
     * Some revisions answer I2C only briefly after IRQ, so read immediately. */
    const uint8_t start_register = 0x01;
    uint8_t data[6];
    /* CST816 Arduino examples finish the register write with STOP before
     * issuing the read. A repeated-start transaction can fail on this chip. */
    esp_err_t err = i2c_master_transmit(s_touch, &start_register, 1, 25);
    if (err == ESP_OK) {
        err = i2c_master_receive(s_touch, data, sizeof(data), 25);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Touch IRQ but I2C read failed: %s", esp_err_to_name(err));
        return false;
    }
    if (data[1] != 1) {
        return false;
    }
    ++s_read_count;

    const uint16_t x = (uint16_t)(((data[2] & 0x0F) << 8) | data[3]);
    const uint16_t y = (uint16_t)(((data[4] & 0x0F) << 8) | data[5]);
    if (x >= ROUND_LCD_WIDTH || y >= ROUND_LCD_HEIGHT) {
        ESP_LOGW(TAG, "Touch outside 240x240: x=%u y=%u", x, y);
        return false;
    }
    if ((uint32_t)(now_ms - s_last_report_ms) < 250U &&
        x == s_last_x && y == s_last_y) {
        return false;
    }
    s_last_report_ms = now_ms;
    s_last_x = x;
    s_last_y = y;
    point->x = x;
    point->y = y;
    point->gesture = data[0];
    return true;
}
