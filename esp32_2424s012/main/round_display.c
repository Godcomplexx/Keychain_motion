#include "round_display.h"

#include <string.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "font_5x7.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "oled_display.h"

/* pet_face scales its 128-wide design to the panel itself and carves shapes at
 * full resolution, so this adapter draws 1:1. Only the 5x7 font is enlarged:
 * each glyph dot becomes a TEXT_SCALE x TEXT_SCALE block to stay readable. */
#define TEXT_SCALE 2
#define LCD_STRIP_HEIGHT 20
#define LCD_MAX_STRIPS (ROUND_LCD_HEIGHT / LCD_STRIP_HEIGHT)

static uint16_t s_frame[ROUND_LCD_WIDTH * ROUND_LCD_HEIGHT];
static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_flush_done;

/*
 * Row bands, inclusive; top > bottom means empty. "Ink" rows may hold
 * non-black pixels in s_frame. "Dirty" rows changed since the last present(),
 * by drawing or by clearing old ink, so only they are sent over SPI. The
 * panel's power-on contents are unknown, so the first present() sends all.
 */
static int s_ink_top = 0;
static int s_ink_bottom = ROUND_LCD_HEIGHT - 1;
static int s_dirty_top = 0;
static int s_dirty_bottom = ROUND_LCD_HEIGHT - 1;

static void mark_rows(int *top, int *bottom, int from, int to)
{
    if (from < *top) { *top = from; }
    if (to > *bottom) { *bottom = to; }
}
static bool s_marker_visible;
static uint16_t s_marker_x;
static uint16_t s_marker_y;

static bool IRAM_ATTR flush_finished(esp_lcd_panel_io_handle_t io,
                                     esp_lcd_panel_io_event_data_t *event,
                                     void *context)
{
    BaseType_t task_woken = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)context, &task_woken);
    return task_woken == pdTRUE;
}

esp_err_t round_display_init(void)
{
    /* One count per strip, so present() can queue every strip and wait once. */
    s_flush_done = xSemaphoreCreateCounting(LCD_MAX_STRIPS, 0);
    if (s_flush_done == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const spi_bus_config_t bus = GC9A01_PANEL_BUS_SPI_CONFIG(
        ROUND_LCD_SCLK_GPIO, ROUND_LCD_MOSI_GPIO,
        ROUND_LCD_WIDTH * LCD_STRIP_HEIGHT * sizeof(uint16_t));
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }

    esp_lcd_panel_io_handle_t io;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = ROUND_LCD_CS_GPIO,
        .dc_gpio_num = ROUND_LCD_DC_GPIO,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000, /* Within GC9A01 limits; checked by eye. */
        .trans_queue_depth = 2,
        .on_color_trans_done = flush_finished,
        .user_ctx = s_flush_done,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                   &io_config, &io);
    if (err != ESP_OK) {
        return err;
    }

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1, /* LCD reset is tied to the board power circuit. */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_gc9a01(io, &panel_config, &s_panel);
    if (err != ESP_OK) {
        return err;
    }
    if ((err = esp_lcd_panel_reset(s_panel)) != ESP_OK ||
        (err = esp_lcd_panel_init(s_panel)) != ESP_OK ||
        /* This GC9A01 glass needs the panel's inversion mode for true colors. */
        (err = esp_lcd_panel_invert_color(s_panel, true)) != ESP_OK ||
        (err = esp_lcd_panel_disp_on_off(s_panel, true)) != ESP_OK) {
        return err;
    }

    const gpio_config_t backlight = {
        .pin_bit_mask = 1ULL << ROUND_LCD_BL_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    if ((err = gpio_config(&backlight)) != ESP_OK) {
        return err;
    }
    return gpio_set_level(ROUND_LCD_BL_GPIO, 1);
}

void round_display_touch_marker(uint16_t x, uint16_t y, bool visible)
{
    s_marker_x = x;
    s_marker_y = y;
    s_marker_visible = visible;
}

/* The panel consumes high byte first; ESP32-C3 stores uint16_t low byte first. */
static void set_physical_pixel(int x, int y, uint16_t rgb565)
{
    if (x >= 0 && x < ROUND_LCD_WIDTH && y >= 0 && y < ROUND_LCD_HEIGHT) {
        s_frame[y * ROUND_LCD_WIDTH + x] = (uint16_t)((rgb565 << 8) | (rgb565 >> 8));
        mark_rows(&s_ink_top, &s_ink_bottom, y, y);
        mark_rows(&s_dirty_top, &s_dirty_bottom, y, y);
    }
}

