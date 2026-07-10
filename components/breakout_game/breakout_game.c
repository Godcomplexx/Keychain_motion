#include "breakout_game.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "oled_display.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define HUD_HEIGHT 9
#define BRICK_COLUMNS 8
#define MAX_BRICK_ROWS 5
#define BRICK_X 2
#define BRICK_Y 10
#define BRICK_CELL_WIDTH 16
#define BRICK_CELL_HEIGHT 6
#define BRICK_WIDTH 14
#define BRICK_HEIGHT 4
#define PADDLE_WIDTH 22
#define PADDLE_HEIGHT 3
#define PADDLE_Y 59
#define BALL_RADIUS 1
#define INITIAL_LIVES 3
#define INACTIVITY_LIMIT_US 300000000LL
#define PHASE_MESSAGE_US 900000LL
#define GAME_OVER_MESSAGE_US 2000000LL
#define SERVE_DELAY_US 650000LL
#define MAX_FRAME_SECONDS 0.08f
#define PADDLE_SPEED 112.0f
#define CONTROL_DEAD_ZONE 0.035f
#define CONTROL_GAIN 1.18f
#define CONTROL_FILTER_OLD_WEIGHT 0.55f
#define CONTROL_FILTER_NEW_WEIGHT 0.45f

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

static uint32_t next_random(breakout_game_t *game)
{
    uint32_t value = game->random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    game->random_state = value != 0U ? value : 0x6D2B79F5U;
    return game->random_state;
}

static void draw_filled_rectangle(int x, int y, int width, int height)
{
    for (int py = y; py < y + height; ++py) {
        for (int px = x; px < x + width; ++px) {
            oled_display_set_pixel(px, py, true);
        }
    }
}

static void draw_horizontal_line(int y)
{
    for (int x = 0; x < SCREEN_WIDTH; ++x) {
        oled_display_set_pixel(x, y, true);
    }
}

static void generate_level(breakout_game_t *game)
{
    uint16_t extra_rows = game->level / 3U;
    if (extra_rows > 2U) {
        extra_rows = 2U;
    }
    game->brick_rows = (uint8_t)(3U + extra_rows);
    game->bricks_remaining = 0;

    for (uint8_t row = 0; row < MAX_BRICK_ROWS; ++row) {
        game->brick_masks[row] = 0;
        if (row >= game->brick_rows) {
            continue;
        }

        uint8_t mask = 0;
        for (uint8_t column = 0; column < BRICK_COLUMNS; ++column) {
            /*
             * Higher levels gradually remove fewer bricks, but each generated
             * row still has gaps so successive layouts remain visibly varied.
             */
            const uint32_t roll = next_random(game) % 100U;
            const uint32_t fill_threshold =
                game->level < 12U ? 68U + game->level * 2U : 92U;
            if (roll < fill_threshold) {
                mask |= (uint8_t)(1U << column);
            }
        }
        if (mask == 0U) {
            mask = (uint8_t)(1U << (next_random(game) % BRICK_COLUMNS));
        }
        game->brick_masks[row] = mask;
        game->bricks_remaining += (uint8_t)__builtin_popcount(mask);
    }
}

static float level_ball_speed(const breakout_game_t *game)
{
    const uint16_t capped_level = game->level < 20U ? game->level : 20U;
    return 29.0f + (float)capped_level * 1.25f;
}

static void reset_ball(breakout_game_t *game, int64_t now_us)
{
    const float speed = level_ball_speed(game);
    game->ball_x = game->paddle_x + PADDLE_WIDTH * 0.5f;
    game->ball_y = PADDLE_Y - BALL_RADIUS - 1;
    game->ball_vx = (next_random(game) & 1U) != 0U
                        ? speed * 0.62f
                        : -speed * 0.62f;
    game->ball_vy = -speed;
    game->serve_deadline_us = now_us + SERVE_DELAY_US;
}

static void begin_level(breakout_game_t *game, int64_t now_us)
{
    generate_level(game);
    game->paddle_x = (SCREEN_WIDTH - PADDLE_WIDTH) * 0.5f;
    reset_ball(game, now_us);
    game->phase = BREAKOUT_GAME_PLAYING;
}

void breakout_game_start(breakout_game_t *game,
                         int64_t now_us,
                         uint32_t random_seed,
                         float neutral_tilt_x)
{
    if (game == NULL) {
        return;
    }

    *game = (breakout_game_t){
        .lives = INITIAL_LIVES,
        .level = 1,
        .random_state = random_seed != 0U ? random_seed : 0xA341316CU,
        .neutral_tilt_x = neutral_tilt_x,
        .last_activity_us = now_us,
    };
    begin_level(game, now_us);
}

