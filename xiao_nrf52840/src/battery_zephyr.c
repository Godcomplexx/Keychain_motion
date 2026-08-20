#include "battery.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>

#include "esp_log.h"

static const char *TAG = "battery";

#if DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)

/*
 * The Gerber carries no component values, so the R17/R18 ratio is unknown. A
 * 1:1 divider - the usual choice for a single-cell sense line - is assumed
 * here. Every sample also reports the raw pin voltage, so correcting this needs
 * exactly one multimeter measurement of CELL_PLUS:
 *
 *     BATTERY_DIVIDER_NUMERATOR = round(100 * V_cell / V_pin)
 */
#define BATTERY_DIVIDER_NUMERATOR 200
#define BATTERY_DIVIDER_DENOMINATOR 100

/*
 * How much the charger lifts the terminal voltage. Measured on this board: the
 * reading rose by 30 mV at the pin, so 60 mV at the cell, within seconds of the
 * cable going in - far too fast to be actual charge. Subtracting it turns the
 * reading back into something the discharge curve can be asked about.
 */
#define BATTERY_CHARGING_LIFT_MV 60

/* A LiPo discharge curve is far from linear; these are the usual landmarks. */
static const struct {
    int32_t millivolts;
    uint8_t percent;
} k_discharge_curve[] = {
    {4200, 100}, {4100, 90}, {4000, 80}, {3930, 70}, {3870, 60},
    {3820, 50},  {3790, 40}, {3770, 30}, {3730, 20}, {3680, 10},
    {3500, 5},   {3300, 0},
};

static const struct adc_dt_spec s_channel =
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
static bool s_available;

/*
 * Smoothed pin voltage. A raw reading walks around by a few millivolts, and the
 * discharge curve is steep enough around 3.7 V that this alone made the gauge
 * jump by ten percent between samples. Charging current lifts the terminal
 * voltage on top of that, so a voltage-only gauge reads high while charging -
 * smoothing hides the jitter, not that systematic error.
 */
static int32_t s_smoothed_millivolts;

static uint8_t percent_for(int32_t cell_millivolts)
{
    const size_t count = ARRAY_SIZE(k_discharge_curve);
    if (cell_millivolts >= k_discharge_curve[0].millivolts) {
        return k_discharge_curve[0].percent;
    }
    if (cell_millivolts <= k_discharge_curve[count - 1U].millivolts) {
        return k_discharge_curve[count - 1U].percent;
    }

    for (size_t index = 1U; index < count; ++index) {
        if (cell_millivolts < k_discharge_curve[index].millivolts) {
            continue;
        }
        /* Interpolate inside the bracket the reading landed in. */
        const int32_t high_mv = k_discharge_curve[index - 1U].millivolts;
        const int32_t low_mv = k_discharge_curve[index].millivolts;
        const int32_t high_pct = k_discharge_curve[index - 1U].percent;
        const int32_t low_pct = k_discharge_curve[index].percent;
        return (uint8_t)(low_pct + (high_pct - low_pct) *
                         (cell_millivolts - low_mv) / (high_mv - low_mv));
    }
    return k_discharge_curve[count - 1U].percent;
}

esp_err_t battery_init(void)
{
    if (!adc_is_ready_dt(&s_channel)) {
        ESP_LOGW(TAG, "ADC channel not ready; battery level stays unknown");
        return ESP_FAIL;
    }
    if (adc_channel_setup_dt(&s_channel) != 0) {
        ESP_LOGW(TAG, "ADC channel setup failed");
        return ESP_FAIL;
    }

    s_available = true;
    ESP_LOGI(TAG, "Battery sense ready on AIN0 (P0.02), assuming a %d/%d divider",
             BATTERY_DIVIDER_NUMERATOR, BATTERY_DIVIDER_DENOMINATOR);
    return ESP_OK;
}

bool battery_is_available(void)
{
    return s_available;
}

bool battery_sample(battery_reading_t *reading, bool charging)
{
    if (!s_available || reading == NULL) {
        return false;
    }

    int16_t sample = 0;
    struct adc_sequence sequence = {
        .buffer = &sample,
        .buffer_size = sizeof(sample),
    };
    if (adc_sequence_init_dt(&s_channel, &sequence) != 0 ||
        adc_read_dt(&s_channel, &sequence) != 0) {
        return false;
    }

    int32_t millivolts = sample;
    if (adc_raw_to_millivolts_dt(&s_channel, &millivolts) != 0) {
        return false;
    }

    if (s_smoothed_millivolts == 0) {
        s_smoothed_millivolts = millivolts;
    } else {
        s_smoothed_millivolts = (s_smoothed_millivolts * 3 + millivolts) / 4;
    }

    reading->pin_millivolts = millivolts;
    reading->cell_millivolts = (s_smoothed_millivolts *
                                BATTERY_DIVIDER_NUMERATOR) /
                               BATTERY_DIVIDER_DENOMINATOR;
    reading->resting_millivolts = reading->cell_millivolts -
                                  (charging ? BATTERY_CHARGING_LIFT_MV : 0);
    reading->percent = percent_for(reading->resting_millivolts);
    return true;
}

#else /* no ADC channel in this build */

esp_err_t battery_init(void)
{
    ESP_LOGI(TAG, "No battery sense channel in this build");
    return ESP_ERR_NOT_SUPPORTED;
}

bool battery_is_available(void)
{
    return false;
}

bool battery_sample(battery_reading_t *reading, bool charging)
{
    ARG_UNUSED(reading);
    ARG_UNUSED(charging);
    return false;
}

#endif
