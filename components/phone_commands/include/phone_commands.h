#ifndef PHONE_COMMANDS_H
#define PHONE_COMMANDS_H

#include <stdbool.h>

#include "motion_state.h"
#include "phone_sync.h"

/*
 * What a command from the phone means, decided without doing any of it.
 *
 * This used to live inside the main loop as a chain of branches mixed in with
 * radio teardown, game startup and screen bookkeeping - the one part of that
 * loop that is pure decision and nothing else. Pulled out, it can be checked
 * on a host: the awkward cases are which commands take the radio down and
 * which must not, and those are exactly the ones that are painful to test by
 * hand on a battery-powered keychain.
 */
typedef struct {
    /* MOTION_EVENT_NONE when the command changes no screen. */
    motion_event_t event;
    /*
     * True only for the game, which needs the radio off and expects nothing
     * further. Drawing is the opposite: the radio is the whole point, since
     * every stroke arrives over it.
     */
    bool shutdown_radio;
    bool start_game;
    /* The drawing pad opens on a blank canvas rather than the last picture. */
    bool reset_canvas;
} phone_command_plan_t;

phone_command_plan_t phone_command_plan(phone_sync_command_t command);

#endif /* PHONE_COMMANDS_H */
