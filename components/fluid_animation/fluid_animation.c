#include "fluid_animation.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"

#include "board_config.h"
#include "oled_display.h"

#define PARTICLE_COLUMNS 12
#define PARTICLE_ROWS 8
#define PARTICLE_COUNT (PARTICLE_COLUMNS * PARTICLE_ROWS)
#define PARTICLE_SPACING_PIXELS 2

#define TILT_DEAD_ZONE_G 0.05f
#define MAX_CELL_STEPS_PER_SECOND 50.0f
#define MAX_CELL_STEPS_PER_FRAME 3
#define MAX_FRAME_DELTA_SECONDS 0.05f

_Static_assert(PARTICLE_COUNT == 96,
               "The sand prototype must contain 96 particles");

static const char *TAG = "fluid_animation";

/* One occupied cell is one visible particle; overlap is impossible. */
static bool s_occupied[BOARD_OLED_HEIGHT][BOARD_OLED_WIDTH];
static float s_movement_accumulator;
static uint32_t s_update_number;

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

static float absolute_float(float value)
{
    return value < 0.0f ? -value : value;
}

static int direction_from_tilt(float tilt)
{
    if (tilt > TILT_DEAD_ZONE_G) {
        return 1;
    }
    if (tilt < -TILT_DEAD_ZONE_G) {
        return -1;
    }
    return 0;
}

static bool cell_is_inside_display(int x, int y)
{
    return x >= 0 && x < BOARD_OLED_WIDTH &&
           y >= 0 && y < BOARD_OLED_HEIGHT;
}

static bool move_to_free_cell(int source_x, int source_y,
                              int target_x, int target_y)
{
    if (!cell_is_inside_display(target_x, target_y) ||
        s_occupied[target_y][target_x]) {
        return false;
    }

    s_occupied[source_y][source_x] = false;
    s_occupied[target_y][target_x] = true;
    return true;
}

static bool try_alternative_cells(int x, int y,
                                  int direction_x, int direction_y)
{
    int first_x;
    int first_y;
    int second_x;
    int second_y;

    if (direction_x != 0 && direction_y != 0) {
        /* For diagonal tilt, try each component direction separately. */
        first_x = x + direction_x;
        first_y = y;
        second_x = x;
        second_y = y + direction_y;
    } else if (direction_x != 0) {
        /* A horizontally blocked grain may slide diagonally up or down. */
        first_x = x + direction_x;
        first_y = y - 1;
        second_x = x + direction_x;
        second_y = y + 1;
    } else {
        /* A vertically blocked grain may slide diagonally left or right. */
        first_x = x - 1;
        first_y = y + direction_y;
        second_x = x + 1;
        second_y = y + direction_y;
    }

    /* Alternating priority prevents a permanent left/right visual bias. */
    const bool reverse_order = ((x + y + (int)s_update_number) & 1) != 0;
    if (reverse_order) {
        if (move_to_free_cell(x, y, second_x, second_y)) {
            return true;
        }
        return move_to_free_cell(x, y, first_x, first_y);
    }

    if (move_to_free_cell(x, y, first_x, first_y)) {
        return true;
    }
    return move_to_free_cell(x, y, second_x, second_y);
}

static void advance_grid_once(int direction_x, int direction_y)
{
    const int x_start = direction_x > 0 ? BOARD_OLED_WIDTH - 1 : 0;
    const int x_end = direction_x > 0 ? -1 : BOARD_OLED_WIDTH;
    const int x_increment = direction_x > 0 ? -1 : 1;
    const int y_start = direction_y > 0 ? BOARD_OLED_HEIGHT - 1 : 0;
    const int y_end = direction_y > 0 ? -1 : BOARD_OLED_HEIGHT;
    const int y_increment = direction_y > 0 ? -1 : 1;

    /* Front-most cells move first, so one grain moves at most once per step. */
    for (int y = y_start; y != y_end; y += y_increment) {
        for (int x = x_start; x != x_end; x += x_increment) {
            if (!s_occupied[y][x]) {
                continue;
            }

            if (move_to_free_cell(x, y,
                                  x + direction_x,
                                  y + direction_y)) {
                continue;
            }

            try_alternative_cells(x, y, direction_x, direction_y);
        }
    }

    ++s_update_number;
}

static void advance_sand(float tilt_x, float tilt_y, float delta_seconds)
{
    if (delta_seconds <= 0.0f) {
        return;
    }

    const float safe_tilt_x = clamp_float(tilt_x, -1.0f, 1.0f);
    const float safe_tilt_y = clamp_float(tilt_y, -1.0f, 1.0f);
    const int direction_x = direction_from_tilt(safe_tilt_x);
    const int direction_y = direction_from_tilt(safe_tilt_y);

    if (direction_x == 0 && direction_y == 0) {
        return;
    }

    const float tilt_strength =
        absolute_float(safe_tilt_x) > absolute_float(safe_tilt_y)
            ? absolute_float(safe_tilt_x)
            : absolute_float(safe_tilt_y);
    const float safe_delta = clamp_float(delta_seconds, 0.0f,
                                          MAX_FRAME_DELTA_SECONDS);

    s_movement_accumulator +=
        tilt_strength * MAX_CELL_STEPS_PER_SECOND * safe_delta;

    int steps_this_frame = 0;
    while (s_movement_accumulator >= 1.0f &&
           steps_this_frame < MAX_CELL_STEPS_PER_FRAME) {
        advance_grid_once(direction_x, direction_y);
        s_movement_accumulator -= 1.0f;
        ++steps_this_frame;
    }
}

void fluid_animation_reset(void)
{
    memset(s_occupied, 0, sizeof(s_occupied));
    s_movement_accumulator = 0.0f;
    s_update_number = 0;

    const int grid_width =
        ((PARTICLE_COLUMNS - 1) * PARTICLE_SPACING_PIXELS) + 1;
    const int grid_height =
        ((PARTICLE_ROWS - 1) * PARTICLE_SPACING_PIXELS) + 1;
    const int start_x = (BOARD_OLED_WIDTH - grid_width) / 2;
    const int start_y = (BOARD_OLED_HEIGHT - grid_height) / 2;

    for (int row = 0; row < PARTICLE_ROWS; ++row) {
        for (int column = 0; column < PARTICLE_COLUMNS; ++column) {
            const int x = start_x + column * PARTICLE_SPACING_PIXELS;
            const int y = start_y + row * PARTICLE_SPACING_PIXELS;
            s_occupied[y][x] = true;
        }
    }

    ESP_LOGI(TAG, "Initialized %u unique sand cells in a %dx%d grid",
             (unsigned int)PARTICLE_COUNT,
             PARTICLE_COLUMNS,
             PARTICLE_ROWS);
}

esp_err_t fluid_animation_render(float tilt_x, float tilt_y,
                                 float delta_seconds)
{
    advance_sand(tilt_x, tilt_y, delta_seconds);
    oled_display_clear();

    for (int y = 0; y < BOARD_OLED_HEIGHT; ++y) {
        for (int x = 0; x < BOARD_OLED_WIDTH; ++x) {
            if (s_occupied[y][x]) {
                oled_display_set_pixel(x, y, true);
            }
        }
    }

    return oled_display_present();
}
