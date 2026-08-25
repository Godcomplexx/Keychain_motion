#ifndef PHONE_SYNC_H
#define PHONE_SYNC_H

#include <stdbool.h>

#include "device_clock.h"
#include "esp_err.h"

typedef enum {
    PHONE_SYNC_COMMAND_NONE = 0,
    PHONE_SYNC_COMMAND_START_GAME,
    /*
     * Setting the clock and putting the clock on screen are separate things.
     * A background sync should correct the time without taking the display
     * away from whatever the companion was doing; showing it is a request the
     * person makes, so it has a command of its own.
     */
    PHONE_SYNC_COMMAND_SHOW_TIME,
} phone_sync_command_t;

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

#endif /* PHONE_SYNC_H */
