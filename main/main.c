#include <stdbool.h>
#include <stdint.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "adxl345.h"
#include "device_clock.h"
#include "flip_animation.h"
#include "i2c_bus.h"
#include "idle_animation.h"
#include "motion_detector.h"
#include "motion_state.h"
#include "oled_display.h"
#include "phone_sync.h"
#include "step_counter.h"
#include "time_animation.h"

#define ACTIVE_FRAME_DELAY_MS 5
#define STARTUP_SCREEN_DELAY_MS 1000
#define MICROSECONDS_PER_SECOND 1000000.0f
#define RAW_LOG_INTERVAL_FRAMES 25
#define SLEEP_STILLNESS_TIMEOUT_US 60000000
#define SLEEP_FRAME_INTERVAL_US 300000
#define TIME_FRAME_INTERVAL_US 250000
#define TIME_STATE_DURATION_US 60000000
#define LOW_POWER_SENSOR_READ_INTERVAL_US 150000
#define LOW_POWER_LOOP_DELAY_MS 50
#define MOTION_MOVEMENT_DELTA_THRESHOLD 80
#define MOTION_SHAKE_DELTA_THRESHOLD 500
#define OLED_ACTIVE_CONTRAST 0xCF
#define OLED_IDLE_CONTRAST 0x18

static const char *TAG = "keychain";

static esp_err_t render_sleep_frame_if_due(int64_t now_us,
                                           int64_t *previous_sleep_frame_us,
                                           uint8_t *sleep_frame_index)
{
    if (now_us - *previous_sleep_frame_us < SLEEP_FRAME_INTERVAL_US) {
        return ESP_OK;
    }

    esp_err_t err = idle_animation_render_frame(*sleep_frame_index);
    if (err != ESP_OK) {
        return err;
    }

    ++(*sleep_frame_index);
    *previous_sleep_frame_us = now_us;
    return ESP_OK;
}

static esp_err_t render_time_frame_if_due(int64_t now_us,
                                          int64_t entered_at_us,
                                          int64_t *previous_time_frame_us,
                                          const device_clock_t *device_clock,
                                          const step_counter_t *step_counter)
{
    if (now_us - *previous_time_frame_us < TIME_FRAME_INTERVAL_US) {
        return ESP_OK;
    }

    device_clock_datetime_t datetime;
    device_clock_get_datetime(device_clock, now_us, &datetime);

    const time_animation_view_t view = {
        .year = datetime.year,
        .month = datetime.month,
        .day = datetime.day,
        .hour = datetime.hour,
        .minute = datetime.minute,
        .second = datetime.second,
        .steps_today = step_counter_get_steps_today(step_counter),
    };

    *previous_time_frame_us = now_us;
    return time_animation_render(&view,
                                 now_us - entered_at_us,
                                 TIME_STATE_DURATION_US);
}

static esp_err_t apply_state_entry_actions(motion_state_t state,
                                           int64_t now_us,
                                           int64_t *previous_frame_us,
                                           int64_t *previous_sleep_frame_us,
                                           uint8_t *sleep_frame_index,
                                           int64_t *previous_time_frame_us)
{
    switch (state) {
    case MOTION_STATE_FLUID:
        /* Active animation should use normal brightness and fresh physics. */
        ESP_RETURN_ON_ERROR(oled_display_set_contrast(OLED_ACTIVE_CONTRAST),
                            TAG, "OLED active contrast setup failed");
        flip_animation_reset();
        *previous_frame_us = now_us;
        break;

    case MOTION_STATE_SLEEP:
        /* Sleep animation stays visible but dimmer and slower. */
        ESP_RETURN_ON_ERROR(oled_display_set_contrast(OLED_IDLE_CONTRAST),
                            TAG, "OLED sleep contrast setup failed");
        *sleep_frame_index = 0;
        *previous_sleep_frame_us = 0;
        break;

    case MOTION_STATE_TIME:
        /* TIME is a visible interaction state, so restore normal contrast. */
        ESP_RETURN_ON_ERROR(oled_display_set_contrast(OLED_ACTIVE_CONTRAST),
                            TAG, "OLED time contrast setup failed");
        *previous_time_frame_us = 0;
        break;

    default:
        break;
    }

    ESP_LOGI(TAG, "Entered %s state", motion_state_name(state));
    return ESP_OK;
}

