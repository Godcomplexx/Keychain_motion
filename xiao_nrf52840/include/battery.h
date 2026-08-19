#ifndef BATTERY_H
#define BATTERY_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * Battery gauge for the E73 keychain PCB.
 *
 * BAT_SENSE reaches U4 pin 7, which the EBYTE manual maps to P0.02 / AIN0. The
 * R17/R18 divider between the cell and that pin has no value printed in the
 * Gerber, so the ratio applied here is an assumption - see battery_zephyr.c.
 */

#define BATTERY_PERCENT_UNKNOWN 255U

typedef struct {
    /* Measured at the pin, before the divider is undone. Logged so the ratio
     * can be corrected from a single multimeter reading of the cell. */
    int32_t pin_millivolts;
    /* Pin reading scaled back up to the cell, using the assumed ratio. */
    int32_t cell_millivolts;
    uint8_t percent;
} battery_reading_t;

/* Returns ESP_ERR_NOT_SUPPORTED when this build has no ADC channel. */
esp_err_t battery_init(void);

bool battery_is_available(void);

/* One conversion. Returns false when no reading could be taken. */
bool battery_sample(battery_reading_t *reading);

#endif /* BATTERY_H */