static bool ball_hits_brick(breakout_game_t *game,
                            float ball_x,
                            float ball_y)
{
    for (uint8_t row = 0; row < game->brick_rows; ++row) {
        const int brick_y = BRICK_Y + row * BRICK_CELL_HEIGHT;
        if (ball_y + BALL_RADIUS < brick_y ||
            ball_y - BALL_RADIUS >= brick_y + BRICK_HEIGHT) {
            continue;
        }

        for (uint8_t column = 0; column < BRICK_COLUMNS; ++column) {
            const uint8_t bit = (uint8_t)(1U << column);
            if ((game->brick_masks[row] & bit) == 0U) {
                continue;
            }

            const int brick_x = BRICK_X + column * BRICK_CELL_WIDTH;
            if (ball_x + BALL_RADIUS < brick_x ||
                ball_x - BALL_RADIUS >= brick_x + BRICK_WIDTH) {
                continue;
            }

            game->brick_masks[row] &= (uint8_t)~bit;
            --game->bricks_remaining;
            game->score += 10U * game->level;
            return true;
        }
    }
    return false;
}

static void update_paddle(breakout_game_t *game,
                          float tilt_x,
                          float frame_seconds,
                          int64_t now_us)
{
    float control = tilt_x - game->neutral_tilt_x;
    if (fabsf(control) < CONTROL_DEAD_ZONE) {
        control = 0.0f;
    } else {
        game->last_activity_us = now_us;
    }
    control = clamp_float(control * CONTROL_GAIN, -1.0f, 1.0f);
    game->filtered_control =
        game->filtered_control * CONTROL_FILTER_OLD_WEIGHT +
        control * CONTROL_FILTER_NEW_WEIGHT;
    game->paddle_x +=
        game->filtered_control * PADDLE_SPEED * frame_seconds;
    game->paddle_x =
        clamp_float(game->paddle_x, 1.0f,
                    (float)(SCREEN_WIDTH - PADDLE_WIDTH - 1));
}

static void lose_life(breakout_game_t *game, int64_t now_us)
{
    if (game->lives > 0U) {
        --game->lives;
    }
    if (game->lives == 0U) {
        game->phase = BREAKOUT_GAME_OVER;
        game->phase_deadline_us = now_us + GAME_OVER_MESSAGE_US;
        return;
    }
    reset_ball(game, now_us);
}

static void update_ball(breakout_game_t *game,
                        float frame_seconds,
                        int64_t now_us)
{
    if (now_us < game->serve_deadline_us) {
        game->ball_x = game->paddle_x + PADDLE_WIDTH * 0.5f;
        game->ball_y = PADDLE_Y - BALL_RADIUS - 1;
        return;
    }

    float next_x = game->ball_x + game->ball_vx * frame_seconds;
    float next_y = game->ball_y + game->ball_vy * frame_seconds;

    if (next_x - BALL_RADIUS <= 0.0f) {
        next_x = BALL_RADIUS;
        game->ball_vx = fabsf(game->ball_vx);
    } else if (next_x + BALL_RADIUS >= SCREEN_WIDTH - 1) {
        next_x = SCREEN_WIDTH - BALL_RADIUS - 1;
        game->ball_vx = -fabsf(game->ball_vx);
    }

    if (next_y - BALL_RADIUS <= HUD_HEIGHT) {
        next_y = HUD_HEIGHT + BALL_RADIUS;
        game->ball_vy = fabsf(game->ball_vy);
    }

    if (game->ball_vy > 0.0f &&
        next_y + BALL_RADIUS >= PADDLE_Y &&
        game->ball_y + BALL_RADIUS < PADDLE_Y &&
        next_x >= game->paddle_x - BALL_RADIUS &&
        next_x <= game->paddle_x + PADDLE_WIDTH + BALL_RADIUS) {
        const float paddle_center =
            game->paddle_x + PADDLE_WIDTH * 0.5f;
        const float hit_position =
            clamp_float((next_x - paddle_center) /
                            (PADDLE_WIDTH * 0.5f),
                        -1.0f, 1.0f);
        const float speed = level_ball_speed(game);
        game->ball_vx = hit_position * speed * 0.9f +
                        game->filtered_control * speed * 0.25f;
        game->ball_vx =
            clamp_float(game->ball_vx, -speed * 0.95f, speed * 0.95f);
        game->ball_vy = -speed;
        next_y = PADDLE_Y - BALL_RADIUS - 1;
    }

    if (game->ball_vy < 0.0f && ball_hits_brick(game, next_x, next_y)) {
        game->ball_vy = fabsf(game->ball_vy);
    } else if (game->ball_vy > 0.0f &&
               ball_hits_brick(game, next_x, next_y)) {
        game->ball_vy = -fabsf(game->ball_vy);
    }

    game->ball_x = next_x;
    game->ball_y = next_y;

    if (game->ball_y - BALL_RADIUS > SCREEN_HEIGHT) {
        lose_life(game, now_us);
    } else if (game->bricks_remaining == 0U) {
        game->phase = BREAKOUT_GAME_LEVEL_CLEAR;
        game->phase_deadline_us = now_us + PHASE_MESSAGE_US;
        ++game->level;
    }
}

