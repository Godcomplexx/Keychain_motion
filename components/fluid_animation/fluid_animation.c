#include "fluid_animation.h"

#include <stdbool.h>

#include "board_config.h"
#include "oled_display.h"

#define MAIN_BLOB_RADIUS 12
#define TRAILING_BLOB_RADIUS 8
#define LEADING_BLOB_RADIUS 6
#define BLOB_ACCELERATION 70.0f
#define BLOB_DAMPING 2.5f
#define BLOB_RESTITUTION 0.55f
#define MAX_DELTA_SECONDS 0.20f
#define TRAILING_OFFSET 8.0f
#define LEADING_OFFSET 5.0f

typedef struct {
    float x;
    float y;
    float velocity_x;
    float velocity_y;
} fluid_state_t;

static fluid_state_t s_state;

static float clamp_float(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static void draw_filled_circle(int center_x, int center_y, int radius)
{
    const int radius_squared = radius * radius;

    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if ((x * x) + (y * y) <= radius_squared) {
                oled_display_set_pixel(center_x + x, center_y + y, true);
            }
        }
    }
}

static void update_axis(float *position, float *velocity, float tilt,
                        float minimum, float maximum, float delta_seconds)
{
    *velocity += tilt * BLOB_ACCELERATION * delta_seconds;
    *velocity /= 1.0f + (BLOB_DAMPING * delta_seconds);
    *position += *velocity * delta_seconds;

    if (*position < minimum) {
        *position = minimum;
        *velocity = -*velocity * BLOB_RESTITUTION;
    } else if (*position > maximum) {
        *position = maximum;
        *velocity = -*velocity * BLOB_RESTITUTION;
    }
}

void fluid_animation_reset(void)
{
    s_state.x = BOARD_OLED_WIDTH / 2.0f;
    s_state.y = BOARD_OLED_HEIGHT / 2.0f;
    s_state.velocity_x = 0.0f;
    s_state.velocity_y = 0.0f;
}

esp_err_t fluid_animation_render(float tilt_x, float tilt_y,
                                 float delta_seconds)
{
    tilt_x = clamp_float(tilt_x, -1.0f, 1.0f);
    tilt_y = clamp_float(tilt_y, -1.0f, 1.0f);
    delta_seconds = clamp_float(delta_seconds, 0.0f, MAX_DELTA_SECONDS);

    update_axis(&s_state.x, &s_state.velocity_x, tilt_x,
                MAIN_BLOB_RADIUS,
                BOARD_OLED_WIDTH - 1 - MAIN_BLOB_RADIUS,
                delta_seconds);
    update_axis(&s_state.y, &s_state.velocity_y, tilt_y,
                MAIN_BLOB_RADIUS,
                BOARD_OLED_HEIGHT - 1 - MAIN_BLOB_RADIUS,
                delta_seconds);

    const int center_x = (int)s_state.x;
    const int center_y = (int)s_state.y;
    const int trailing_x = center_x - (int)(tilt_x * TRAILING_OFFSET);
    const int trailing_y = center_y - (int)(tilt_y * TRAILING_OFFSET);
    const int leading_x = center_x + (int)(tilt_x * LEADING_OFFSET);
    const int leading_y = center_y + (int)(tilt_y * LEADING_OFFSET);

    oled_display_clear();
    draw_filled_circle(trailing_x, trailing_y, TRAILING_BLOB_RADIUS);
    draw_filled_circle(center_x, center_y, MAIN_BLOB_RADIUS);
    draw_filled_circle(leading_x, leading_y, LEADING_BLOB_RADIUS);

    return oled_display_present();
}
