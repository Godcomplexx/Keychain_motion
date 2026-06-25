#include "idle_animation.h"

#include <stdint.h>

#include "esp_err.h"

#include "board_config.h"
#include "oled_display.h"

#include "idle_animation_frames.inc"

#define IDLE_FRAME_WIDTH BOARD_OLED_WIDTH
#define IDLE_FRAME_HEIGHT BOARD_OLED_HEIGHT
#define IDLE_PAGE_HEIGHT 8

uint8_t idle_animation_get_frame_count(void)
{
    return IDLE_ANIMATION_FRAME_COUNT;
}

esp_err_t idle_animation_render_frame(uint8_t frame_index)
{
    if (IDLE_ANIMATION_FRAME_COUNT == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    const uint8_t wrapped_frame =
        (uint8_t)(frame_index % IDLE_ANIMATION_FRAME_COUNT);
    const uint8_t *frame = s_idle_frames[wrapped_frame];

    oled_display_clear();

    /*
     * Frames are stored in SSD1306 page order: each byte is one vertical
     * group of 8 pixels at a single x coordinate.
     */
    for (int page = 0; page < IDLE_FRAME_HEIGHT / IDLE_PAGE_HEIGHT; ++page) {
        for (int x = 0; x < IDLE_FRAME_WIDTH; ++x) {
            const uint8_t pixels =
                frame[x + page * IDLE_FRAME_WIDTH];

            if (pixels == 0) {
                continue;
            }

            for (int bit = 0; bit < IDLE_PAGE_HEIGHT; ++bit) {
                if ((pixels & (1U << bit)) != 0U) {
                    oled_display_set_pixel(x,
                                           page * IDLE_PAGE_HEIGHT + bit,
                                           true);
                }
            }
        }
    }

    return oled_display_present();
}