static motion_event_t detector_result_to_event(
    const motion_detector_result_t *result)
{
    if (result->shake_detected) {
        return MOTION_EVENT_SHAKE_DETECTED;
    }
    if (result->movement_detected) {
        return MOTION_EVENT_MOVEMENT_DETECTED;
    }
    if (result->stillness_timeout) {
        return MOTION_EVENT_STILLNESS_TIMEOUT;
    }

    return MOTION_EVENT_NONE;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Smart Motion Keychain started");

    /* Prepare the FLIP particle arrays before the render loop uses them. */
    flip_animation_stats_t flip_stats;
    esp_err_t err = flip_animation_prepare(&flip_stats);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FLIP data-model preparation failed: %s",
                 esp_err_to_name(err));
        return;
    }

    /* Verify the I2C address before the driver reads ADXL345 registers. */
    err = i2c_bus_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C initialization failed: %s", esp_err_to_name(err));
        return;
    }

    err = i2c_bus_scan();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C scan failed: %s", esp_err_to_name(err));
        i2c_bus_deinit();
        return;
    }

    err = adxl345_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADXL345 initialization failed: %s",
                 esp_err_to_name(err));
        i2c_bus_deinit();
        return;
    }

    err = oled_display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED initialization failed: %s", esp_err_to_name(err));
        adxl345_deinit();
        i2c_bus_deinit();
        return;
    }

    err = oled_display_set_contrast(OLED_ACTIVE_CONTRAST);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED contrast setup failed: %s",
                 esp_err_to_name(err));
        oled_display_deinit();
        adxl345_deinit();
        i2c_bus_deinit();
        return;
    }

    err = oled_display_show_startup();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED drawing failed: %s", esp_err_to_name(err));
        oled_display_deinit();
        adxl345_deinit();
        i2c_bus_deinit();
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(STARTUP_SCREEN_DELAY_MS));
    flip_animation_reset();
    ESP_LOGI(TAG, "Starting ADXL345-controlled FLIP particle animation");

    uint32_t frames_until_log = 0;
    int64_t previous_frame_us = esp_timer_get_time();
    int64_t previous_sleep_frame_us = 0;
    int64_t previous_time_frame_us = 0;
    int64_t previous_low_power_sensor_read_us = 0;
    uint8_t sleep_frame_index = 0;
    device_clock_t device_clock;
    motion_detector_t motion_detector;
    motion_state_machine_t motion_state;
    step_counter_t step_counter;

    device_clock_init(&device_clock, previous_frame_us);
    device_clock_datetime_t datetime;
    device_clock_get_datetime(&device_clock, previous_frame_us, &datetime);
    step_counter_init(&step_counter, device_clock_date_key(&datetime));

    err = phone_sync_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Phone sync BLE disabled: %s", esp_err_to_name(err));
    }

    motion_detector_init(&motion_detector,
                         previous_frame_us,
                         MOTION_MOVEMENT_DELTA_THRESHOLD,
                         MOTION_SHAKE_DELTA_THRESHOLD,
                         SLEEP_STILLNESS_TIMEOUT_US);
    motion_state_init(&motion_state, previous_frame_us);

    while (true) {
        const int64_t current_frame_us = esp_timer_get_time();
        const float frame_seconds =
            (float)(current_frame_us - previous_frame_us) /
            MICROSECONDS_PER_SECOND;
        previous_frame_us = current_frame_us;
        motion_state_t current_state = motion_state_get(&motion_state);

        if (device_clock_is_stale(&device_clock, current_frame_us)) {
            phone_sync_request_advertising();
        }

        device_clock_datetime_t phone_datetime;
        if (phone_sync_get_datetime_update(&phone_datetime)) {
            device_clock_set_datetime(&device_clock,
                                      &phone_datetime,
                                      current_frame_us);
            if (step_counter_get_date_key(&step_counter) !=
                device_clock_date_key(&phone_datetime)) {
                step_counter_init(&step_counter,
                                  device_clock_date_key(&phone_datetime));
            }
            phone_sync_stop_advertising();
            ESP_LOGI(TAG,
                     "Clock synced from phone: %04u-%02u-%02u %02u:%02u:%02u",
                     (unsigned int)phone_datetime.year,
                     (unsigned int)phone_datetime.month,
                     (unsigned int)phone_datetime.day,
                     (unsigned int)phone_datetime.hour,
                     (unsigned int)phone_datetime.minute,
                     (unsigned int)phone_datetime.second);

            const motion_state_t previous_state = current_state;
            current_state = motion_state_handle_event(&motion_state,
                                                      MOTION_EVENT_TIME_REQUESTED,
                                                      true,
                                                      current_frame_us);
            if (current_state != previous_state) {
                err = apply_state_entry_actions(current_state,
                                                current_frame_us,
                                                &previous_frame_us,
                                                &previous_sleep_frame_us,
                                                &sleep_frame_index,
                                                &previous_time_frame_us);
                if (err != ESP_OK) {
                    break;
                }
            } else if (current_state == MOTION_STATE_TIME) {
                previous_time_frame_us = 0;
            }
        }

        if (current_state == MOTION_STATE_TIME &&
            current_frame_us - motion_state_entered_at_us(&motion_state) >=
            TIME_STATE_DURATION_US) {
            const bool movement_recent =
                motion_detector_has_recent_movement(&motion_detector,
                                                    current_frame_us);
            motion_state_handle_event(&motion_state,
                                      MOTION_EVENT_TIME_TIMEOUT,
                                      movement_recent,
                                      current_frame_us);
            current_state = motion_state_get(&motion_state);
            err = apply_state_entry_actions(current_state,
                                            current_frame_us,
                                            &previous_frame_us,
                                            &previous_sleep_frame_us,
                                            &sleep_frame_index,
                                            &previous_time_frame_us);
            if (err != ESP_OK) {
                break;
            }
        }

        if ((current_state == MOTION_STATE_SLEEP ||
             current_state == MOTION_STATE_TIME) &&
            current_frame_us - previous_low_power_sensor_read_us <
            LOW_POWER_SENSOR_READ_INTERVAL_US) {
            if (current_state == MOTION_STATE_SLEEP) {
                err = render_sleep_frame_if_due(current_frame_us,
                                                &previous_sleep_frame_us,
                                                &sleep_frame_index);
            } else {
                err = render_time_frame_if_due(
                    current_frame_us,
                    motion_state_entered_at_us(&motion_state),
                    &previous_time_frame_us,
                    &device_clock,
                    &step_counter);
            }

            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s rendering failed: %s",
                         motion_state_name(current_state),
                         esp_err_to_name(err));
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(LOW_POWER_LOOP_DELAY_MS));
            continue;
        }

        adxl345_raw_data_t raw_data;
        err = adxl345_read_raw(&raw_data);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ADXL345 read failed: %s", esp_err_to_name(err));
            break;
        }
        if (current_state != MOTION_STATE_FLUID) {
            previous_low_power_sensor_read_us = current_frame_us;
        }
        device_clock_get_datetime(&device_clock, current_frame_us, &datetime);
        step_counter_update(&step_counter,
                            &raw_data,
                            device_clock_date_key(&datetime),
                            current_frame_us);

        const motion_detector_result_t detector_result =
            motion_detector_update(&motion_detector, &raw_data,
                                   current_frame_us);
        const motion_event_t event =
            detector_result_to_event(&detector_result);

        if (event != MOTION_EVENT_NONE) {
            const bool movement_recent =
                motion_detector_has_recent_movement(&motion_detector,
                                                    current_frame_us);
            const motion_state_t previous_state = current_state;

            current_state = motion_state_handle_event(&motion_state,
                                                      event,
                                                      movement_recent,
                                                      current_frame_us);
            if (current_state != previous_state) {
                err = apply_state_entry_actions(current_state,
                                                current_frame_us,
                                                &previous_frame_us,
                                                &previous_sleep_frame_us,
                                                &sleep_frame_index,
                                                &previous_time_frame_us);
                if (err != ESP_OK) {
                    break;
                }
            }
        }

        const float tilt_x = adxl345_raw_to_g(raw_data.x);
        const float tilt_y = adxl345_raw_to_g(raw_data.y);

        if (frames_until_log == 0) {
            const int tilt_x_milligravity = (int)(tilt_x * 1000.0f);
            const int tilt_y_milligravity = (int)(tilt_y * 1000.0f);
            ESP_LOGI(TAG,
                     "State=%s event=%s steps=%lu delta=%d still=%lld ms raw: X=%d (%d mg), Y=%d (%d mg), Z=%d",
                     motion_state_name(current_state),
                     motion_event_name(event),
                     (unsigned long)step_counter_get_steps_today(&step_counter),
                     detector_result.raw_delta,
                     detector_result.stillness_us / 1000,
                     raw_data.x, tilt_x_milligravity,
                     raw_data.y, tilt_y_milligravity,
                     raw_data.z);
            frames_until_log = RAW_LOG_INTERVAL_FRAMES;
        }
        --frames_until_log;

        switch (current_state) {
        case MOTION_STATE_FLUID:
            err = flip_animation_render(tilt_x, tilt_y, frame_seconds);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Particle rendering failed: %s",
                         esp_err_to_name(err));
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(ACTIVE_FRAME_DELAY_MS));
            break;

        case MOTION_STATE_SLEEP:
            err = render_sleep_frame_if_due(current_frame_us,
                                            &previous_sleep_frame_us,
                                            &sleep_frame_index);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Sleep rendering failed: %s",
                         esp_err_to_name(err));
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(LOW_POWER_LOOP_DELAY_MS));
            break;

        case MOTION_STATE_TIME:
            err = render_time_frame_if_due(
                current_frame_us,
                motion_state_entered_at_us(&motion_state),
                &previous_time_frame_us,
                &device_clock,
                &step_counter);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Time rendering failed: %s",
                         esp_err_to_name(err));
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(LOW_POWER_LOOP_DELAY_MS));
            break;

        default:
            ESP_LOGE(TAG, "Unknown motion state");
            err = ESP_ERR_INVALID_STATE;
            break;
        }

        if (err != ESP_OK) {
            break;
        }
    }

    oled_display_deinit();
    adxl345_deinit();
    i2c_bus_deinit();
}
