#include <assert.h>
#include <stdint.h>

#include "round_reactions.h"

int main(void)
{
    round_reactions_t state;
    round_reactions_init(&state, 0);
    /* A stationary start must not invent a gesture. */
    assert(round_reactions_update(&state, 0, 0, 1, 0) ==
           ROUND_GESTURE_NONE);
    assert(round_reactions_update(&state, 0, 0, 1, 3000) ==
           ROUND_GESTURE_NONE);

    /* A gentle lift after rest is happy; holding it tilted is one gaze. */
    assert(round_reactions_update(&state, 0.40f, 0, 0.92f, 3100) ==
           ROUND_GESTURE_PICKED_UP);
    assert(round_reactions_tilt_zone(&state) == 1);
    assert(round_reactions_update(&state, 0.40f, 0, 0.92f, 3200) ==
           ROUND_GESTURE_NONE);
    assert(round_reactions_update(&state, 0.0f, 0, 1.0f, 3300) ==
           ROUND_GESTURE_NONE);
    assert(round_reactions_tilt_zone(&state) == 0);
    assert(round_reactions_update(&state, -0.40f, 0, 0.92f, 3400) ==
           ROUND_GESTURE_TILT_LEFT);

    /* Small motion after quiet is curious, without counting as a pickup. */
    round_reactions_init(&state, 0);
    (void)round_reactions_update(&state, 0, 0, 1, 0);
    assert(round_reactions_update(&state, 0.10f, 0, 1, 2500) ==
           ROUND_GESTURE_SMALL_MOVEMENT);

    /* One shock is surprise; three separated shocks are a deliberate shake. */
    round_reactions_init(&state, 0);
    (void)round_reactions_update(&state, 0, 0, 1, 0);
    assert(round_reactions_update(&state, 1.0f, 0, 1, 500) ==
           ROUND_GESTURE_JOLT);
    assert(round_reactions_update(&state, 0, 0, 1, 800) ==
           ROUND_GESTURE_JOLT);
    assert(round_reactions_update(&state, 1.0f, 0, 1, 1100) ==
           ROUND_GESTURE_SHAKE);
    assert(round_reactions_still_ms(&state, 31000) >= 30000U);
    return 0;
}
