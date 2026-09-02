#include "oled_display.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

#include "board_config.h"
#include "esp_log.h"
#include "font_5x7.h"
#include "oled_display_optional.h"

/*
 * Driver for the bodge-wired round GC9107 IPS panel, standing in for the
 * SSD1306 this project normally talks to over I2C. It implements the same
 * oled_display.h contract oled_display_zephyr.c does, so a renderer that has
 * not been touched - time_animation, message_screen, draw_pad - keeps drawing
 * into the same 128x64 monochrome canvas without change, letterboxed at
 * PANEL_Y_OFFSET into this panel's taller frame. A renderer written to know
 * about colour and the round shape (pet_face) instead calls
 * oled_display_is_color()/panel_width()/panel_height() and draws full-
 * resolution RGB565 straight into the same buffer via
 * oled_display_set_pixel_rgb(); both paths write the one s_pixel_buffer this
 * file owns, so a single oled_display_present() sends whichever was drawn.
 *
 * The two are mutually exclusive translation units: CMakeLists.txt compiles
 * one or the other, chosen by KEYCHAIN_DISPLAY_DRIVER, because both define
 * the same oled_display_* symbols. The default stays the I2C OLED; this file
 * only links in when a build explicitly asks for it, so every other unit
 * with the standard display is unaffected.
 *
 * The command set below is the MIPI DCS baseline (SLPOUT, COLMOD, MADCTL,
 * CASET/RASET/RAMWR, DISPON) common to essentially this whole family of small
 * SPI panels. What was available to write this driver was the pinout and
 * mechanical page of the GC9107 datasheet, not its full register reference,
 * so it deliberately does not touch vendor-specific gamma/power/frame-rate
 * registers that a fuller datasheet would tune - those affect image quality,
 * not whether pixels land in the right place. MADCTL's colour-order bit in
 * particular is a guess (0x00) and may need flipping once seen on hardware.
 */

#define FONT_WIDTH 5
#define FONT_HEIGHT 7
#define FONT_ADVANCE 6

#define GC9107_CMD_SLPOUT 0x11
#define GC9107_CMD_COLMOD 0x3A
#define GC9107_CMD_MADCTL 0x36
#define GC9107_CMD_CASET 0x2A
#define GC9107_CMD_RASET 0x2B
#define GC9107_CMD_RAMWR 0x2C
#define GC9107_CMD_DISPON 0x29
#define GC9107_CMD_DISPOFF 0x28

/* RGB565, one byte per bit-per-pixel-times-8: 0x05 is the DCS code for it. */
#define GC9107_COLMOD_RGB565 0x05

/* The logical canvas every renderer draws into - unchanged from the OLED. */
#define CANVAS_WIDTH BOARD_OLED_WIDTH
#define CANVAS_HEIGHT BOARD_OLED_HEIGHT

/* Centred inside the taller round panel rather than pinned to its top. */
#define PANEL_Y_OFFSET (((int)BOARD_GC9107_HEIGHT - CANVAS_HEIGHT) / 2)

static const char *TAG = "gc9107";

static const struct device *const s_spi = DEVICE_DT_GET(BOARD_GC9107_SPI_NODE);
static const struct gpio_dt_spec s_cs =
    GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), gc9107_cs_gpios);
static const struct gpio_dt_spec s_dc =
    GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), gc9107_dc_gpios);
static const struct gpio_dt_spec s_reset =
    GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), gc9107_reset_gpios);

/*
 * One full RGB565 frame at the panel's own physical resolution, not the
 * smaller logical canvas - so a caller that wants the round display's real
 * shape and colour (oled_display_set_pixel_rgb) can reach every pixel, while
 * a caller still using the plain on/off API lands inside it letterboxed at
 * PANEL_Y_OFFSET, exactly as before. ~29 KiB, well inside this chip's 256 KiB
 * RAM, and simpler and more robust than chunking the transfer around CS
 * toggles.
 */
static uint8_t s_pixel_buffer[BOARD_GC9107_WIDTH * BOARD_GC9107_HEIGHT * 2];
static bool s_available;

static const struct spi_config s_spi_cfg = {
    /*
     * The datasheet excerpt available for this driver did not give a maximum
     * SPI clock. 8 MHz is comfortably inside what this whole controller
     * family tolerates, chosen conservatively rather than pushed for speed
     * absent a confirmed ceiling.
     */
    .frequency = 8000000,
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER,
    /* CS is a plain GPIO this driver drives itself (see the file comment on
     * why: D/C needs the same per-byte handling and the SPI framework only
     * automates CS, not both). */
    .cs = {0},
};

static void select_chip(bool selected)
{
    /* GPIO_ACTIVE_LOW in the overlay means gpio_pin_set(..., 1) drives the
     * physical line low, which is exactly the "selected" state here. */
    gpio_pin_set_dt(&s_cs, selected ? 1 : 0);
}

