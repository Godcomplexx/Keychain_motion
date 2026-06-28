#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_config.h"
#include "breakout_game.h"
#include "device_clock.h"
#include "flip_animation.h"
#include "i2c_bus.h"
#include "idle_animation.h"
#include "motion_detector.h"
#include "motion_state.h"
#include "mpu6050.h"
#include "oled_display.h"
#include "phone_sync.h"
#include "step_counter.h"
#include "time_animation.h"

/*
 * FLUID is the default active state, so its loop rate dominates average power.
 * A short delay plus 200 kHz I2C targets roughly 9-12 FPS. FLIP splits longer
 * frame intervals into stable 50 ms physics substeps instead of dropping time.
 */
#define ACTIVE_FRAME_DELAY_MS 40
#define GAME_FRAME_DELAY_MS 40
#define STARTUP_SCREEN_DELAY_MS 1000
#define MICROSECONDS_PER_SECOND 1000000.0f
/*
 * Logging every frame wastes CPU and UART time in normal battery operation.
 * At the conservative FLUID rate this logs roughly once per minute.
 */
#define RAW_LOG_INTERVAL_FRAMES 200
#define SLEEP_STILLNESS_TIMEOUT_US 30000000
#define OLED_OFF_SLEEP_DELAY_US 30000000
#define SLEEP_FRAME_INTERVAL_US 600000
/*
 * The TIME screen only shows whole seconds, so refreshing it four times per
 * second just sent the same 1 KB framebuffer over I2C three extra times. Once
 * per second keeps the clock correct while cutting TIME-state display traffic
 * by about 4x. The progress bar still advances smoothly enough at 1 Hz.
 */
#define TIME_FRAME_INTERVAL_US 1000000
#define TIME_STATE_DURATION_US 60000000
#define LOW_POWER_SENSOR_READ_INTERVAL_US 150000
#define LOW_POWER_LOOP_DELAY_MS 50
#define MOTION_MOVEMENT_DELTA_THRESHOLD 80
#define MOTION_SHAKE_DELTA_THRESHOLD 350
/*
 * TIME should open only on an intentional gesture, not on a single accidental
 * bump. Require three shake peaks within a short window before requesting a
 * phone sync; TIME opens only after the phone sends fresh time.
 */
#define MOTION_SHAKE_COUNT_REQUIRED 3
#define MOTION_SHAKE_WINDOW_US 2500000
#define PHONE_SYNC_WINDOW_US 60000000
#define PHONE_SYNC_SHUTDOWN_GRACE_MS 200
#define CPU_MAX_FREQUENCY_MHZ 160
#define CPU_MIN_FREQUENCY_MHZ 40
#define OLED_ACTIVE_CONTRAST 0x9F
#define OLED_GAME_CONTRAST 0x8F
#define OLED_IDLE_CONTRAST 0x18
#define DISPLAY_RENDER_FAILURE_LIMIT 5
#define DISPLAY_RENDER_RECOVERY_DELAY_MS 250

static const char *TAG = "keychain";

static esp_err_t configure_power_management(void)
{
    const esp_pm_config_t config = {
        .max_freq_mhz = CPU_MAX_FREQUENCY_MHZ,
        .min_freq_mhz = CPU_MIN_FREQUENCY_MHZ,
        .light_sleep_enable = false,
    };
    return esp_pm_configure(&config);
}

