#ifndef PHONE_SYNC_H
#define PHONE_SYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "device_clock.h"
#include "esp_err.h"

typedef enum {
    PHONE_SYNC_COMMAND_NONE = 0,
    PHONE_SYNC_COMMAND_START_GAME,
    /*
     * The particle field, which the pet plays with rather than performs. It
     * has no other way in: the tilt gesture is switched off and the activity
     * scheduler never picks it, because a two-player game started alone would
     * be stranger than silence.
     */
    PHONE_SYNC_COMMAND_START_FLUID,
    /* A short note to put on the screen, sent as TEXT:<the note>. */
    PHONE_SYNC_COMMAND_SHOW_MESSAGE,
} phone_sync_command_t;

/*
 * What one command write can carry after the TEXT: prefix. Sized for a
 * forwarded phone notification (app + title + body), not just a hand-typed
 * note - the screen only ever shows the first ~57 characters of it anyway
 * (message_screen_render truncates rather than scrolling), so the rest
 * exists to be readable in the log, not on the panel.
 */
#define PHONE_SYNC_MESSAGE_MAX_LENGTH 140

esp_err_t phone_sync_init(void);

esp_err_t phone_sync_request_advertising(void);

void phone_sync_stop_advertising(void);

/* Stop and deinitialize NimBLE before entering light sleep. */
esp_err_t phone_sync_shutdown(void);

bool phone_sync_get_datetime_update(device_clock_datetime_t *datetime);

bool phone_sync_get_command(phone_sync_command_t *command);

/*
 * True once per phone connection, then false again. Distinct from a time sync:
 * the phone is simply here, which is worth greeting before it says anything.
 */
bool phone_sync_take_connected_event(void);

/*
 * Publishes the charge over the standard Battery Service. Values above 100 are
 * ignored rather than clamped: a reading that far out is a fault worth leaving
 * visible, not a number worth rounding.
 */
void phone_sync_publish_battery(uint8_t percent);

/*
 * Copies the note that arrived with the last SHOW_MESSAGE command. False when
 * none has arrived, in which case the buffer is left alone.
 */
bool phone_sync_get_message(char *out, size_t length);

#endif /* PHONE_SYNC_H */