static esp_err_t write_bytes(const uint8_t *data, size_t length, bool is_data)
{
    gpio_pin_set_dt(&s_dc, is_data ? 1 : 0);

    const struct spi_buf buf = {.buf = (void *)data, .len = length};
    const struct spi_buf_set set = {.buffers = &buf, .count = 1};

    select_chip(true);
    const int result = spi_write(s_spi, &s_spi_cfg, &set);
    select_chip(false);

    return result == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t write_command(uint8_t command)
{
    return write_bytes(&command, 1, false);
}

static esp_err_t write_command_data(uint8_t command, const uint8_t *params,
                                    size_t length)
{
    esp_err_t err = write_command(command);
    if (err != ESP_OK || length == 0U) {
        return err;
    }
    return write_bytes(params, length, true);
}

static esp_err_t set_window(int x, int y, int width, int height)
{
    const int x_end = x + width - 1;
    const int y_end = y + height - 1;

    const uint8_t caset[4] = {(uint8_t)(x >> 8), (uint8_t)x,
                              (uint8_t)(x_end >> 8), (uint8_t)x_end};
    const uint8_t raset[4] = {(uint8_t)(y >> 8), (uint8_t)y,
                              (uint8_t)(y_end >> 8), (uint8_t)y_end};

    esp_err_t err = write_command_data(GC9107_CMD_CASET, caset, sizeof(caset));
    if (err != ESP_OK) {
        return err;
    }
    return write_command_data(GC9107_CMD_RASET, raset, sizeof(raset));
}

bool oled_display_is_available(void)
{
    return s_available;
}

esp_err_t oled_display_init(void)
{
    if (s_available) {
        return ESP_OK;
    }
    if (!device_is_ready(s_spi) || !gpio_is_ready_dt(&s_cs) ||
        !gpio_is_ready_dt(&s_dc) || !gpio_is_ready_dt(&s_reset)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (gpio_pin_configure_dt(&s_cs, GPIO_OUTPUT_INACTIVE) != 0 ||
        gpio_pin_configure_dt(&s_dc, GPIO_OUTPUT_INACTIVE) != 0 ||
        gpio_pin_configure_dt(&s_reset, GPIO_OUTPUT_INACTIVE) != 0) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Hardware reset: the datasheet marks RESET active low and requires it to
     * initialise the chip at all - this is not optional the way it is on
     * some controllers with a software reset fallback. */
    gpio_pin_set_dt(&s_reset, 1); /* asserted (active low) */
    k_sleep(K_MSEC(10));
    gpio_pin_set_dt(&s_reset, 0); /* released */
    k_sleep(K_MSEC(120));

    esp_err_t err = write_command(GC9107_CMD_SLPOUT);
    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    k_sleep(K_MSEC(120)); /* DCS-mandated settle time after sleep-out. */

    const uint8_t colmod = GC9107_COLMOD_RGB565;
    err = write_command_data(GC9107_CMD_COLMOD, &colmod, 1);
    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    /*
     * Orientation/colour-order byte. 0x00 is the DCS default (top-to-bottom,
     * left-to-right, RGB). If the image comes up mirrored or colour-swapped
     * on real hardware, this is the one byte to change - not a wiring fault.
     */
    const uint8_t madctl = 0x00;
    err = write_command_data(GC9107_CMD_MADCTL, &madctl, 1);
    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    err = write_command(GC9107_CMD_DISPON);
    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    k_sleep(K_MSEC(20));

    s_available = true;
    oled_display_clear();
    ESP_LOGI(TAG, "GC9107 ready on %s", s_spi->name);
    return ESP_OK;
}

/* Write one pixel straight into the panel-native RGB565 buffer. Every other
 * pixel-setting function in this file funnels through here. */
static void plot_panel(int x, int y, uint16_t color)
{
    if (x < 0 || x >= BOARD_GC9107_WIDTH || y < 0 || y >= BOARD_GC9107_HEIGHT) {
        return;
    }
    const size_t offset = (size_t)(y * BOARD_GC9107_WIDTH + x) * 2U;
    s_pixel_buffer[offset] = (uint8_t)(color >> 8);
    s_pixel_buffer[offset + 1U] = (uint8_t)color;
}

void oled_display_clear(void)
{
    memset(s_pixel_buffer, 0, sizeof(s_pixel_buffer));
}

void oled_display_set_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= CANVAS_WIDTH || y < 0 || y >= CANVAS_HEIGHT) {
        return;
    }
    plot_panel(x, y + PANEL_Y_OFFSET, on ? 0xFFFFU : 0x0000U);
}

bool oled_display_is_color(void)
{
    return true;
}

int oled_display_panel_width(void)
{
    return BOARD_GC9107_WIDTH;
}