static esp_err_t enter_motion_wakeup_sleep(void)
{
    bool sensor_low_power = false;
    bool display_off = false;
    bool gpio_wakeup_configured = false;
    bool wakeup_enabled = false;

    esp_err_t err = phone_sync_shutdown();
    if (err != ESP_OK) {
        return err;
    }

    const gpio_config_t interrupt_gpio_config = {
        .pin_bit_mask = 1ULL << BOARD_MPU6050_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&interrupt_gpio_config);
    if (err != ESP_OK) {
        return err;
    }

    err = mpu6050_set_motion_wakeup_mode();
    if (err != ESP_OK) {
        return err;
    }
    sensor_low_power = true;

    err = oled_display_set_power(false);
    if (err != ESP_OK) {
        goto cleanup;
    }
    display_off = true;

    err = gpio_wakeup_enable(BOARD_MPU6050_INT_GPIO,
                             GPIO_INTR_HIGH_LEVEL);
    if (err != ESP_OK) {
        goto cleanup;
    }
    gpio_wakeup_configured = true;

    err = esp_sleep_enable_gpio_wakeup();
    if (err != ESP_OK) {
        goto cleanup;
    }
    wakeup_enabled = true;

    ESP_LOGI(TAG, "OLED off; entering light sleep until MPU-6050 motion");
    err = esp_light_sleep_start();

cleanup:
    if (wakeup_enabled) {
        (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    }
    if (gpio_wakeup_configured) {
        (void)gpio_wakeup_disable(BOARD_MPU6050_INT_GPIO);
    }
    if (sensor_low_power) {
        esp_err_t restore_err = mpu6050_set_active_mode();
        if (err == ESP_OK) {
            err = restore_err;
        }
    }
    if (display_off) {
        esp_err_t display_err = oled_display_set_power(true);
        if (err == ESP_OK) {
            err = display_err;
        }
    }

    return err;
}

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
        .clock_synced = device_clock_has_phone_sync(device_clock),
    };

    esp_err_t err = time_animation_render(&view,
                                          now_us - entered_at_us,
                                          TIME_STATE_DURATION_US);
    if (err == ESP_OK) {
        *previous_time_frame_us = now_us;
    }
    return err;
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
        ESP_RETURN_ON_ERROR(oled_display_set_power(true),
                            TAG, "OLED active power setup failed");
        ESP_RETURN_ON_ERROR(oled_display_set_contrast(OLED_ACTIVE_CONTRAST),
                            TAG, "OLED active contrast setup failed");
        flip_animation_reset();
        *previous_frame_us = now_us;
        break;

    case MOTION_STATE_SLEEP:
        /* Sleep animation stays visible but dimmer and slower. */
        ESP_RETURN_ON_ERROR(oled_display_set_power(true),
                            TAG, "OLED sleep power setup failed");
        ESP_RETURN_ON_ERROR(oled_display_set_contrast(OLED_IDLE_CONTRAST),
                            TAG, "OLED sleep contrast setup failed");
        *sleep_frame_index = 0;
        *previous_sleep_frame_us = 0;
        break;

    case MOTION_STATE_TIME:
        /* TIME is a visible interaction state, so restore normal contrast. */
        ESP_RETURN_ON_ERROR(oled_display_set_power(true),
                            TAG, "OLED time power setup failed");
        ESP_RETURN_ON_ERROR(oled_display_set_contrast(OLED_ACTIVE_CONTRAST),
                            TAG, "OLED time contrast setup failed");
        *previous_time_frame_us = 0;
        break;

    case MOTION_STATE_GAME:
        ESP_RETURN_ON_ERROR(oled_display_set_power(true),
                            TAG, "OLED game power setup failed");
        ESP_RETURN_ON_ERROR(oled_display_set_contrast(OLED_GAME_CONTRAST),
                            TAG, "OLED game contrast setup failed");
        *previous_frame_us = now_us;
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

