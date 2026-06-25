#ifndef PHONE_SYNC_H
#define PHONE_SYNC_H

#include <stdbool.h>

#include "device_clock.h"
#include "esp_err.h"

esp_err_t phone_sync_init(void);

void phone_sync_request_advertising(void);

void phone_sync_stop_advertising(void);

bool phone_sync_get_datetime_update(device_clock_datetime_t *datetime);

#endif /* PHONE_SYNC_H */
