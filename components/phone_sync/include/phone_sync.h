#ifndef PHONE_SYNC_H
#define PHONE_SYNC_H

#include <stdbool.h>

#include "device_clock.h"
#include "esp_err.h"

typedef enum {
    PHONE_SYNC_COMMAND_NONE = 0,
    PHONE_SYNC_COMMAND_START_GAME,
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