static void draw_hud(const breakout_game_t *game)
{
    char level_text[8];
    char score_text[16];
    (void)snprintf(level_text, sizeof(level_text), "L%u",
                   (unsigned int)game->level);
    (void)snprintf(score_text, sizeof(score_text), "S%lu",
                   (unsigned long)game->score);
    oled_display_draw_text(1, 0, level_text);
    oled_display_draw_text(42, 0, score_text);

    for (uint8_t life = 0; life < game->lives; ++life) {
        draw_filled_rectangle(117 - life * 5, 2, 3, 3);
    }
    draw_horizontal_line(8);
}

static void draw_playfield(const breakout_game_t *game)
{
    for (uint8_t row = 0; row < game->brick_rows; ++row) {
        for (uint8_t column = 0; column < BRICK_COLUMNS; ++column) {
            if ((game->brick_masks[row] & (1U << column)) == 0U) {
                continue;
            }
            draw_filled_rectangle(BRICK_X + column * BRICK_CELL_WIDTH,
                                  BRICK_Y + row * BRICK_CELL_HEIGHT,
                                  BRICK_WIDTH,
                                  BRICK_HEIGHT);
        }
    }

    draw_filled_rectangle((int)game->paddle_x, PADDLE_Y,
                          PADDLE_WIDTH, PADDLE_HEIGHT);
    draw_filled_rectangle((int)game->ball_x - BALL_RADIUS,
                          (int)game->ball_y - BALL_RADIUS,
                          BALL_RADIUS * 2 + 1,
                          BALL_RADIUS * 2 + 1);
}

static esp_err_t render_game(const breakout_game_t *game)
{
    oled_display_clear();
    draw_hud(game);

    if (game->phase == BREAKOUT_GAME_PLAYING) {
        draw_playfield(game);
    } else if (game->phase == BREAKOUT_GAME_LEVEL_CLEAR) {
        char text[12];
        (void)snprintf(text, sizeof(text), "LEVEL %u",
                       (unsigned int)game->level);
        oled_display_draw_text(40, 29, text);
    } else {
        char score_text[16];
        oled_display_draw_text(55, 24, "END");
        (void)snprintf(score_text, sizeof(score_text), "S%lu",
                       (unsigned long)game->score);
        oled_display_draw_text(45, 36, score_text);
    }

    return oled_display_present();
}

esp_err_t breakout_game_update_and_render(breakout_game_t *game,
                                          float tilt_x,
                                          float frame_seconds,
                                          int64_t now_us,
                                          bool *finished)
{
    if (game == NULL || finished == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *finished = false;
    if (now_us - game->last_activity_us >= INACTIVITY_LIMIT_US &&
        game->phase != BREAKOUT_GAME_OVER) {
        game->phase = BREAKOUT_GAME_OVER;
        game->phase_deadline_us = now_us + GAME_OVER_MESSAGE_US;
    }

    const float safe_frame_seconds =
        clamp_float(frame_seconds, 0.0f, MAX_FRAME_SECONDS);

    if (game->phase == BREAKOUT_GAME_PLAYING) {
        update_paddle(game, tilt_x, safe_frame_seconds, now_us);
        update_ball(game, safe_frame_seconds, now_us);
    } else if (now_us >= game->phase_deadline_us) {
        if (game->phase == BREAKOUT_GAME_LEVEL_CLEAR) {
            begin_level(game, now_us);
        } else {
            *finished = true;
            return ESP_OK;
        }
    }

    return render_game(game);
}
