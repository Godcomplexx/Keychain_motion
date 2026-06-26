#include "device_clock.h"

#include <stdbool.h>
#include <stddef.h>

#define MICROSECONDS_PER_SECOND 1000000LL
#define HOURS_PER_DAY 24
#define MINUTES_PER_HOUR 60
#define SECONDS_PER_MINUTE 60
#define MONTHS_PER_YEAR 12
#define DEVICE_CLOCK_SYNC_MAX_AGE_US (24LL * 60LL * 60LL * MICROSECONDS_PER_SECOND)

static uint8_t parse_two_digits(const char *text)
{
    return (uint8_t)((text[0] - '0') * 10 + (text[1] - '0'));
}

static uint16_t parse_four_digits(const char *text)
{
    return (uint16_t)((text[0] - '0') * 1000 +
                      (text[1] - '0') * 100 +
                      (text[2] - '0') * 10 +
                      (text[3] - '0'));
}

static uint8_t parse_build_month(const char *month_text)
{
    static const char *const months[MONTHS_PER_YEAR] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };

    for (uint8_t index = 0; index < MONTHS_PER_YEAR; ++index) {
        const char *candidate = months[index];
        if (month_text[0] == candidate[0] &&
            month_text[1] == candidate[1] &&
            month_text[2] == candidate[2]) {
            return (uint8_t)(index + 1);
        }
    }

    return 1;
}

static uint8_t parse_build_day(const char *date_text)
{
    if (date_text[4] == ' ') {
        return (uint8_t)(date_text[5] - '0');
    }

    return parse_two_digits(&date_text[4]);
}

static bool is_leap_year(uint16_t year)
{
    if ((year % 400U) == 0U) {
        return true;
    }
    if ((year % 100U) == 0U) {
        return false;
    }

    return (year % 4U) == 0U;
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t normal_days[MONTHS_PER_YEAR] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31,
    };

    if (month == 2 && is_leap_year(year)) {
        return 29;
    }

    return normal_days[month - 1U];
}

static void add_days(device_clock_datetime_t *datetime, uint32_t days)
{
    while (days > 0U) {
        const uint8_t month_days =
            days_in_month(datetime->year, datetime->month);

        if (datetime->day < month_days) {
            ++datetime->day;
        } else {
            datetime->day = 1;
            if (datetime->month < MONTHS_PER_YEAR) {
                ++datetime->month;
            } else {
                datetime->month = 1;
                ++datetime->year;
            }
        }
        --days;
    }
}

static bool datetime_is_valid(const device_clock_datetime_t *datetime)
{
    if (datetime == NULL) {
        return false;
    }
    if (datetime->year < 2024U || datetime->year > 2099U) {
        return false;
    }
    if (datetime->month < 1U || datetime->month > MONTHS_PER_YEAR) {
        return false;
    }
    if (datetime->day < 1U ||
        datetime->day > days_in_month(datetime->year, datetime->month)) {
        return false;
    }
    if (datetime->hour >= HOURS_PER_DAY ||
        datetime->minute >= MINUTES_PER_HOUR ||
        datetime->second >= SECONDS_PER_MINUTE) {
        return false;
    }

    return true;
}

void device_clock_init(device_clock_t *clock, int64_t now_us)
{
    if (clock == NULL) {
        return;
    }

    /*
     * This is an MVP clock source. It starts from firmware build time;
     * later NTP or phone setup can replace this initialization.
     */
    clock->build_datetime.year = parse_four_digits(&__DATE__[7]);
    clock->build_datetime.month = parse_build_month(__DATE__);
    clock->build_datetime.day = parse_build_day(__DATE__);
    clock->build_datetime.hour = parse_two_digits(&__TIME__[0]);
    clock->build_datetime.minute = parse_two_digits(&__TIME__[3]);
    clock->build_datetime.second = parse_two_digits(&__TIME__[6]);
    clock->started_at_us = now_us;
    clock->last_synced_at_us = 0;
    clock->has_phone_sync = false;
}

void device_clock_get_datetime(const device_clock_t *clock,
                               int64_t now_us,
                               device_clock_datetime_t *datetime)
{
    if (clock == NULL || datetime == NULL) {
        return;
    }

    *datetime = clock->build_datetime;

    int64_t elapsed_seconds =
        (now_us - clock->started_at_us) / MICROSECONDS_PER_SECOND;
    if (elapsed_seconds < 0) {
        elapsed_seconds = 0;
    }

    uint32_t total_seconds =
        (uint32_t)datetime->second + (uint32_t)elapsed_seconds;
    datetime->second = (uint8_t)(total_seconds % SECONDS_PER_MINUTE);

    uint32_t total_minutes =
        (uint32_t)datetime->minute +
        total_seconds / SECONDS_PER_MINUTE;
    datetime->minute = (uint8_t)(total_minutes % MINUTES_PER_HOUR);

    uint32_t total_hours =
        (uint32_t)datetime->hour + total_minutes / MINUTES_PER_HOUR;
    datetime->hour = (uint8_t)(total_hours % HOURS_PER_DAY);

    add_days(datetime, total_hours / HOURS_PER_DAY);
}

void device_clock_set_datetime(device_clock_t *clock,
                               const device_clock_datetime_t *datetime,
                               int64_t now_us)
{
    if (clock == NULL || !datetime_is_valid(datetime)) {
        return;
    }

    /* The phone-provided time becomes the new base for the software clock. */
    clock->build_datetime = *datetime;
    clock->started_at_us = now_us;
    clock->last_synced_at_us = now_us;
    clock->has_phone_sync = true;
}

bool device_clock_is_stale(const device_clock_t *clock, int64_t now_us)
{
    if (clock == NULL || !clock->has_phone_sync) {
        return true;
    }

    return now_us - clock->last_synced_at_us >=
           DEVICE_CLOCK_SYNC_MAX_AGE_US;
}

bool device_clock_has_phone_sync(const device_clock_t *clock)
{
    return clock != NULL && clock->has_phone_sync;
}

uint32_t device_clock_date_key(const device_clock_datetime_t *datetime)
{
    if (datetime == NULL) {
        return 0;
    }

    return (uint32_t)datetime->year * 10000U +
           (uint32_t)datetime->month * 100U +
           (uint32_t)datetime->day;
}
