#include "flip_animation.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "board_config.h"
#include "oled_display.h"

#define FLIP_PARTICLE_COLUMNS 20
#define FLIP_PARTICLE_ROWS 10
#define FLIP_PARTICLE_COUNT \
    (FLIP_PARTICLE_COLUMNS * FLIP_PARTICLE_ROWS)
#define FLIP_PARTICLE_SPACING 6.40f

#define FLIP_GRID_WIDTH 32
#define FLIP_GRID_HEIGHT 16
#define FLIP_GRID_CELL_COUNT (FLIP_GRID_WIDTH * FLIP_GRID_HEIGHT)
#define FLIP_CELL_WIDTH \
    ((float)BOARD_OLED_WIDTH / (float)FLIP_GRID_WIDTH)
#define FLIP_CELL_HEIGHT \
    ((float)BOARD_OLED_HEIGHT / (float)FLIP_GRID_HEIGHT)

#define FLIP_MIN_FRAME_SECONDS 0.005f
#define FLIP_MAX_FRAME_SECONDS 0.050f
#define FLIP_SUBSTEPS 1
#define FLIP_PRESSURE_ITERATIONS 8
#define FLIP_SEPARATION_ITERATIONS 1
#define FLIP_BLEND_RATIO 0.92f
#define FLIP_PRESSURE_OVER_RELAXATION 1.70f

#define FLIP_GRAVITY_PIXELS_PER_SECOND_SQUARED 360.0f
#define FLIP_PARTICLE_DAMPING 0.998f
#define FLIP_MAX_SPEED_PIXELS_PER_SECOND 300.0f
#define FLIP_PARTICLE_MIN_DISTANCE 4.00f
#define FLIP_RENDER_RADIUS_PIXELS 5
#define FLIP_WALL_MARGIN 0.50f
#define FLIP_WALL_TANGENTIAL_DAMPING 0.82f
#define FLIP_FULL_INTERPOLATION_WEIGHT 0.999f
#define FLIP_PROFILE_LOG_INTERVAL_US 2000000

typedef struct {
    float x;
    float y;
    float velocity_x;
    float velocity_y;
} flip_particle_t;

typedef struct {
    float velocity_x;
    float velocity_y;
    float previous_velocity_x;
    float previous_velocity_y;
    float weight;
    float pressure;
    float divergence;
    bool contains_fluid;
} flip_grid_cell_t;

_Static_assert(FLIP_PARTICLE_COUNT == 200,
               "Expected 20x10 particle grid");
_Static_assert(FLIP_GRID_CELL_COUNT == 512,
               "The FLIP prototype requires a 32 by 16 grid");

static const char *TAG = "flip_animation";
static flip_particle_t s_particles[FLIP_PARTICLE_COUNT];
static flip_grid_cell_t s_grid[FLIP_GRID_HEIGHT][FLIP_GRID_WIDTH];
static uint16_t s_bucket_counts[FLIP_GRID_CELL_COUNT];
static uint16_t s_bucket_starts[FLIP_GRID_CELL_COUNT + 1];
static uint16_t s_bucket_offsets[FLIP_GRID_CELL_COUNT];
static uint16_t s_bucket_particle_ids[FLIP_PARTICLE_COUNT];
static uint16_t s_fluid_cell_ids[FLIP_GRID_CELL_COUNT];
static uint16_t s_fluid_cell_count;
static bool s_prepared;

typedef struct {
    int64_t integrate_us;
    int64_t separate_us;
    int64_t particle_to_grid_us;
    int64_t pressure_us;
    int64_t grid_to_particle_us;
    int64_t draw_us;
    int64_t started_us;
    uint32_t frames;
} flip_profile_t;

static flip_profile_t s_profile;

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