int oled_display_panel_height(void)
{
    return BOARD_GC9107_HEIGHT;
}

void oled_display_set_pixel_rgb(int x, int y, uint16_t color565)
{
    plot_panel(x, y, color565);
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

static void draw_character_scaled(int x, int y, char character, int scale)
{
    const uint8_t *columns = font_5x7_find(character);
    if (columns == NULL) {
        return;
    }

    for (int column = 0; column < FONT_WIDTH; ++column) {
        for (int row = 0; row < FONT_HEIGHT; ++row) {
            if ((columns[column] & (1U << row)) == 0U) {
                continue;
            }
            for (int block_y = 0; block_y < scale; ++block_y) {
                for (int block_x = 0; block_x < scale; ++block_x) {
                    oled_display_set_pixel(x + column * scale + block_x,
                                           y + row * scale + block_y, true);
                }
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
        draw_character_scaled(x, y, *text++, 1);
        x += FONT_ADVANCE;
    }
}

static void draw_character_rgb(int x, int y, char character, uint16_t color)
{
    const uint8_t *columns = font_5x7_find(character);
    if (columns == NULL) {
        return;
    }

    for (int column = 0; column < FONT_WIDTH; ++column) {
        for (int row = 0; row < FONT_HEIGHT; ++row) {
            if ((columns[column] & (1U << row)) != 0U) {
                plot_panel(x + column, y + row, color);
            }
        }
    }
}

void oled_display_draw_text_rgb(int x, int y, const char *text,
                                uint16_t color565)
{
    if (text == NULL) {
        return;
    }
    while (*text != '\0') {
        draw_character_rgb(x, y, *text++, color565);
        x += FONT_ADVANCE;
    }
}

void oled_display_draw_text_scaled(int x, int y, const char *text, int scale)
{
    if (text == NULL) {
        return;
    }
    if (scale < 1) {
        scale = 1;
    }
    while (*text != '\0') {
        draw_character_scaled(x, y, *text++, scale);
        x += FONT_ADVANCE * scale;
    }
}

int oled_display_text_width(const char *text, int scale)
{
    if (text == NULL) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }
    const int characters = (int)strlen(text);
    if (characters == 0) {
        return 0;
    }
    return characters * FONT_ADVANCE * scale - scale;
}

void oled_display_draw_rect(int x, int y, int width, int height, bool on)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    for (int px = x; px < x + width; ++px) {
        oled_display_set_pixel(px, y, on);
        oled_display_set_pixel(px, y + height - 1, on);
    }
    for (int py = y; py < y + height; ++py) {
        oled_display_set_pixel(x, py, on);
        oled_display_set_pixel(x + width - 1, py, on);
    }
}

void oled_display_fill_rect(int x, int y, int width, int height, bool on)
{
    for (int py = y; py < y + height; ++py) {
        for (int px = x; px < x + width; ++px) {
            oled_display_set_pixel(px, py, on);
        }
    }
}

esp_err_t oled_display_show_startup(void)
{
    if (!s_available) {
        return ESP_ERR_INVALID_STATE;
    }

    oled_display_clear();
    oled_display_draw_text(22, 20, "SMART KEYCHAIN");
    oled_display_draw_text(40, 36, "GC9107 READY");
    return oled_display_present();
}

esp_err_t oled_display_present(void)
{
    if (!s_available) {
        return ESP_ERR_INVALID_STATE;
    }

    /* s_pixel_buffer is already RGB565 at the panel's own resolution - both
     * oled_display_set_pixel (canvas, letterboxed) and oled_display_set_pixel_rgb
     * (full panel) write into it directly, so presenting is just sending it. */
    esp_err_t err =
        set_window(0, 0, BOARD_GC9107_WIDTH, BOARD_GC9107_HEIGHT);
    if (err != ESP_OK) {
        return err;
    }

    err = write_command(GC9107_CMD_RAMWR);
    if (err != ESP_OK) {
        return err;
    }
    return write_bytes(s_pixel_buffer, sizeof(s_pixel_buffer), true);
}

esp_err_t oled_display_set_contrast(uint8_t contrast)
{
    /*
     * No brightness/contrast register was available to confirm for this
     * chip - the datasheet excerpt this driver was written from covers pins
     * and the base command set, not backlight or gamma control. The pet's
     * sleep/wake dimming this normally drives is a no-op here rather than a
     * guess at an unconfirmed register.
     */
    (void)contrast;
    return s_available ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t oled_display_set_power(bool on)
{
    if (!s_available) {
        return ESP_ERR_INVALID_STATE;
    }
    return write_command(on ? GC9107_CMD_DISPON : GC9107_CMD_DISPOFF);
}

esp_err_t oled_display_deinit(void)
{
    s_available = false;
    return ESP_OK;
}
