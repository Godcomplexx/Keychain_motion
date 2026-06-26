#ifndef DEVICE_CLOCK_H
#define DEVICE_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} device_clock_datetime_t;

typedef struct {
    device_clock_datetime_t build_datetime;
    int64_t started_at_us;
    int64_t last_synced_at_us;
    bool has_phone_sync;
} device_clock_t;

void device_clock_init(device_clock_t *clock, int64_t now_us);

void device_clock_get_datetime(const device_clock_t *clock,
                               int64_t now_us,
                               device_clock_datetime_t *datetime);

void device_clock_set_datetime(device_clock_t *clock,
                               const device_clock_datetime_t *datetime,
                               int64_t now_us);

bool device_clock_is_stale(const device_clock_t *clock, int64_t now_us);

bool device_clock_has_phone_sync(const device_clock_t *clock);

uint32_t device_clock_date_key(const device_clock_datetime_t *datetime);

#endif /* DEVICE_CLOCK_H */