static int clamp_int(int value, int minimum, int maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static int grid_index(int x, int y)
{
    return y * FLIP_GRID_WIDTH + x;
}

static int particle_grid_x(float x)
{
    return clamp_int((int)(x / FLIP_CELL_WIDTH), 0,
                     FLIP_GRID_WIDTH - 1);
}

static int particle_grid_y(float y)
{
    return clamp_int((int)(y / FLIP_CELL_HEIGHT), 0,
                     FLIP_GRID_HEIGHT - 1);
}

static void reset_particles(void)
{
    const float group_width =
        (float)(FLIP_PARTICLE_COLUMNS - 1) * FLIP_PARTICLE_SPACING;
    const float group_height =
        (float)(FLIP_PARTICLE_ROWS - 1) * FLIP_PARTICLE_SPACING;
    const float start_x = ((float)BOARD_OLED_WIDTH - group_width) * 0.5f;
    const float start_y = ((float)BOARD_OLED_HEIGHT - group_height) * 0.5f;

    size_t index = 0;
    for (int row = 0; row < FLIP_PARTICLE_ROWS; ++row) {
        for (int column = 0; column < FLIP_PARTICLE_COLUMNS; ++column) {
            flip_particle_t *particle = &s_particles[index++];
            particle->x = start_x +
                          (float)column * FLIP_PARTICLE_SPACING;
            particle->y = start_y +
                          (float)row * FLIP_PARTICLE_SPACING;
            particle->velocity_x = 0.0f;
            particle->velocity_y = 0.0f;
        }
    }
}

static bool particle_is_valid(const flip_particle_t *particle)
{
    return isfinite(particle->x) && isfinite(particle->y) &&
           isfinite(particle->velocity_x) &&
           isfinite(particle->velocity_y) &&
           particle->x >= 0.0f &&
           particle->x < (float)BOARD_OLED_WIDTH &&
           particle->y >= 0.0f &&
           particle->y < (float)BOARD_OLED_HEIGHT;
}

static esp_err_t validate_particles(void)
{
    for (size_t index = 0; index < FLIP_PARTICLE_COUNT; ++index) {
        if (!particle_is_valid(&s_particles[index])) {
            ESP_LOGE(TAG, "Particle %u has invalid state",
                     (unsigned int)index);
            return ESP_ERR_INVALID_STATE;
        }
    }
    return ESP_OK;
}

static void resolve_wall_collision(flip_particle_t *particle)
{
    const float minimum_x = FLIP_WALL_MARGIN;
    const float maximum_x =
        (float)(BOARD_OLED_WIDTH - 1) - FLIP_WALL_MARGIN;
    const float minimum_y = FLIP_WALL_MARGIN;
    const float maximum_y =
        (float)(BOARD_OLED_HEIGHT - 1) - FLIP_WALL_MARGIN;

    if (particle->x < minimum_x) {
        particle->x = minimum_x;
        if (particle->velocity_x < 0.0f) {
            particle->velocity_x = 0.0f;
        }
        particle->velocity_y *= FLIP_WALL_TANGENTIAL_DAMPING;
    } else if (particle->x > maximum_x) {
        particle->x = maximum_x;
        if (particle->velocity_x > 0.0f) {
            particle->velocity_x = 0.0f;
        }
        particle->velocity_y *= FLIP_WALL_TANGENTIAL_DAMPING;
    }

    if (particle->y < minimum_y) {
        particle->y = minimum_y;
        if (particle->velocity_y < 0.0f) {
            particle->velocity_y = 0.0f;
        }
        particle->velocity_x *= FLIP_WALL_TANGENTIAL_DAMPING;
    } else if (particle->y > maximum_y) {
        particle->y = maximum_y;
        if (particle->velocity_y > 0.0f) {
            particle->velocity_y = 0.0f;
        }
        particle->velocity_x *= FLIP_WALL_TANGENTIAL_DAMPING;
    }
}

static void integrate_particles(float tilt_x, float tilt_y,
                                float delta_seconds)
{
    const float gravity_x = clamp_float(tilt_x, -1.0f, 1.0f) *
                            FLIP_GRAVITY_PIXELS_PER_SECOND_SQUARED;
    const float gravity_y = clamp_float(tilt_y, -1.0f, 1.0f) *
                            FLIP_GRAVITY_PIXELS_PER_SECOND_SQUARED;

    for (size_t index = 0; index < FLIP_PARTICLE_COUNT; ++index) {
        flip_particle_t *particle = &s_particles[index];
        particle->velocity_x += gravity_x * delta_seconds;
        particle->velocity_y += gravity_y * delta_seconds;
        particle->velocity_x *= FLIP_PARTICLE_DAMPING;
        particle->velocity_y *= FLIP_PARTICLE_DAMPING;
        particle->velocity_x = clamp_float(
            particle->velocity_x,
            -FLIP_MAX_SPEED_PIXELS_PER_SECOND,
            FLIP_MAX_SPEED_PIXELS_PER_SECOND);
        particle->velocity_y = clamp_float(
            particle->velocity_y,
            -FLIP_MAX_SPEED_PIXELS_PER_SECOND,
            FLIP_MAX_SPEED_PIXELS_PER_SECOND);
        particle->x += particle->velocity_x * delta_seconds;
        particle->y += particle->velocity_y * delta_seconds;
        resolve_wall_collision(particle);
    }
}

static void rebuild_particle_buckets(void)
{
    memset(s_bucket_counts, 0, sizeof(s_bucket_counts));

    for (size_t index = 0; index < FLIP_PARTICLE_COUNT; ++index) {
        const int cell_x = particle_grid_x(s_particles[index].x);
        const int cell_y = particle_grid_y(s_particles[index].y);
        ++s_bucket_counts[grid_index(cell_x, cell_y)];
    }

    s_bucket_starts[0] = 0;
    for (int cell = 0; cell < FLIP_GRID_CELL_COUNT; ++cell) {
        s_bucket_starts[cell + 1] =
            (uint16_t)(s_bucket_starts[cell] + s_bucket_counts[cell]);
        s_bucket_offsets[cell] = s_bucket_starts[cell];
    }

    for (size_t index = 0; index < FLIP_PARTICLE_COUNT; ++index) {
        const int cell_x = particle_grid_x(s_particles[index].x);
        const int cell_y = particle_grid_y(s_particles[index].y);
        const int cell = grid_index(cell_x, cell_y);
        s_bucket_particle_ids[s_bucket_offsets[cell]++] = (uint16_t)index;
    }
}

static void separate_particle_pair(size_t first_index, size_t second_index)
{
    flip_particle_t *first = &s_particles[first_index];
    flip_particle_t *second = &s_particles[second_index];
    float difference_x = second->x - first->x;
    float difference_y = second->y - first->y;
    float distance_squared = difference_x * difference_x +
                             difference_y * difference_y;
    const float minimum_distance_squared =
        FLIP_PARTICLE_MIN_DISTANCE * FLIP_PARTICLE_MIN_DISTANCE;

    if (distance_squared >= minimum_distance_squared) {
        return;
    }

    if (distance_squared < 0.000001f) {
        difference_x = ((first_index + second_index) & 1U) == 0U
                           ? 0.001f
                           : 0.0f;
        difference_y = difference_x == 0.0f ? 0.001f : 0.0f;
        distance_squared = 0.000001f;
    }

    /* Fast reciprocal square root: avoids expensive sqrtf on ESP32-C3.
     * One Newton-Raphson iteration gives ~0.2% error — enough for separation. */
    union { float f; uint32_t i; } u = { .f = distance_squared };
    u.i = 0x5f3759dfu - (u.i >> 1);
    float inv_dist = u.f * (1.5f - 0.5f * distance_squared * u.f * u.f);

    /* correction = 0.5 * (min_dist - dist) / dist = 0.5 * (min_dist * inv_dist - 1) */
    const float correction = 0.5f * (FLIP_PARTICLE_MIN_DISTANCE * inv_dist - 1.0f);
    const float correction_x = difference_x * correction;
    const float correction_y = difference_y * correction;

    first->x -= correction_x;
    first->y -= correction_y;
    second->x += correction_x;
    second->y += correction_y;
    /* Wall clamping is done once per substep in integrate_particles; skip here. */
}

static void separate_particles_in_bucket(int cell)
{
    for (uint16_t first = s_bucket_starts[cell];
         first < s_bucket_starts[cell + 1];
         ++first) {
        for (uint16_t second = (uint16_t)(first + 1U);
             second < s_bucket_starts[cell + 1];
             ++second) {
            separate_particle_pair(s_bucket_particle_ids[first],
                                   s_bucket_particle_ids[second]);
        }
    }
}

static void separate_particles_between_buckets(int first_cell,
                                               int second_cell)
{
    for (uint16_t first = s_bucket_starts[first_cell];
         first < s_bucket_starts[first_cell + 1];
         ++first) {
        for (uint16_t second = s_bucket_starts[second_cell];
             second < s_bucket_starts[second_cell + 1];
             ++second) {
            separate_particle_pair(s_bucket_particle_ids[first],
                                   s_bucket_particle_ids[second]);
        }
    }
}

static void separate_particles(void)
{
    for (int iteration = 0;
         iteration < FLIP_SEPARATION_ITERATIONS;
         ++iteration) {
        rebuild_particle_buckets();

        for (int y = 0; y < FLIP_GRID_HEIGHT; ++y) {
            for (int x = 0; x < FLIP_GRID_WIDTH; ++x) {
                const int cell = grid_index(x, y);
                if (s_bucket_counts[cell] == 0U) {
                    continue;
                }

                /* Each unordered bucket pair is visited exactly once. */
                separate_particles_in_bucket(cell);
                if (x + 1 < FLIP_GRID_WIDTH) {
                    separate_particles_between_buckets(
                        cell, grid_index(x + 1, y));
                }
                if (y + 1 < FLIP_GRID_HEIGHT) {
                    if (x > 0) {
                        separate_particles_between_buckets(
                            cell, grid_index(x - 1, y + 1));
                    }
                    separate_particles_between_buckets(
                        cell, grid_index(x, y + 1));
                    if (x + 1 < FLIP_GRID_WIDTH) {
                        separate_particles_between_buckets(
                            cell, grid_index(x + 1, y + 1));
                    }
                }
            }
        }
    }
}

static void grid_add_velocity(int x, int y, float weight,
                              float velocity_x, float velocity_y)
{
    if (x < 0 || x >= FLIP_GRID_WIDTH ||
        y < 0 || y >= FLIP_GRID_HEIGHT || weight <= 0.0f) {
        return;
    }

    flip_grid_cell_t *cell = &s_grid[y][x];
    cell->velocity_x += velocity_x * weight;
    cell->velocity_y += velocity_y * weight;
    cell->weight += weight;
}

static void particle_to_grid(void)
{
    memset(s_grid, 0, sizeof(s_grid));
    s_fluid_cell_count = 0;

    for (size_t index = 0; index < FLIP_PARTICLE_COUNT; ++index) {
        const flip_particle_t *particle = &s_particles[index];
        const float grid_x = particle->x / FLIP_CELL_WIDTH;
        const float grid_y = particle->y / FLIP_CELL_HEIGHT;
        const int base_x = clamp_int((int)floorf(grid_x), 0,
                                     FLIP_GRID_WIDTH - 1);
        const int base_y = clamp_int((int)floorf(grid_y), 0,
                                     FLIP_GRID_HEIGHT - 1);
        const float fraction_x = grid_x - floorf(grid_x);
        const float fraction_y = grid_y - floorf(grid_y);
        const float left_weight = 1.0f - fraction_x;
        const float top_weight = 1.0f - fraction_y;

        grid_add_velocity(base_x, base_y,
                          left_weight * top_weight,
                          particle->velocity_x,
                          particle->velocity_y);
        grid_add_velocity(base_x + 1, base_y,
                          fraction_x * top_weight,
                          particle->velocity_x,
                          particle->velocity_y);
        grid_add_velocity(base_x, base_y + 1,
                          left_weight * fraction_y,
                          particle->velocity_x,
                          particle->velocity_y);
        grid_add_velocity(base_x + 1, base_y + 1,
                          fraction_x * fraction_y,
                          particle->velocity_x,
                          particle->velocity_y);

        s_grid[particle_grid_y(particle->y)]
              [particle_grid_x(particle->x)].contains_fluid = true;
    }

    for (int y = 0; y < FLIP_GRID_HEIGHT; ++y) {
        for (int x = 0; x < FLIP_GRID_WIDTH; ++x) {
            flip_grid_cell_t *cell = &s_grid[y][x];
            if (cell->weight > 0.0f) {
                /* One software division is cheaper than two on ESP32-C3. */
                const float inverse_weight = 1.0f / cell->weight;
                cell->velocity_x *= inverse_weight;
                cell->velocity_y *= inverse_weight;
            }
            cell->previous_velocity_x = cell->velocity_x;
            cell->previous_velocity_y = cell->velocity_y;
            if (cell->contains_fluid) {
                s_fluid_cell_ids[s_fluid_cell_count++] =
                    (uint16_t)grid_index(x, y);
            }
        }
    }
}

static float grid_pressure(int x, int y)
{
    if (x < 0 || x >= FLIP_GRID_WIDTH ||
        y < 0 || y >= FLIP_GRID_HEIGHT) {
        return 0.0f;
    }
    return s_grid[y][x].pressure;
}

static float grid_velocity_x(int x, int y)
{
    if (x < 0 || x >= FLIP_GRID_WIDTH ||
        y < 0 || y >= FLIP_GRID_HEIGHT) {
        return 0.0f;
    }
    return s_grid[y][x].velocity_x;
}

static float grid_velocity_y(int x, int y)
{
    if (x < 0 || x >= FLIP_GRID_WIDTH ||
        y < 0 || y >= FLIP_GRID_HEIGHT) {
        return 0.0f;
    }
    return s_grid[y][x].velocity_y;
}

static void compute_grid_divergence(void)
{
    for (uint16_t active = 0; active < s_fluid_cell_count; ++active) {
        const int cell_id = s_fluid_cell_ids[active];
        if (cell_id >= FLIP_GRID_CELL_COUNT) {
            continue;
        }
        const int x = cell_id % FLIP_GRID_WIDTH;
        const int y = cell_id / FLIP_GRID_WIDTH;
        flip_grid_cell_t *cell = &s_grid[y][x];
        const float horizontal =
            grid_velocity_x(x + 1, y) -
            grid_velocity_x(x - 1, y);
        const float vertical =
            grid_velocity_y(x, y + 1) -
            grid_velocity_y(x, y - 1);
        cell->divergence = 0.5f * (horizontal + vertical);
    }
}

static void solve_pressure(void)
{
    compute_grid_divergence();

    for (int iteration = 0;
         iteration < FLIP_PRESSURE_ITERATIONS;
         ++iteration) {
        for (uint16_t active = 0;
             active < s_fluid_cell_count;
             ++active) {
            const int cell_id = s_fluid_cell_ids[active];
            if (cell_id >= FLIP_GRID_CELL_COUNT) {
                continue;
            }
            const int x = cell_id % FLIP_GRID_WIDTH;
            const int y = cell_id / FLIP_GRID_WIDTH;
            flip_grid_cell_t *cell = &s_grid[y][x];

            float pressure_sum = 0.0f;
            int neighbor_count = 0;
            if (x > 0) {
                pressure_sum += grid_pressure(x - 1, y);
                ++neighbor_count;
            }
            if (x + 1 < FLIP_GRID_WIDTH) {
                pressure_sum += grid_pressure(x + 1, y);
                ++neighbor_count;
            }
            if (y > 0) {
                pressure_sum += grid_pressure(x, y - 1);
                ++neighbor_count;
            }
            if (y + 1 < FLIP_GRID_HEIGHT) {
                pressure_sum += grid_pressure(x, y + 1);
                ++neighbor_count;
            }

            if (neighbor_count > 0) {
                static const float inverse_neighbor_count[5] = {
                    0.0f, 1.0f, 0.5f, 0.33333334f, 0.25f,
                };
                const float target_pressure =
                    (pressure_sum - cell->divergence) *
                    inverse_neighbor_count[neighbor_count];
                cell->pressure +=
                    FLIP_PRESSURE_OVER_RELAXATION *
                    (target_pressure - cell->pressure);
            }
        }
    }

    for (uint16_t active = 0; active < s_fluid_cell_count; ++active) {
        const int cell_id = s_fluid_cell_ids[active];
        if (cell_id >= FLIP_GRID_CELL_COUNT) {
            continue;
        }
        const int x = cell_id % FLIP_GRID_WIDTH;
        const int y = cell_id / FLIP_GRID_WIDTH;
        flip_grid_cell_t *cell = &s_grid[y][x];

        cell->velocity_x -= 0.5f *
            (grid_pressure(x + 1, y) -
             grid_pressure(x - 1, y));
        cell->velocity_y -= 0.5f *
            (grid_pressure(x, y + 1) -
             grid_pressure(x, y - 1));

        if (x == 0 || x == FLIP_GRID_WIDTH - 1) {
            cell->velocity_x = 0.0f;
        }
        if (y == 0 || y == FLIP_GRID_HEIGHT - 1) {
            cell->velocity_y = 0.0f;
        }
    }
}

static void sample_grid_velocities(float particle_x, float particle_y,
                                   float *velocity_x, float *velocity_y,
                                   float *previous_velocity_x,
                                   float *previous_velocity_y)
{
    const float grid_x = particle_x / FLIP_CELL_WIDTH;
    const float grid_y = particle_y / FLIP_CELL_HEIGHT;
    const int base_x = clamp_int((int)floorf(grid_x), 0,
                                 FLIP_GRID_WIDTH - 1);
    const int base_y = clamp_int((int)floorf(grid_y), 0,
                                 FLIP_GRID_HEIGHT - 1);
    const float fraction_x = grid_x - floorf(grid_x);
    const float fraction_y = grid_y - floorf(grid_y);
    const float weights[4] = {
        (1.0f - fraction_x) * (1.0f - fraction_y),
        fraction_x * (1.0f - fraction_y),
        (1.0f - fraction_x) * fraction_y,
        fraction_x * fraction_y,
    };
    const int sample_x[4] = {base_x, base_x + 1,
                             base_x, base_x + 1};
    const int sample_y[4] = {base_y, base_y,
                             base_y + 1, base_y + 1};

    float result_x = 0.0f;
    float result_y = 0.0f;
    float previous_result_x = 0.0f;
    float previous_result_y = 0.0f;
    float total_weight = 0.0f;

    for (int sample = 0; sample < 4; ++sample) {
        const int x = sample_x[sample];
        const int y = sample_y[sample];
        if (x < 0 || x >= FLIP_GRID_WIDTH ||
            y < 0 || y >= FLIP_GRID_HEIGHT) {
            continue;
        }
        const flip_grid_cell_t *cell = &s_grid[y][x];
        result_x += cell->velocity_x * weights[sample];
        result_y += cell->velocity_y * weights[sample];
        previous_result_x +=
            cell->previous_velocity_x * weights[sample];
        previous_result_y +=
            cell->previous_velocity_y * weights[sample];
        total_weight += weights[sample];
    }

    /* Interior bilinear weights sum to one and need no normalization. Only
     * clipped samples at the right or bottom edge use a reciprocal. */
    if (total_weight > 0.0f &&
        total_weight < FLIP_FULL_INTERPOLATION_WEIGHT) {
        const float inverse_weight = 1.0f / total_weight;
        result_x *= inverse_weight;
        result_y *= inverse_weight;
        previous_result_x *= inverse_weight;
        previous_result_y *= inverse_weight;
    }
    *velocity_x = result_x;
    *velocity_y = result_y;
    *previous_velocity_x = previous_result_x;
    *previous_velocity_y = previous_result_y;
}

static void grid_to_particles(void)
{
    for (size_t index = 0; index < FLIP_PARTICLE_COUNT; ++index) {
        flip_particle_t *particle = &s_particles[index];
        float pic_x;
        float pic_y;
        float previous_x;
        float previous_y;
        /* Current and previous values use the same four cells and weights. */
        sample_grid_velocities(particle->x, particle->y,
                               &pic_x, &pic_y,
                               &previous_x, &previous_y);

        const float flip_x = particle->velocity_x +
                             (pic_x - previous_x);
        const float flip_y = particle->velocity_y +
                             (pic_y - previous_y);
        particle->velocity_x =
            (1.0f - FLIP_BLEND_RATIO) * pic_x +
            FLIP_BLEND_RATIO * flip_x;
        particle->velocity_y =
            (1.0f - FLIP_BLEND_RATIO) * pic_y +
            FLIP_BLEND_RATIO * flip_y;
        particle->velocity_x = clamp_float(
            particle->velocity_x,
            -FLIP_MAX_SPEED_PIXELS_PER_SECOND,
            FLIP_MAX_SPEED_PIXELS_PER_SECOND);
        particle->velocity_y = clamp_float(
            particle->velocity_y,
            -FLIP_MAX_SPEED_PIXELS_PER_SECOND,
            FLIP_MAX_SPEED_PIXELS_PER_SECOND);
        resolve_wall_collision(particle);
    }
}

static void simulate_substep(float tilt_x, float tilt_y,
                             float delta_seconds)
{
    int64_t started_us = esp_timer_get_time();
    integrate_particles(tilt_x, tilt_y, delta_seconds);
    s_profile.integrate_us += esp_timer_get_time() - started_us;

    started_us = esp_timer_get_time();
    separate_particles();
    s_profile.separate_us += esp_timer_get_time() - started_us;

    started_us = esp_timer_get_time();
    particle_to_grid();
    s_profile.particle_to_grid_us += esp_timer_get_time() - started_us;

    started_us = esp_timer_get_time();
    solve_pressure();
    s_profile.pressure_us += esp_timer_get_time() - started_us;

    started_us = esp_timer_get_time();
    grid_to_particles();
    s_profile.grid_to_particle_us += esp_timer_get_time() - started_us;
}

static void log_profile_if_due(void)
{
    const int64_t now_us = esp_timer_get_time();
    if (s_profile.started_us == 0) {
        s_profile.started_us = now_us;
        return;
    }
    if (s_profile.frames == 0U ||
        now_us - s_profile.started_us < FLIP_PROFILE_LOG_INTERVAL_US) {
        return;
    }

    ESP_LOGI(TAG,
             "FLIP avg us/frame: integrate=%lld separate=%lld "
             "p2g=%lld pressure=%lld g2p=%lld draw=%lld",
             (long long)(s_profile.integrate_us / s_profile.frames),
             (long long)(s_profile.separate_us / s_profile.frames),
             (long long)(s_profile.particle_to_grid_us / s_profile.frames),
             (long long)(s_profile.pressure_us / s_profile.frames),
             (long long)(s_profile.grid_to_particle_us / s_profile.frames),
             (long long)(s_profile.draw_us / s_profile.frames));

    memset(&s_profile, 0, sizeof(s_profile));
    s_profile.started_us = now_us;
}

static void draw_particle_sprite(int center_x, int center_y)
{
    /*
     * The physics particle is still one coordinate. The radius controls only
     * how large that coordinate appears on the 1-bit OLED framebuffer.
     */
    for (int y = center_y - FLIP_RENDER_RADIUS_PIXELS;
         y <= center_y + FLIP_RENDER_RADIUS_PIXELS;
         ++y) {
        for (int x = center_x - FLIP_RENDER_RADIUS_PIXELS;
             x <= center_x + FLIP_RENDER_RADIUS_PIXELS;
             ++x) {
            oled_display_set_pixel(x, y, true);
        }
    }
}

esp_err_t flip_animation_prepare(flip_animation_stats_t *stats)
{
    if (s_prepared) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(s_particles, 0, sizeof(s_particles));
    memset(s_grid, 0, sizeof(s_grid));
    reset_particles();

    esp_err_t err = validate_particles();
    if (err != ESP_OK) {
        return err;
    }

    s_prepared = true;

    if (stats != NULL) {
        stats->particle_count = FLIP_PARTICLE_COUNT;
        stats->grid_cell_count = FLIP_GRID_CELL_COUNT;
        stats->grid_width = FLIP_GRID_WIDTH;
        stats->grid_height = FLIP_GRID_HEIGHT;
        stats->static_memory_bytes =
            sizeof(s_particles) + sizeof(s_grid) +
            sizeof(s_bucket_counts) + sizeof(s_bucket_starts) +
            sizeof(s_bucket_offsets) + sizeof(s_bucket_particle_ids) +
            sizeof(s_fluid_cell_ids);
    }

    ESP_LOGI(TAG,
             "FLIP ready: particles=%u, grid=%ux%u, RAM=%u bytes",
             (unsigned int)FLIP_PARTICLE_COUNT,
             (unsigned int)FLIP_GRID_WIDTH,
             (unsigned int)FLIP_GRID_HEIGHT,
             (unsigned int)(sizeof(s_particles) + sizeof(s_grid) +
                            sizeof(s_bucket_counts) +
                            sizeof(s_bucket_starts) +
                            sizeof(s_bucket_offsets) +
                            sizeof(s_bucket_particle_ids) +
                            sizeof(s_fluid_cell_ids)));
    return ESP_OK;
}

void flip_animation_reset(void)
{
    reset_particles();
    memset(s_grid, 0, sizeof(s_grid));
    memset(&s_profile, 0, sizeof(s_profile));
}

esp_err_t flip_animation_render(float tilt_x, float tilt_y,
                                float delta_seconds)
{
    if (!s_prepared) {
        return ESP_ERR_INVALID_STATE;
    }

    const float safe_delta = clamp_float(delta_seconds,
                                          FLIP_MIN_FRAME_SECONDS,
                                          FLIP_MAX_FRAME_SECONDS);
    const float substep_seconds = safe_delta / (float)FLIP_SUBSTEPS;

    for (int step = 0; step < FLIP_SUBSTEPS; ++step) {
        simulate_substep(tilt_x, tilt_y, substep_seconds);
    }

    esp_err_t err = validate_particles();
    if (err != ESP_OK) {
        return err;
    }

    const int64_t draw_started_us = esp_timer_get_time();
    oled_display_clear();
    for (size_t index = 0; index < FLIP_PARTICLE_COUNT; ++index) {
        const int x = (int)(s_particles[index].x + 0.5f);
        const int y = (int)(s_particles[index].y + 0.5f);
        draw_particle_sprite(x, y);
    }
    esp_err_t present_err = oled_display_present();
    s_profile.draw_us += esp_timer_get_time() - draw_started_us;
    ++s_profile.frames;
    log_profile_if_due();
    return present_err;
}
