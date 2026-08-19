#ifndef XIAO_ESP_TIMER_COMPAT_H
#define XIAO_ESP_TIMER_COMPAT_H

#include <stdint.h>
#include <zephyr/kernel.h>

static inline int64_t esp_timer_get_time(void)
{
    /* Application timing is expressed in microseconds on both platforms. */
    return k_ticks_to_us_floor64(k_uptime_ticks());
}

#endif /* XIAO_ESP_TIMER_COMPAT_H */
