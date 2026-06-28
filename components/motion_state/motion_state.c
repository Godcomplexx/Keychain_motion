#include "motion_state.h"

#include <stddef.h>

#include "esp_log.h"

static const char *TAG = "motion_state";

static void transition_to(motion_state_machine_t *machine,
                          motion_state_t next_state,
                          motion_event_t event,
                          int64_t now_us)
{
    if (machine->current_state == next_state) {
        return;
    }

    ESP_LOGI(TAG, "%s -> %s by %s",
             motion_state_name(machine->current_state),
             motion_state_name(next_state),
             motion_event_name(event));

    machine->current_state = next_state;
    machine->entered_at_us = now_us;
}

void motion_state_init(motion_state_machine_t *machine, int64_t now_us)
{
    if (machine == NULL) {
        return;
    }

    /* FLUID is the normal active state after boot. */
    machine->current_state = MOTION_STATE_FLUID;
    machine->entered_at_us = now_us;
}

motion_state_t motion_state_get(const motion_state_machine_t *machine)
{
    if (machine == NULL) {
        return MOTION_STATE_FLUID;
    }

    return machine->current_state;
}

int64_t motion_state_entered_at_us(const motion_state_machine_t *machine)
{
    if (machine == NULL) {
        return 0;
    }

    return machine->entered_at_us;
}

motion_state_t motion_state_handle_event(motion_state_machine_t *machine,
                                         motion_event_t event,
                                         bool movement_recent,
                                         int64_t now_us)
{
    if (machine == NULL || event == MOTION_EVENT_NONE) {
        return motion_state_get(machine);
    }

    switch (machine->current_state) {
    case MOTION_STATE_FLUID:
        if (event == MOTION_EVENT_GAME_REQUESTED) {
            transition_to(machine, MOTION_STATE_GAME, event, now_us);
        } else if (event == MOTION_EVENT_TIME_REQUESTED) {
            transition_to(machine, MOTION_STATE_TIME, event, now_us);
        } else if (event == MOTION_EVENT_STILLNESS_TIMEOUT) {
            transition_to(machine, MOTION_STATE_SLEEP, event, now_us);
        }
        break;

    case MOTION_STATE_SLEEP:
        if (event == MOTION_EVENT_GAME_REQUESTED) {
            transition_to(machine, MOTION_STATE_GAME, event, now_us);
        } else if (event == MOTION_EVENT_TIME_REQUESTED) {
            transition_to(machine, MOTION_STATE_TIME, event, now_us);
        } else if (event == MOTION_EVENT_MOVEMENT_DETECTED) {
            transition_to(machine, MOTION_STATE_FLUID, event, now_us);
        }
        break;

    case MOTION_STATE_TIME:
        if (event == MOTION_EVENT_GAME_REQUESTED) {
            transition_to(machine, MOTION_STATE_GAME, event, now_us);
        } else if (event == MOTION_EVENT_TIME_TIMEOUT) {
            /* After TIME, choose the next state from recent real motion. */
            transition_to(machine,
                          movement_recent ? MOTION_STATE_FLUID
                                          : MOTION_STATE_SLEEP,
                          event,
                          now_us);
        }
        break;

    case MOTION_STATE_GAME:
        if (event == MOTION_EVENT_GAME_FINISHED) {
            transition_to(machine, MOTION_STATE_FLUID, event, now_us);
        }
        break;

    default:
        machine->current_state = MOTION_STATE_FLUID;
        machine->entered_at_us = now_us;
        break;
    }

    return machine->current_state;
}

const char *motion_state_name(motion_state_t state)
{
    switch (state) {
    case MOTION_STATE_FLUID:
        return "FLUID";
    case MOTION_STATE_SLEEP:
        return "SLEEP";
    case MOTION_STATE_TIME:
        return "TIME";
    case MOTION_STATE_GAME:
        return "GAME";
    default:
        return "UNKNOWN";
    }
}

const char *motion_event_name(motion_event_t event)
{
    switch (event) {
    case MOTION_EVENT_NONE:
        return "NONE";
    case MOTION_EVENT_MOVEMENT_DETECTED:
        return "MOVEMENT_DETECTED";
    case MOTION_EVENT_STILLNESS_TIMEOUT:
        return "STILLNESS_TIMEOUT";
    case MOTION_EVENT_SHAKE_DETECTED:
        return "SHAKE_DETECTED";
    case MOTION_EVENT_TIME_REQUESTED:
        return "TIME_REQUESTED";
    case MOTION_EVENT_TIME_TIMEOUT:
        return "TIME_TIMEOUT";
    case MOTION_EVENT_GAME_REQUESTED:
        return "GAME_REQUESTED";
    case MOTION_EVENT_GAME_FINISHED:
        return "GAME_FINISHED";
    default:
        return "UNKNOWN";
    }
}
