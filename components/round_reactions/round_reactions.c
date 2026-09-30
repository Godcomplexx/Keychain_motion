#include "round_reactions.h"

#include <stddef.h>

/* First-pass thresholds. The USB acceleration log lets us tune these on
 * the actual mounted MPU6050 instead of treating another PCB as identical. */
#define SMALL_MOVEMENT_G 0.08f
#define HANDLED_MOVEMENT_G 0.18f
#define JOLT_G 0.90f
#define TILT_ENTER_G 0.35f
#define TILT_LEAVE_G 0.18f
#define SHAKE_PEAK_GAP_MS 250U
#define SHAKE_WINDOW_MS 2500U

static float absolute(float value)
{
    return value < 0.0f ? -value : value;
}

void round_reactions_init(round_reactions_t *state, uint32_t now_ms)
{
    if (state == NULL) {
        return;
    }
    *state = (round_reactions_t){
        .last_handled_ms = now_ms,
        .last_any_motion_ms = now_ms,
        .last_curious_ms = now_ms - 3000U,
    };
}

round_gesture_t round_reactions_update(round_reactions_t *state,
                                       float x_g, float y_g, float z_g,
                                       uint32_t now_ms)
{
    if (state == NULL) {
        return ROUND_GESTURE_NONE;
    }
    if (!state->has_sample) {
        /* The first reading defines the baseline; it is not a gesture. */
        state->has_sample = true;
        state->previous_x = x_g;
        state->previous_y = y_g;
        state->previous_z = z_g;
        return ROUND_GESTURE_NONE;
    }

    const float delta = absolute(x_g - state->previous_x) +
                        absolute(y_g - state->previous_y) +
                        absolute(z_g - state->previous_z);
    state->previous_x = x_g;
    state->previous_y = y_g;
    state->previous_z = z_g;

    round_gesture_t tilt_event = ROUND_GESTURE_NONE;
    const int8_t old_zone = state->tilt_zone;
    /* Hysteresis keeps a held tilt from restarting its reaction each frame. */
    if (old_zone != 0 && absolute(x_g) < TILT_LEAVE_G) {
        state->tilt_zone = 0;
    } else if (x_g >= TILT_ENTER_G) {
        state->tilt_zone = 1;
    } else if (x_g <= -TILT_ENTER_G) {
        state->tilt_zone = -1;
    }
    if (state->tilt_zone != 0 && state->tilt_zone != old_zone) {
        tilt_event = state->tilt_zone < 0 ? ROUND_GESTURE_TILT_LEFT
                                           : ROUND_GESTURE_TILT_RIGHT;
    }

    const uint32_t quiet_ms = now_ms - state->last_handled_ms;
    const uint32_t still_ms = now_ms - state->last_any_motion_ms;
    if (delta >= SMALL_MOVEMENT_G) {
        state->last_any_motion_ms = now_ms;
    }
    if (delta >= HANDLED_MOVEMENT_G) {
        state->last_handled_ms = now_ms;
    }

    /* Three separate shocks within 2.5 s are an intentional shake. */
    if (delta >= JOLT_G &&
        (state->last_peak_ms == 0 ||
         (uint32_t)(now_ms - state->last_peak_ms) >= SHAKE_PEAK_GAP_MS)) {
        if (state->peak_count == 0 ||
            (uint32_t)(now_ms - state->peak_window_ms) > SHAKE_WINDOW_MS) {
            state->peak_count = 0;
            state->peak_window_ms = now_ms;
        }
        state->last_peak_ms = now_ms;
        if (++state->peak_count >= 3U) {
            state->peak_count = 0;
            return ROUND_GESTURE_SHAKE;
        }
        return ROUND_GESTURE_JOLT;
    }
    if (delta >= HANDLED_MOVEMENT_G && quiet_ms >= 3000U) {
        return ROUND_GESTURE_PICKED_UP;
    }
    if (delta >= SMALL_MOVEMENT_G && delta < HANDLED_MOVEMENT_G &&
        still_ms >= 2000U &&
        (uint32_t)(now_ms - state->last_curious_ms) >= 3000U) {
        state->last_curious_ms = now_ms;
        return ROUND_GESTURE_SMALL_MOVEMENT;
    }
    return tilt_event;
}

int8_t round_reactions_tilt_zone(const round_reactions_t *state)
{
    return state == NULL ? 0 : state->tilt_zone;
}

uint32_t round_reactions_still_ms(const round_reactions_t *state,
                                  uint32_t now_ms)
{
    return state == NULL ? 0 : now_ms - state->last_any_motion_ms;
}
