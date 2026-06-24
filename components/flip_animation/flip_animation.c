#include "flip_animation.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "esp_log.h"

#define FLIP_PARTICLE_COLUMNS 24
#define FLIP_PARTICLE_ROWS 12
#define FLIP_PARTICLE_COUNT \
    (FLIP_PARTICLE_COLUMNS * FLIP_PARTICLE_ROWS)
#define FLIP_PARTICLE_SPACING 1.5f

#define FLIP_GRID_WIDTH 32
#define FLIP_GRID_HEIGHT 16
#define FLIP_GRID_CELL_COUNT (FLIP_GRID_WIDTH * FLIP_GRID_HEIGHT)

#define FLIP_DOMAIN_WIDTH 128.0f
#define FLIP_DOMAIN_HEIGHT 64.0f

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

_Static_assert(FLIP_PARTICLE_COUNT == 288,
               "The FLIP experiment requires 288 particles");
_Static_assert(FLIP_GRID_CELL_COUNT == 512,
               "The FLIP experiment requires a 32 by 16 grid");

static const char *TAG = "flip_animation";
static flip_particle_t s_particles[FLIP_PARTICLE_COUNT];
static flip_grid_cell_t s_grid[FLIP_GRID_HEIGHT][FLIP_GRID_WIDTH];
static bool s_prepared;

static bool particle_is_inside_domain(const flip_particle_t *particle)
{
    return particle->x >= 0.0f && particle->x < FLIP_DOMAIN_WIDTH &&
           particle->y >= 0.0f && particle->y < FLIP_DOMAIN_HEIGHT;
}

static void reset_particles(void)
{
    const float group_width =
        (float)(FLIP_PARTICLE_COLUMNS - 1) * FLIP_PARTICLE_SPACING;
    const float group_height =
        (float)(FLIP_PARTICLE_ROWS - 1) * FLIP_PARTICLE_SPACING;
    const float start_x = (FLIP_DOMAIN_WIDTH - group_width) * 0.5f;
    const float start_y = (FLIP_DOMAIN_HEIGHT - group_height) * 0.5f;

    size_t index = 0;
    for (int row = 0; row < FLIP_PARTICLE_ROWS; ++row) {
        for (int column = 0; column < FLIP_PARTICLE_COLUMNS; ++column) {
            flip_particle_t *particle = &s_particles[index];
            particle->x = start_x +
                          (float)column * FLIP_PARTICLE_SPACING;
            particle->y = start_y +
                          (float)row * FLIP_PARTICLE_SPACING;
            particle->velocity_x = 0.0f;
            particle->velocity_y = 0.0f;
            ++index;
        }
    }
}

static esp_err_t validate_particles(void)
{
    for (size_t index = 0; index < FLIP_PARTICLE_COUNT; ++index) {
        if (!particle_is_inside_domain(&s_particles[index])) {
            ESP_LOGE(TAG, "Particle %u is outside the simulation domain",
                     (unsigned int)index);
            return ESP_ERR_INVALID_STATE;
        }
    }

    return ESP_OK;
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
        stats->static_memory_bytes = sizeof(s_particles) + sizeof(s_grid);
    }

    ESP_LOGI(TAG,
             "FLIP data model ready: particles=%u, grid=%ux%u, RAM=%u bytes",
             (unsigned int)FLIP_PARTICLE_COUNT,
             (unsigned int)FLIP_GRID_WIDTH,
             (unsigned int)FLIP_GRID_HEIGHT,
             (unsigned int)(sizeof(s_particles) + sizeof(s_grid)));
    return ESP_OK;
}
