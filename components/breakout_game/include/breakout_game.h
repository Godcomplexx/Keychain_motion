#ifndef BREAKOUT_GAME_H
#define BREAKOUT_GAME_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    BREAKOUT_GAME_PLAYING = 0,
    BREAKOUT_GAME_LEVEL_CLEAR,
    BREAKOUT_GAME_OVER,
} breakout_game_phase_t;

typedef struct {
    uint8_t brick_masks[5];
    uint8_t brick_rows;
    uint8_t bricks_remaining;
    uint8_t lives;
    uint16_t level;
    uint32_t score;
    uint32_t random_state;
    float paddle_x;
    float ball_x;
    float ball_y;
    float ball_vx;
    float ball_vy;
    float neutral_tilt_x;
    float filtered_control;
    int64_t last_activity_us;
    int64_t phase_deadline_us;
    int64_t serve_deadline_us;
    breakout_game_phase_t phase;
} breakout_game_t;

void breakout_game_start(breakout_game_t *game,
                         int64_t now_us,
                         uint32_t random_seed,
                         float neutral_tilt_x);

/*
 * Advance and draw one frame. finished becomes true after game over or the
 * five minutes without paddle input, allowing the app to return to FLUID.
 */
esp_err_t breakout_game_update_and_render(breakout_game_t *game,
                                          float tilt_x,
                                          float frame_seconds,
                                          int64_t now_us,
                                          bool *finished);

#endif /* BREAKOUT_GAME_H */
