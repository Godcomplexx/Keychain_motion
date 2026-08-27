#include <stdbool.h>
#include <stdio.h>

#include "motion_state.h"

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("FAIL line %d: %s\n", __LINE__, #condition);                \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define DRIVE_AT 1000
#define EVENT_AT 5000

/* Everything the phone can ask for. The game is deliberately absent from the
 * receiving side below: starting it takes the radio down, so no further
 * command can arrive until it ends. */
static const motion_event_t COMMANDS[] = {
    MOTION_EVENT_TIME_REQUESTED,
    MOTION_EVENT_MESSAGE_REQUESTED,
    MOTION_EVENT_DRAW_REQUESTED,
    MOTION_EVENT_WAKE_REQUESTED,
    MOTION_EVENT_GAME_REQUESTED,
};

static const motion_state_t SCREENS[] = {
    MOTION_STATE_FLUID,
    MOTION_STATE_SLEEP,
    MOTION_STATE_TIME,
    MOTION_STATE_MESSAGE,
    MOTION_STATE_DRAW,
};

/* Puts the machine into a state by asking for it the way the phone would. */
static void drive_to(motion_state_machine_t *machine, motion_state_t state)
{
    motion_state_init(machine, DRIVE_AT);
    switch (state) {
    case MOTION_STATE_FLUID:
        break;
    case MOTION_STATE_SLEEP:
        (void)motion_state_handle_event(
            machine, MOTION_EVENT_STILLNESS_TIMEOUT, false, DRIVE_AT);
        break;
    case MOTION_STATE_TIME:
        (void)motion_state_handle_event(
            machine, MOTION_EVENT_TIME_REQUESTED, true, DRIVE_AT);
        break;
    case MOTION_STATE_MESSAGE:
        (void)motion_state_handle_event(
            machine, MOTION_EVENT_MESSAGE_REQUESTED, true, DRIVE_AT);
        break;
    case MOTION_STATE_DRAW:
        (void)motion_state_handle_event(
            machine, MOTION_EVENT_DRAW_REQUESTED, true, DRIVE_AT);
        break;
    default:
        break;
    }
}

/*
 * The property that matters: pressing something on the phone does something
 * now, rather than nothing until the screen times out on its own.
 *
 * "Something" is either a different screen or the same screen starting its
 * spell over. Both were missing in real cases - a second press of Show time
 * sat out the first fifteen seconds in silence, and a note or the particles
 * asked for while the drawing pad was open were dropped entirely.
 */
static bool test_every_screen_answers_every_command(void)
{
    for (unsigned int screen = 0;
         screen < sizeof(SCREENS) / sizeof(SCREENS[0]); ++screen) {
        for (unsigned int command = 0;
             command < sizeof(COMMANDS) / sizeof(COMMANDS[0]); ++command) {
            motion_state_machine_t machine;
            drive_to(&machine, SCREENS[screen]);

            const motion_state_t before = motion_state_get(&machine);
            const int64_t entered_before = motion_state_entered_at_us(&machine);

            const motion_state_t after = motion_state_handle_event(
                &machine, COMMANDS[command], true, EVENT_AT);
            const int64_t entered_after = motion_state_entered_at_us(&machine);

            /*
             * One command is legitimately a no-op. WAKE_REQUESTED asks for
             * the resting screen, and FLUID already is it - there is no
             * countdown to restart and nothing to switch to. The particles it
             * accompanies are started through the companion, not here.
             */
            const bool already_satisfied =
                before == MOTION_STATE_FLUID &&
                COMMANDS[command] == MOTION_EVENT_WAKE_REQUESTED;

            const bool answered = already_satisfied ||
                                  after != before ||
                                  entered_after != entered_before;
            if (!answered) {
                printf("FAIL: %s ignores %s\n",
                       motion_state_name(before),
                       motion_event_name(COMMANDS[command]));
                return false;
            }
        }
    }
    return true;
}

/* The game is the exception, and it is one on purpose. */
static bool test_the_game_is_left_alone(void)
{
    motion_state_machine_t machine;
    motion_state_init(&machine, DRIVE_AT);
    (void)motion_state_handle_event(&machine, MOTION_EVENT_GAME_REQUESTED,
                                    true, DRIVE_AT);
    CHECK(motion_state_get(&machine) == MOTION_STATE_GAME);

    /*
     * Nothing can arrive here anyway - the radio is down for the duration -
     * so the game only ends on its own terms. Shaking is what a person has
     * to reach for, and the adapter turns that into GAME_FINISHED.
     */
    (void)motion_state_handle_event(&machine, MOTION_EVENT_TIME_REQUESTED,
                                    true, EVENT_AT);
    CHECK(motion_state_get(&machine) == MOTION_STATE_GAME);

    (void)motion_state_handle_event(&machine, MOTION_EVENT_GAME_FINISHED,
                                    true, EVENT_AT);
    CHECK(motion_state_get(&machine) == MOTION_STATE_FLUID);
    return true;
}

/* Where a screen goes when its own time runs out depends on recent handling. */
static bool test_timeouts_follow_recent_handling(void)
{
    motion_state_machine_t machine;

    drive_to(&machine, MOTION_STATE_TIME);
    (void)motion_state_handle_event(&machine, MOTION_EVENT_TIME_TIMEOUT,
                                    true, EVENT_AT);
    CHECK(motion_state_get(&machine) == MOTION_STATE_FLUID);

    drive_to(&machine, MOTION_STATE_TIME);
    (void)motion_state_handle_event(&machine, MOTION_EVENT_TIME_TIMEOUT,
                                    false, EVENT_AT);
    CHECK(motion_state_get(&machine) == MOTION_STATE_SLEEP);

    drive_to(&machine, MOTION_STATE_MESSAGE);
    (void)motion_state_handle_event(&machine, MOTION_EVENT_MESSAGE_TIMEOUT,
                                    false, EVENT_AT);
    CHECK(motion_state_get(&machine) == MOTION_STATE_SLEEP);
    return true;
}

/* Being jostled must not close the pad: it is held while the other hand draws. */
static bool test_movement_does_not_end_drawing(void)
{
    motion_state_machine_t machine;
    drive_to(&machine, MOTION_STATE_DRAW);

    (void)motion_state_handle_event(&machine, MOTION_EVENT_MOVEMENT_DETECTED,
                                    true, EVENT_AT);
    CHECK(motion_state_get(&machine) == MOTION_STATE_DRAW);

    (void)motion_state_handle_event(&machine, MOTION_EVENT_STILLNESS_TIMEOUT,
                                    false, EVENT_AT);
    CHECK(motion_state_get(&machine) == MOTION_STATE_DRAW);
    return true;
}

int main(void)
{
    CHECK(test_every_screen_answers_every_command());
    CHECK(test_the_game_is_left_alone());
    CHECK(test_timeouts_follow_recent_handling());
    CHECK(test_movement_does_not_end_drawing());

    puts("PASS: motion_state host tests");
    return 0;
}
