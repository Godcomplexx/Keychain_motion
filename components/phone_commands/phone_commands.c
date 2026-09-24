#include "phone_commands.h"

phone_command_plan_t phone_command_plan(phone_sync_command_t command)
{
    phone_command_plan_t plan = {
        .event = MOTION_EVENT_NONE,
        .shutdown_radio = false,
        .start_game = false,
        .start_particles = false,
    };

    switch (command) {
    case PHONE_SYNC_COMMAND_START_GAME:
        plan.event = MOTION_EVENT_GAME_REQUESTED;
        plan.shutdown_radio = true;
        plan.start_game = true;
        break;

    case PHONE_SYNC_COMMAND_SHOW_MESSAGE:
        plan.event = MOTION_EVENT_MESSAGE_REQUESTED;
        break;

    case PHONE_SYNC_COMMAND_START_FLUID:
        /* Particles play on the resting screen, so it has to be showing. */
        plan.event = MOTION_EVENT_WAKE_REQUESTED;
        plan.start_particles = true;
        break;

    case PHONE_SYNC_COMMAND_NONE:
    default:
        break;
    }

    return plan;
}