bool oled_display_is_color(void) { return true; }
int oled_display_panel_width(void) { return ROUND_LCD_WIDTH; }
int oled_display_panel_height(void) { return ROUND_LCD_HEIGHT; }

void oled_display_clear(void)
{
    if (s_ink_top > s_ink_bottom) {
        return; /* Already black everywhere. */
    }
    /* Everything outside the ink band is black already; wiping it changes rows. */
    memset(&s_frame[s_ink_top * ROUND_LCD_WIDTH], 0,
           (size_t)(s_ink_bottom - s_ink_top + 1) * ROUND_LCD_WIDTH * sizeof(s_frame[0]));
    mark_rows(&s_dirty_top, &s_dirty_bottom, s_ink_top, s_ink_bottom);
    s_ink_top = ROUND_LCD_HEIGHT;
    s_ink_bottom = -1;
}

void oled_display_set_pixel_rgb(int x, int y, uint16_t color565)
{
    set_physical_pixel(x, y, color565); /* Bounds are checked there. */
}

void oled_display_set_pixel(int x, int y, bool on)
{
    oled_display_set_pixel_rgb(x, y, on ? 0xFFFF : 0);
}

void oled_display_draw_text_rgb(int x, int y, const char *text, uint16_t color565)
{
    for (; *text != '\0'; ++text, x += 6 * TEXT_SCALE) {
        const font_5x7_glyph_t *glyph = NULL;
        for (size_t index = 0; index < sizeof(FONT_5X7_GLYPHS) / sizeof(FONT_5X7_GLYPHS[0]); ++index) {
            if (FONT_5X7_GLYPHS[index].character == *text) {
                glyph = &FONT_5X7_GLYPHS[index];
                break;
            }
        }
        if (glyph == NULL) {
            continue;
        }
        for (int column = 0; column < 5; ++column) {
            for (int row = 0; row < 7; ++row) {
                if ((glyph->columns[column] & (1U << row)) == 0) {
                    continue;
                }
                for (int dy = 0; dy < TEXT_SCALE; ++dy) {
                    for (int dx = 0; dx < TEXT_SCALE; ++dx) {
                        set_physical_pixel(x + column * TEXT_SCALE + dx,
                                           y + row * TEXT_SCALE + dy, color565);
                    }
                }
            }
        }
    }
}

void oled_display_draw_text(int x, int y, const char *text)
{
    oled_display_draw_text_rgb(x, y, text, 0xFFFF);
}

esp_err_t oled_display_present(void)
{
    if (s_marker_visible) {
        /* A yellow cross proves both the touch event and its raw coordinates. */
        for (int delta = -8; delta <= 8; ++delta) {
            set_physical_pixel((int)s_marker_x + delta, s_marker_y, 0xFFE0);
            set_physical_pixel(s_marker_x, (int)s_marker_y + delta, 0xFFE0);
        }
    }
    /*
     * Queue every changed strip back to back. The SPI driver itself waits for
     * the previous strip before starting the next, and wakes this task at
     * once. Waiting on s_flush_done after each strip instead cost a whole
     * FreeRTOS tick per strip, because the "done" callback cannot ask for an
     * immediate task switch.
     */
    int strips = 0;
    esp_err_t err = ESP_OK;
    for (int y = s_dirty_top; y <= s_dirty_bottom && err == ESP_OK;
         y += LCD_STRIP_HEIGHT) {
        int end = y + LCD_STRIP_HEIGHT;
        if (end > s_dirty_bottom + 1) { end = s_dirty_bottom + 1; }
        err = esp_lcd_panel_draw_bitmap(s_panel, 0, y, ROUND_LCD_WIDTH, end,
                                        &s_frame[y * ROUND_LCD_WIDTH]);
        if (err == ESP_OK) { ++strips; }
    }
    /* The next render overwrites s_frame, so every queued strip must be sent. */
    for (int done = 0; done < strips; ++done) {
        if (xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
    }
    if (err == ESP_OK) {
        s_dirty_top = ROUND_LCD_HEIGHT;
        s_dirty_bottom = -1;
    }
    return err;
}
