#include "oled_display.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/spi_master.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_log.h"

#include "board_config.h"
#include "font_5x7.h"

#define OLED_BITS_PER_PIXEL 1
#define OLED_COMMAND_BITS 8
#define OLED_PARAMETER_BITS 8
#define OLED_SPI_MODE 0
#define OLED_SPI_TRANSACTION_QUEUE_DEPTH 1
#define FONT_WIDTH 5
#define FONT_HEIGHT 7
#define FONT_ADVANCE 6

#define OLED_FRAMEBUFFER_SIZE \
    (BOARD_OLED_WIDTH * BOARD_OLED_HEIGHT * OLED_BITS_PER_PIXEL / 8)

static const char *TAG = "oled_display";
static bool s_spi_bus_initialized;
static esp_lcd_panel_io_handle_t s_io_handle;
static esp_lcd_panel_handle_t s_panel_handle;
static uint8_t s_framebuffer[OLED_FRAMEBUFFER_SIZE];

static void oled_display_cleanup(void)
{
    if (s_panel_handle != NULL) {
        esp_err_t err = esp_lcd_panel_del(s_panel_handle);
        if (err == ESP_OK) {
            s_panel_handle = NULL;
        } else {
            ESP_LOGW(TAG, "Failed to delete OLED panel: %s",
                     esp_err_to_name(err));
            return;
        }
    }

    if (s_io_handle != NULL) {
        esp_err_t err = esp_lcd_panel_io_del(s_io_handle);
        if (err == ESP_OK) {
            s_io_handle = NULL;
        } else {
            ESP_LOGW(TAG, "Failed to delete OLED panel I/O: %s",
                     esp_err_to_name(err));
        }
    }

    if (s_spi_bus_initialized) {
        esp_err_t err = spi_bus_free(BOARD_OLED_SPI_HOST);
        if (err == ESP_OK) {
            s_spi_bus_initialized = false;
        } else {
            ESP_LOGW(TAG, "Failed to free OLED SPI bus: %s",
                     esp_err_to_name(err));
        }
    }
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

    /* SSD1306 stores eight vertical pixels in each page byte. */
    const size_t byte_index = (size_t)x +
                              ((size_t)y / 8U) * BOARD_OLED_WIDTH;
    const uint8_t pixel_mask =
        (uint8_t)(1U << ((unsigned int)y % 8U));

    if (on) {
        s_framebuffer[byte_index] |= pixel_mask;
    } else {
        s_framebuffer[byte_index] &= (uint8_t)~pixel_mask;
    }
}

static void framebuffer_draw_character(int x, int y, char character)
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

static void framebuffer_draw_text(int x, int y, const char *text)
{
    while (*text != '\0') {
        framebuffer_draw_character(x, y, *text);
        x += FONT_ADVANCE;
        ++text;
    }
}

esp_err_t oled_display_init(void)
{
    if (s_spi_bus_initialized || s_panel_handle != NULL ||
        s_io_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const spi_bus_config_t bus_config = {
        .sclk_io_num = BOARD_OLED_SPI_SCLK_GPIO,
        .mosi_io_num = BOARD_OLED_SPI_MOSI_GPIO,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = OLED_FRAMEBUFFER_SIZE,
    };

    esp_err_t err = spi_bus_initialize(BOARD_OLED_SPI_HOST, &bus_config,
                                        SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize OLED SPI bus: %s",
                 esp_err_to_name(err));
        return err;
    }
    s_spi_bus_initialized = true;

    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = BOARD_OLED_SPI_CS_GPIO,
        .dc_gpio_num = BOARD_OLED_SPI_DC_GPIO,
        .spi_mode = OLED_SPI_MODE,
        .pclk_hz = BOARD_OLED_SPI_CLOCK_HZ,
        .trans_queue_depth = OLED_SPI_TRANSACTION_QUEUE_DEPTH,
        .lcd_cmd_bits = OLED_COMMAND_BITS,
        .lcd_param_bits = OLED_PARAMETER_BITS,
        /* SSD1306 command arguments also require D/C low in 4-wire SPI. */
        .flags.dc_low_on_param = 1,
    };

    err = esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)BOARD_OLED_SPI_HOST,
        &io_config,
        &s_io_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create OLED panel I/O: %s",
                 esp_err_to_name(err));
        oled_display_cleanup();
        return err;
    }

    const esp_lcd_panel_ssd1306_config_t ssd1306_config = {
        .height = BOARD_OLED_HEIGHT,
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BOARD_OLED_SPI_RESET_GPIO,
        .bits_per_pixel = OLED_BITS_PER_PIXEL,
        .vendor_config = (void *)&ssd1306_config,
    };

    err = esp_lcd_new_panel_ssd1306(s_io_handle, &panel_config,
                                     &s_panel_handle);
    if (err == ESP_OK) {
        err = esp_lcd_panel_reset(s_panel_handle);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_init(s_panel_handle);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_disp_on_off(s_panel_handle, true);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SSD1306 initialization failed: %s",
                 esp_err_to_name(err));
        oled_display_cleanup();
        return err;
    }

    ESP_LOGI(TAG,
             "SSD1306-compatible SPI OLED initialized: SCK=%d, MOSI=%d, "
             "CS=%d, DC=%d, RESET=%d",
             BOARD_OLED_SPI_SCLK_GPIO,
             BOARD_OLED_SPI_MOSI_GPIO,
             BOARD_OLED_SPI_CS_GPIO,
             BOARD_OLED_SPI_DC_GPIO,
             BOARD_OLED_SPI_RESET_GPIO);
    return ESP_OK;
}

esp_err_t oled_display_show_startup(void)
{
    if (s_panel_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    oled_display_clear();
    framebuffer_draw_text(22, 20, "SMART KEYCHAIN");
    framebuffer_draw_text(49, 36, "READY");

    esp_err_t err = oled_display_present();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Startup screen sent to OLED");
    }

    return err;
}

esp_err_t oled_display_present(void)
{
    if (s_panel_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_lcd_panel_draw_bitmap(s_panel_handle,
                                     0, 0,
                                     BOARD_OLED_WIDTH,
                                     BOARD_OLED_HEIGHT,
                                     s_framebuffer);
}

esp_err_t oled_display_deinit(void)
{
    if (!s_spi_bus_initialized && s_panel_handle == NULL &&
        s_io_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_panel_handle != NULL) {
        esp_err_t err = esp_lcd_panel_del(s_panel_handle);
        if (err != ESP_OK) {
            return err;
        }
        s_panel_handle = NULL;
    }

    if (s_io_handle != NULL) {
        esp_err_t err = esp_lcd_panel_io_del(s_io_handle);
        if (err != ESP_OK) {
            return err;
        }
        s_io_handle = NULL;
    }

    if (s_spi_bus_initialized) {
        esp_err_t err = spi_bus_free(BOARD_OLED_SPI_HOST);
        if (err != ESP_OK) {
            return err;
        }
        s_spi_bus_initialized = false;
    }

    return ESP_OK;
}
