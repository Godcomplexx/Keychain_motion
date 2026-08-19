#include "oled_display.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#include "board_config.h"
#include "esp_log.h"
#include "font_5x7.h"
#include "oled_display_optional.h"

#define OLED_FRAMEBUFFER_SIZE (BOARD_OLED_WIDTH * BOARD_OLED_HEIGHT / 8)
#define OLED_DATA_CHUNK_SIZE 128
#define FONT_WIDTH 5
#define FONT_HEIGHT 7
#define FONT_ADVANCE 6

static const char *TAG = "oled";
static const struct device *const s_i2c = DEVICE_DT_GET(BOARD_I2C_NODE);
static uint8_t s_framebuffer[OLED_FRAMEBUFFER_SIZE];
static bool s_available;

static esp_err_t send_commands(const uint8_t *commands, size_t count)
{
    uint8_t packet[32];
    if (count + 1U > sizeof(packet)) {
        return ESP_ERR_INVALID_SIZE;
    }

    packet[0] = 0x00; /* SSD1306 control byte: following bytes are commands. */
    memcpy(&packet[1], commands, count);
    return i2c_write(s_i2c, packet, count + 1U,
                     BOARD_OLED_I2C_ADDRESS) == 0 ? ESP_OK : ESP_FAIL;
}

static const uint8_t *font_5x7_find(char character)
{
    for (size_t index = 0; index < FONT_5X7_GLYPH_COUNT; ++index) {
        if (FONT_5X7_GLYPHS[index].character == character) {
            return FONT_5X7_GLYPHS[index].columns;
        }
    }
    return NULL;
}

bool oled_display_is_available(void)
{
    return s_available;
}

void oled_display_clear(void)
{
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
}

void oled_display_set_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= BOARD_OLED_WIDTH ||
        y < 0 || y >= BOARD_OLED_HEIGHT) {
        return;
    }

    const size_t byte_index = (size_t)x +
                              ((size_t)y / 8U) * BOARD_OLED_WIDTH;
    const uint8_t mask = (uint8_t)(1U << ((unsigned int)y % 8U));
    if (on) {
        s_framebuffer[byte_index] |= mask;
    } else {
        s_framebuffer[byte_index] &= (uint8_t)~mask;
    }
}

static void draw_character(int x, int y, char character)
{
    const uint8_t *columns = font_5x7_find(character);
    if (columns == NULL) {
        return;
    }

    for (int column = 0; column < FONT_WIDTH; ++column) {
        for (int row = 0; row < FONT_HEIGHT; ++row) {
            if ((columns[column] & (1U << row)) != 0U) {
                oled_display_set_pixel(x + column, y + row, true);
            }
        }
    }
}

void oled_display_draw_text(int x, int y, const char *text)
{
    if (text == NULL) {
        return;
    }
    while (*text != '\0') {
        draw_character(x, y, *text++);
        x += FONT_ADVANCE;
    }
}

esp_err_t oled_display_init(void)
{
    if (s_available) {
        return ESP_OK;
    }
    if (!device_is_ready(s_i2c)) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t init_commands[] = {
        0xAE,       /* Display off while geometry and power are configured. */
        0xD5, 0x80, /* Default oscillator and display clock. */
        0xA8, 0x3F, /* 64 multiplexed rows. */
        0xD3, 0x00, /* No vertical display offset. */
        0x40,       /* Display RAM starts at line zero. */
        0x8D, 0x14, /* Enable the internal charge pump. */
        0x20, 0x00, /* Horizontal addressing matches our page framebuffer. */
        0xA1,       /* Mirror columns for the common 0.96-inch module. */
        0xC8,       /* Scan COM outputs from high to low. */
        0xDA, 0x12, /* Alternative COM pin layout for 128x64 panels. */
        0x81, 0x7F, /* Moderate initial contrast. */
        0xD9, 0xF1, /* Charge-pump precharge period. */
        0xDB, 0x40, /* VCOM deselect level. */
        0xA4,       /* Show display RAM, not the all-on test pattern. */
        0xA6,       /* Normal (non-inverted) pixels. */
        0xAF,       /* Display on. */
    };

    esp_err_t err = send_commands(init_commands, sizeof(init_commands));
    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    s_available = true;
    oled_display_clear();
    ESP_LOGI(TAG, "SSD1306 ready at 0x%02X", BOARD_OLED_I2C_ADDRESS);
    return ESP_OK;
}

esp_err_t oled_display_present(void)
{
    if (!s_available) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t address_commands[] = {
        0x21, 0x00, BOARD_OLED_WIDTH - 1,
        0x22, 0x00, (BOARD_OLED_HEIGHT / 8) - 1,
    };
    esp_err_t err = send_commands(address_commands,
                                  sizeof(address_commands));
    if (err != ESP_OK) {
        s_available = false;
        return err;
    }

    uint8_t packet[OLED_DATA_CHUNK_SIZE + 1U];
    packet[0] = 0x40; /* SSD1306 control byte: following bytes are RAM data. */
    for (size_t offset = 0; offset < sizeof(s_framebuffer);
         offset += OLED_DATA_CHUNK_SIZE) {
        size_t count = sizeof(s_framebuffer) - offset;
        if (count > OLED_DATA_CHUNK_SIZE) {
            count = OLED_DATA_CHUNK_SIZE;
        }
        memcpy(&packet[1], &s_framebuffer[offset], count);
        if (i2c_write(s_i2c, packet, count + 1U,
                      BOARD_OLED_I2C_ADDRESS) != 0) {
            s_available = false;
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t oled_display_show_startup(void)
{
    if (!s_available) {
        return ESP_ERR_INVALID_STATE;
    }

    oled_display_clear();
    oled_display_draw_text(22, 20, "SMART KEYCHAIN");
    oled_display_draw_text(40, 36, "XIAO READY");
    return oled_display_present();
}

esp_err_t oled_display_set_contrast(uint8_t contrast)
{
    if (!s_available) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t commands[] = {0x81, contrast};
    return send_commands(commands, sizeof(commands));
}

esp_err_t oled_display_set_power(bool on)
{
    if (!s_available) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t command = on ? 0xAF : 0xAE;
    return send_commands(&command, 1);
}

esp_err_t oled_display_deinit(void)
{
    if (!s_available) {
        return ESP_OK;
    }
    (void)oled_display_set_power(false);
    s_available = false;
    return ESP_OK;
}