static bool display_render_succeeded_or_deferred(const char *label,
                                                 esp_err_t err,
                                                 uint8_t *failure_count)
{
    if (err == ESP_OK) {
        *failure_count = 0;
        return true;
    }

    ++(*failure_count);
    if (*failure_count >= DISPLAY_RENDER_FAILURE_LIMIT) {
        ESP_LOGE(TAG,
                 "%s rendering failed %u times, keeping app alive for BLE/time sync: %s",
                 label,
                 (unsigned int)*failure_count,
                 esp_err_to_name(err));
        *failure_count = 0;
    } else {
        ESP_LOGW(TAG,
                 "%s rendering failed, will retry next loop (%u/%u): %s",
                 label,
                 (unsigned int)*failure_count,
                 (unsigned int)DISPLAY_RENDER_FAILURE_LIMIT,
                 esp_err_to_name(err));
    }

    vTaskDelay(pdMS_TO_TICKS(DISPLAY_RENDER_RECOVERY_DELAY_MS));
    return false;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Smart Motion Keychain started");

    esp_err_t err = configure_power_management();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Dynamic frequency scaling unavailable: %s",
                 esp_err_to_name(err));
    }

    /* Prepare the FLIP particle arrays before the render loop uses them. */
    flip_animation_stats_t flip_stats;
    err = flip_animation_prepare(&flip_stats);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FLIP data-model preparation failed: %s",
                 esp_err_to_name(err));
        return;
    }

    /* Verify the I2C address before the driver reads MPU-6050 registers. */
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

    err = mpu6050_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MPU-6050 initialization failed: %s",
                 esp_err_to_name(err));
        i2c_bus_deinit();
        return;
    }

    err = oled_display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED initialization failed: %s", esp_err_to_name(err));
        mpu6050_deinit();
        i2c_bus_deinit();
        return;
    }

    err = oled_display_set_contrast(OLED_ACTIVE_CONTRAST);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED contrast setup failed: %s",
                 esp_err_to_name(err));
        oled_display_deinit();
        mpu6050_deinit();
        i2c_bus_deinit();
        return;
    }

    err = oled_display_show_startup();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED drawing failed: %s", esp_err_to_name(err));
        oled_display_deinit();
        mpu6050_deinit();
        i2c_bus_deinit();
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(STARTUP_SCREEN_DELAY_MS));
    flip_animation_reset();
    ESP_LOGI(TAG, "Starting MPU-6050-controlled FLIP particle animation");

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
    breakout_game_t breakout_game;
    uint8_t display_render_failure_count = 0;
    float last_tilt_x = 0.0f;

    device_clock_init(&device_clock, previous_frame_us);
    device_clock_datetime_t datetime;
    device_clock_get_datetime(&device_clock, previous_frame_us, &datetime);
    step_counter_init(&step_counter, device_clock_date_key(&datetime));

    err = phone_sync_init();
    const bool phone_sync_available = err == ESP_OK;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Phone sync BLE disabled: %s", esp_err_to_name(err));
    }
    bool phone_sync_window_active = false;
    int64_t phone_sync_window_deadline_us = 0;

    motion_detector_init(&motion_detector,
                         previous_frame_us,
                         MOTION_MOVEMENT_DELTA_THRESHOLD,
                         MOTION_SHAKE_DELTA_THRESHOLD,
                         MOTION_SHAKE_COUNT_REQUIRED,
                         MOTION_SHAKE_WINDOW_US,
                         SLEEP_STILLNESS_TIMEOUT_US);
    motion_state_init(&motion_state, previous_frame_us);

    while (true) {
        const int64_t current_frame_us = esp_timer_get_time();
        const float frame_seconds =
            (float)(current_frame_us - previous_frame_us) /
            MICROSECONDS_PER_SECOND;
        previous_frame_us = current_frame_us;
        motion_state_t current_state = motion_state_get(&motion_state);

        if (phone_sync_window_active &&
            current_frame_us >= phone_sync_window_deadline_us) {
            phone_sync_stop_advertising();
            const esp_err_t shutdown_err = phone_sync_shutdown();
            if (shutdown_err != ESP_OK) {
                ESP_LOGW(TAG, "BLE shutdown after timeout failed: %s",
                         esp_err_to_name(shutdown_err));
            }
            phone_sync_window_active = false;
            ESP_LOGI(TAG, "Phone sync window expired");
        }

        phone_sync_command_t phone_command;
        if (phone_sync_available &&
            phone_sync_get_command(&phone_command)) {
            phone_sync_window_active = false;
            phone_sync_stop_advertising();
            vTaskDelay(pdMS_TO_TICKS(PHONE_SYNC_SHUTDOWN_GRACE_MS));
            const esp_err_t shutdown_err = phone_sync_shutdown();
            if (shutdown_err != ESP_OK) {
                ESP_LOGW(TAG, "BLE shutdown after command failed: %s",
                         esp_err_to_name(shutdown_err));
            }

            if (phone_command == PHONE_SYNC_COMMAND_START_GAME) {
                const motion_state_t previous_state = current_state;
                current_state = motion_state_handle_event(
                    &motion_state,
                    MOTION_EVENT_GAME_REQUESTED,
                    true,
                    current_frame_us);
                breakout_game_start(&breakout_game,
                                    current_frame_us,
                                    esp_random(),
                                    last_tilt_x);
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
                ESP_LOGW(TAG, "Breakout started from phone command");
            }
        }

        device_clock_datetime_t phone_datetime;
        if (phone_sync_available &&
            phone_sync_get_datetime_update(&phone_datetime)) {
            device_clock_set_datetime(&device_clock,
                                      &phone_datetime,
                                      current_frame_us);
            if (step_counter_get_date_key(&step_counter) !=
                device_clock_date_key(&phone_datetime)) {
                step_counter_init(&step_counter,
                                  device_clock_date_key(&phone_datetime));
            }
            phone_sync_window_active = false;
            phone_sync_stop_advertising();
            vTaskDelay(pdMS_TO_TICKS(PHONE_SYNC_SHUTDOWN_GRACE_MS));
            const esp_err_t shutdown_err = phone_sync_shutdown();
            if (shutdown_err != ESP_OK) {
                ESP_LOGW(TAG, "BLE shutdown after sync failed: %s",
                         esp_err_to_name(shutdown_err));
            }
            ESP_LOGW(TAG,
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

        if (current_state == MOTION_STATE_SLEEP &&
            !phone_sync_window_active &&
            !usb_serial_jtag_is_connected() &&
            current_frame_us - motion_state_entered_at_us(&motion_state) >=
                OLED_OFF_SLEEP_DELAY_US) {
            err = enter_motion_wakeup_sleep();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Light sleep failed: %s",
                         esp_err_to_name(err));
                break;
            }

            const int64_t woke_at_us = esp_timer_get_time();
            motion_detector_init(&motion_detector,
                                 woke_at_us,
                                 MOTION_MOVEMENT_DELTA_THRESHOLD,
                                 MOTION_SHAKE_DELTA_THRESHOLD,
                                 MOTION_SHAKE_COUNT_REQUIRED,
                                 MOTION_SHAKE_WINDOW_US,
                                 SLEEP_STILLNESS_TIMEOUT_US);
            current_state = motion_state_handle_event(
                &motion_state,
                MOTION_EVENT_MOVEMENT_DETECTED,
                true,
                woke_at_us);
            err = apply_state_entry_actions(current_state,
                                            woke_at_us,
                                            &previous_frame_us,
                                            &previous_sleep_frame_us,
                                            &sleep_frame_index,
                                            &previous_time_frame_us);
            if (err != ESP_OK) {
                break;
            }
            previous_low_power_sensor_read_us = woke_at_us;
            ESP_LOGI(TAG, "Woke from light sleep by MPU-6050 motion");
            continue;
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

            if (!display_render_succeeded_or_deferred(
                    motion_state_name(current_state),
                    err,
                    &display_render_failure_count)) {
                continue;
            }

            vTaskDelay(pdMS_TO_TICKS(LOW_POWER_LOOP_DELAY_MS));
            continue;
        }

        mpu6050_accel_data_t raw_data;
        err = mpu6050_read_accel(&raw_data);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "MPU-6050 read failed: %s", esp_err_to_name(err));
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

        if (event == MOTION_EVENT_SHAKE_DETECTED) {
            if (current_state == MOTION_STATE_GAME) {
                current_state = motion_state_handle_event(
                    &motion_state,
                    MOTION_EVENT_GAME_FINISHED,
                    true,
                    current_frame_us);
                err = apply_state_entry_actions(current_state,
                                                current_frame_us,
                                                &previous_frame_us,
                                                &previous_sleep_frame_us,
                                                &sleep_frame_index,
                                                &previous_time_frame_us);
                if (err != ESP_OK) {
                    break;
                }
                ESP_LOGW(TAG,
                         "Breakout stopped by triple shake; opening BLE window");
            }

            if (phone_sync_window_active) {
                /* One gesture owns the whole 60-second synchronization window. */
            } else if (phone_sync_available) {
                err = phone_sync_request_advertising();
                if (err == ESP_OK) {
                    phone_sync_window_active = true;
                    phone_sync_window_deadline_us =
                        current_frame_us + PHONE_SYNC_WINDOW_US;
                    ESP_LOGW(
                        TAG,
                        "Triple shake detected; waiting up to 60 s for phone time");
                } else {
                    ESP_LOGW(TAG, "BLE sync request failed: %s",
                             esp_err_to_name(err));
                }
            } else {
                ESP_LOGW(TAG,
                         "Triple shake detected, but phone sync is unavailable");
            }
        } else if (event != MOTION_EVENT_NONE) {
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

        const float tilt_x = mpu6050_accel_to_g(raw_data.x);
        const float tilt_y = mpu6050_accel_to_g(raw_data.y);
        last_tilt_x = tilt_x;

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
            if (!display_render_succeeded_or_deferred(
                    "Particle",
                    err,
                    &display_render_failure_count)) {
                continue;
            }
            vTaskDelay(pdMS_TO_TICKS(ACTIVE_FRAME_DELAY_MS));
            break;

        case MOTION_STATE_SLEEP:
            err = render_sleep_frame_if_due(current_frame_us,
                                            &previous_sleep_frame_us,
                                            &sleep_frame_index);
            if (!display_render_succeeded_or_deferred(
                    "Sleep",
                    err,
                    &display_render_failure_count)) {
                continue;
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
            if (!display_render_succeeded_or_deferred(
                    "Time",
                    err,
                    &display_render_failure_count)) {
                continue;
            }
            vTaskDelay(pdMS_TO_TICKS(LOW_POWER_LOOP_DELAY_MS));
            break;

        case MOTION_STATE_GAME: {
            bool game_finished = false;
            err = breakout_game_update_and_render(&breakout_game,
                                                  tilt_x,
                                                  frame_seconds,
                                                  current_frame_us,
                                                  &game_finished);
            if (!display_render_succeeded_or_deferred(
                    "Breakout",
                    err,
                    &display_render_failure_count)) {
                continue;
            }
            if (game_finished) {
                current_state = motion_state_handle_event(
                    &motion_state,
                    MOTION_EVENT_GAME_FINISHED,
                    true,
                    current_frame_us);
                err = apply_state_entry_actions(current_state,
                                                current_frame_us,
                                                &previous_frame_us,
                                                &previous_sleep_frame_us,
                                                &sleep_frame_index,
                                                &previous_time_frame_us);
                if (err != ESP_OK) {
                    break;
                }
                ESP_LOGI(TAG, "Breakout finished; returning to FLUID");
            }
            vTaskDelay(pdMS_TO_TICKS(GAME_FRAME_DELAY_MS));
            break;
        }

        default:
            ESP_LOGE(TAG, "Unknown motion state");
            err = ESP_ERR_INVALID_STATE;
            break;
        }

        if (err != ESP_OK) {
            break;
        }
    }

    (void)phone_sync_shutdown();
    oled_display_deinit();
    mpu6050_deinit();
    i2c_bus_deinit();
}
