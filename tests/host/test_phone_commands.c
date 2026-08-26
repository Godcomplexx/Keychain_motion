#include <stdbool.h>
#include <stdio.h>

#include "motion_state.h"
#include "phone_commands.h"

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("FAIL line %d: %s\n", __LINE__, #condition);                \
            return false;                                                      \
        }                                                                      \
    } while (0)

/*
 * The sequence a person actually performs: start a game, then later open the
 * drawing pad and close it. Its awkward part is which commands take the radio
 * down and which must not - the game wants it off, the pad cannot work without
 * it - and that is precisely what is painful to check by hand on a
 * battery-powered keychain with one USB port.
 */
static bool test_game_then_draw(void)
{
    const phone_command_plan_t game =
        phone_command_plan(PHONE_SYNC_COMMAND_START_GAME);
    CHECK(game.event == MOTION_EVENT_GAME_REQUESTED);
    CHECK(game.start_game);
    CHECK(game.shutdown_radio);
    CHECK(!game.reset_canvas);

    const phone_command_plan_t open =
        phone_command_plan(PHONE_SYNC_COMMAND_START_DRAW);
    CHECK(open.event == MOTION_EVENT_DRAW_REQUESTED);
    /* Drawing over a radio that has just been shut down cannot work. */
    CHECK(!open.shutdown_radio);
    CHECK(open.reset_canvas);
    CHECK(!open.start_game);

    const phone_command_plan_t close =
        phone_command_plan(PHONE_SYNC_COMMAND_STOP_DRAW);
    CHECK(close.event == MOTION_EVENT_DRAW_FINISHED);
    CHECK(!close.shutdown_radio);
    CHECK(!close.reset_canvas);
    return true;
}

static bool test_clock_is_silent_about_the_radio(void)
{
    /*
     * Showing the clock used to drop the radio, so the next button press had
     * nowhere to arrive. The person is holding the phone and may press
     * something else immediately.
     */
    const phone_command_plan_t plan =
        phone_command_plan(PHONE_SYNC_COMMAND_SHOW_TIME);
    CHECK(plan.event == MOTION_EVENT_TIME_REQUESTED);
    CHECK(!plan.shutdown_radio);
    CHECK(!plan.start_game);
    CHECK(!plan.reset_canvas);
    return true;
}

/*
 * The particle field is the one command that reaches past the screen state
 * machine into the companion, and the one that has to wake the resting screen
 * to be seen at all.
 */
static bool test_particles_wake_the_resting_screen(void)
{
    const phone_command_plan_t plan =
        phone_command_plan(PHONE_SYNC_COMMAND_START_FLUID);
    CHECK(plan.event == MOTION_EVENT_WAKE_REQUESTED);
    CHECK(plan.start_particles);
    CHECK(!plan.shutdown_radio);
    CHECK(!plan.start_game);
    CHECK(!plan.reset_canvas);
    return true;
}

static bool test_no_command_does_nothing(void)
{
    const phone_command_plan_t plan =
        phone_command_plan(PHONE_SYNC_COMMAND_NONE);
    CHECK(plan.event == MOTION_EVENT_NONE);
    CHECK(!plan.shutdown_radio);
    CHECK(!plan.start_game);
    CHECK(!plan.reset_canvas);
    CHECK(!plan.start_particles);
    return true;
}

/* Exactly one command may take the radio down; the rest depend on it. */
static bool test_only_the_game_takes_the_radio(void)
{
    static const phone_sync_command_t commands[] = {
        PHONE_SYNC_COMMAND_NONE,
        PHONE_SYNC_COMMAND_START_GAME,
        PHONE_SYNC_COMMAND_SHOW_TIME,
        PHONE_SYNC_COMMAND_START_DRAW,
        PHONE_SYNC_COMMAND_STOP_DRAW,
        PHONE_SYNC_COMMAND_START_FLUID,
    };

    int shutdowns = 0;
    for (unsigned int index = 0;
         index < sizeof(commands) / sizeof(commands[0]); ++index) {
        if (phone_command_plan(commands[index]).shutdown_radio) {
            ++shutdowns;
        }
    }
    CHECK(shutdowns == 1);
    return true;
}

int main(void)
{
    CHECK(test_game_then_draw());
    CHECK(test_clock_is_silent_about_the_radio());
    CHECK(test_particles_wake_the_resting_screen());
    CHECK(test_no_command_does_nothing());
    CHECK(test_only_the_game_takes_the_radio());

    puts("PASS: phone_commands host tests");
    return 0;
}
