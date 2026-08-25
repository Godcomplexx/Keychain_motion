#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include <nrf.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/usb/usb_device.h>
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#include <zephyr/dfu/mcuboot.h>
#endif

#include "breakout_game.h"
#include "device_clock.h"
#include "esp_log.h"
#include "flip_animation.h"
#include "motion_detector.h"
#include "motion_state.h"
#include "mpu6050.h"
#include "mpu6050_optional.h"
#include "battery.h"
#include "i2c_scan.h"
#include "oled_display.h"
#include "oled_display_optional.h"
#include "pet_behavior.h"
#include "pet_face.h"
#include "phone_sync.h"
#include "step_counter.h"
#include "time_animation.h"
#include "usb_dfu_touch.h"

#define ACTIVE_FRAME_DELAY_MS 25
#define GAME_FRAME_DELAY_MS 40
#define LOW_POWER_LOOP_DELAY_MS 50
#define STARTUP_SCREEN_DELAY_MS 1000
#define HARDWARE_RETRY_INTERVAL_US 5000000
#define SENSORLESS_BLE_RETRY_US 5000000
#define MICROSECONDS_PER_SECOND 1000000.0f
#define TIME_FRAME_INTERVAL_US 1000000
#define TIME_STATE_DURATION_US 60000000
#define SLEEP_STILLNESS_TIMEOUT_US 30000000
#define MOTION_MOVEMENT_DELTA_THRESHOLD 80
#define MOTION_SHAKE_DELTA_THRESHOLD 350
#define MOTION_SHAKE_COUNT_REQUIRED 3
#define MOTION_SHAKE_WINDOW_US 2500000
#define PHONE_SYNC_WINDOW_US 60000000
#define PHONE_SYNC_SHUTDOWN_GRACE_MS 200
#define OLED_ACTIVE_CONTRAST 0x9F
#define OLED_GAME_CONTRAST 0x8F
#define OLED_IDLE_CONTRAST 0x18
#define RAW_LOG_INTERVAL_FRAMES 200
/* The companion face is redrawn slowly in SLEEP to keep the average current low. */
#define PET_SLEEP_FRAME_INTERVAL_US 200000
/* A cell voltage moves slowly; sampling it often would only waste current. */
#define BATTERY_SAMPLE_INTERVAL_US 10000000
/* Tilt gestures need hysteresis, otherwise every frame near the edge fires. */
#define TILT_TRIGGER_G 0.35f
#define TILT_RELEASE_G 0.18f
/* Below this the keychain counts as level and the gaze rests in the middle. */
#define TILT_DEAD_ZONE_G 0.06f
/*
 * Tip it steeply and hold, like tipping a glass, and the particles come out.
 * Steep enough and long enough that ordinary handling cannot do it by accident.
 */
/*
 * 0.70 g is about 44 degrees from flat. Steeper than that is awkward while the
 * board still has a display and a USB cable hanging off it, and the keychain
 * already rests near 0.38 g, so this is a clear deliberate tip either way.
 */
#define PLAY_GESTURE_TILT_G 0.70f
#define PLAY_GESTURE_HOLD_US 1500000

/*
 * In free fall the accelerometer stops feeling gravity, so the magnitude of
 * the vector collapses towards zero. Nothing else on a desk does that.
 */
#define FREE_FALL_MAGNITUDE_G 0.35f
#define FREE_FALL_HOLD_US 80000
#define FREE_FALL_REARM_US 1500000

/*
 * Gravity pointing the wrong way along Z, held long enough to be deliberate.
 *
 * Which Z sign means inverted is not derivable. Not from the layout - U5 shares
 * the top layer with everything else - and not from a reading either, because a
 * reading says where the board is, not which way it is meant to be. Samples
 * taken minutes apart showed -0.88 g and +0.91 g, both at rest, and picking a
 * sign by hand got it wrong twice.
 *
 * So the firmware stops guessing and learns instead: whichever way up the
 * keychain is when it starts is "upright", and the opposite is inverted. That
 * makes the rule something a person can state without ambiguity, and a power
 * cycle in the normal position is the whole recalibration procedure.
 */
#define UPSIDE_DOWN_G 0.55f
#define UPSIDE_DOWN_HOLD_US 600000
/* Ignore near-vertical startups, where Z carries no orientation information. */
#define UPRIGHT_REFERENCE_MIN_G 0.40f

/*
 * Rocking is slow, gentle and repetitive: several direction changes inside a
 * few seconds, with an amplitude between "not still" and "being shaken".
 */
#define ROCKING_MIN_TILT_G 0.12f
#define ROCKING_MAX_TILT_G 0.70f
#define ROCKING_REVERSALS_REQUIRED 4
#define ROCKING_WINDOW_US 5000000

/*
 * Something moved nearby without picking the keychain up: a tremor above the
 * noise floor but below the threshold that counts as handling.
 */
#define NEARBY_MOTION_MIN_DELTA 18
#define NEARBY_MOTION_MAX_DELTA 70
#define NEARBY_MOTION_REARM_US 12000000

static const char *TAG = "keychain";

/*
 * On this PCB the USB 5 V rail feeds both the charger (U1) and the module VBUS
 * pin, so "VBUS is present" is the same fact as "the charger is plugged in".
 *
 * This reads the SoC's own VBUS detector instead of listening to USB device
 * stack callbacks. The E73 image enumerates no USB class at all - the
 * nrf52840dk devicetree has no cdc-acm node, so CONFIG_USB_CDC_ACM is dropped -
 * and would therefore never receive a single callback. The POWER peripheral
 * answers regardless, and a dumb charger that never enumerates still raises
 * VBUS, which a USB callback could not have reported either.
 */
static bool charger_is_attached(void)
{
    return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0U;
}

#if DT_NODE_EXISTS(DT_NODELABEL(accel_int))

/*
 * ACC_INT1 reaches U4 pin 22 = P0.07, and the LIS2DW12 pulses it on wake-up.
 * The devicetree pulls the line down, so an unsoldered sensor cannot produce
 * phantom edges. This is the path that will let the SoC sleep and be woken by
 * movement instead of polling the sensor.
 */
static const struct gpio_dt_spec s_accel_int =
    GPIO_DT_SPEC_GET(DT_NODELABEL(accel_int), gpios);
static struct gpio_callback s_accel_int_callback;
static volatile bool s_accel_int_fired;

static void accel_int_handler(const struct device *port,
                              struct gpio_callback *callback,
                              gpio_port_pins_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(callback);
    ARG_UNUSED(pins);
    s_accel_int_fired = true;
}

static void accel_int_init(void)
{
    if (!gpio_is_ready_dt(&s_accel_int) ||
        gpio_pin_configure_dt(&s_accel_int, GPIO_INPUT) != 0 ||
        gpio_pin_interrupt_configure_dt(&s_accel_int,
                                        GPIO_INT_EDGE_TO_ACTIVE) != 0) {
        ESP_LOGW(TAG, "ACC_INT1 could not be armed");
        return;
    }
    gpio_init_callback(&s_accel_int_callback, accel_int_handler,
                       BIT(s_accel_int.pin));
    (void)gpio_add_callback(s_accel_int.port, &s_accel_int_callback);
    ESP_LOGI(TAG, "ACC_INT1 armed on P0.%02u", s_accel_int.pin);
}

/* Consume a pending edge, if any. */
static bool accel_int_take(void)
{
    if (!s_accel_int_fired) {
        return false;
    }
    s_accel_int_fired = false;
    return true;
}

#else

static void accel_int_init(void)
{
    /* No ACC_INT1 line in this build. */
}

static bool accel_int_take(void)
{
    return false;
}

#endif

/*
 * Turn a tilt in g into the -100..100 the face follows.
 *
 * A small dead zone keeps a keychain lying flat from twitching, and the caller
 * smooths the result so the eyes glide instead of chattering at 40 frames per
 * second.
 */
static int8_t tilt_to_percent(float tilt)
{
    const float magnitude = (tilt < 0.0f) ? -tilt : tilt;
    if (magnitude < TILT_DEAD_ZONE_G) {
        return 0;
    }

    int value = (int)(tilt * 100.0f);
    if (value > 100) { value = 100; }
    if (value < -100) { value = -100; }
    return (int8_t)value;
}

/*
 * Convert a raw tilt reading into -1, 0 or +1 with hysteresis: the gesture only
 * re-arms after the keychain returns close to level.
 */
static int tilt_zone(float tilt_x, int previous_zone)
{
    const float magnitude = (tilt_x < 0.0f) ? -tilt_x : tilt_x;
    if (previous_zone != 0 && magnitude < TILT_RELEASE_G) {
        return 0;
    }
    if (tilt_x <= -TILT_TRIGGER_G) {
        return -1;
    }
    if (tilt_x >= TILT_TRIGGER_G) {
        return 1;
    }
    return previous_zone;
}

static int64_t uptime_us(void)
{
    return k_ticks_to_us_floor64(k_uptime_ticks());
}

/*
 * The resting screen is the same companion face, only redrawn a few times per
 * second. The pet decides what it looks like there: bored, asleep or busy with
 * one of its own activities.
 */
static esp_err_t render_pet_sleep_frame_if_due(int64_t now_us,
                                               int64_t *previous_frame_us,
                                               const pet_view_t *view,
                                               float tilt_x, float tilt_y,
                                               float frame_seconds)
{
    if (!oled_display_is_available()) {
        return ESP_OK;
    }

    /* Particles need every frame to look like a fluid; a face does not. */
    if (view->id == PET_ACT_FLUID) {
        *previous_frame_us = now_us;
        return flip_animation_render(tilt_x, tilt_y, frame_seconds);
    }

    if (now_us - *previous_frame_us < PET_SLEEP_FRAME_INTERVAL_US) {
        return ESP_OK;
    }
    const esp_err_t err = pet_face_render(view, (uint32_t)(now_us / 1000));
    if (err == ESP_OK) {
        *previous_frame_us = now_us;
    }
    return err;
}

static esp_err_t render_time_frame_if_due(int64_t now_us,
                                          int64_t entered_at_us,
                                          int64_t *previous_frame_us,
                                          const device_clock_t *clock,
                                          const step_counter_t *steps)
{
    if (!oled_display_is_available() ||
        now_us - *previous_frame_us < TIME_FRAME_INTERVAL_US) {
        return ESP_OK;
    }

    device_clock_datetime_t datetime;
    device_clock_get_datetime(clock, now_us, &datetime);
    const time_animation_view_t view = {
        .year = datetime.year,
        .month = datetime.month,
        .day = datetime.day,
        .hour = datetime.hour,
        .minute = datetime.minute,
        .second = datetime.second,
        .steps_today = step_counter_get_steps_today(steps),
        .clock_synced = device_clock_has_phone_sync(clock),
    };
    const esp_err_t err = time_animation_render(
        &view, now_us - entered_at_us, TIME_STATE_DURATION_US);
    if (err == ESP_OK) {
        *previous_frame_us = now_us;
    }
    return err;
}

static void apply_state_entry_actions(motion_state_t state,
                                      int64_t now_us,
                                      int64_t *previous_frame_us,
                                      int64_t *previous_sleep_frame_us,
                                      int64_t *previous_time_frame_us)
{
    /* A missing OLED is not fatal: BLE and sensor probing keep running. */
    if (!oled_display_is_available()) {
        ESP_LOGI(TAG, "Entered %s state (OLED absent)",
                 motion_state_name(state));
        return;
    }

    (void)oled_display_set_power(true);
    switch (state) {
    case MOTION_STATE_FLUID:
        (void)oled_display_set_contrast(OLED_ACTIVE_CONTRAST);
        flip_animation_reset();
        *previous_frame_us = now_us;
        break;
    case MOTION_STATE_SLEEP:
        (void)oled_display_set_contrast(OLED_IDLE_CONTRAST);
        *previous_sleep_frame_us = 0;
        break;
    case MOTION_STATE_TIME:
        (void)oled_display_set_contrast(OLED_ACTIVE_CONTRAST);
        *previous_time_frame_us = 0;
        break;
    case MOTION_STATE_GAME:
        (void)oled_display_set_contrast(OLED_GAME_CONTRAST);
        *previous_frame_us = now_us;
        break;
    default:
        break;
    }
    ESP_LOGI(TAG, "Entered %s state", motion_state_name(state));
}

static motion_event_t detector_result_to_event(
    const motion_detector_result_t *result)
{
    if (result->shake_detected) return MOTION_EVENT_SHAKE_DETECTED;
    if (result->movement_detected) return MOTION_EVENT_MOVEMENT_DETECTED;
    if (result->stillness_timeout) return MOTION_EVENT_STILLNESS_TIMEOUT;
    return MOTION_EVENT_NONE;
}

static void log_render_error(const char *screen, esp_err_t err)
{
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s render deferred; OLED will be reprobed: %s",
                 screen, esp_err_to_name(err));
    }
}

int main(void)
{
    /* USB is useful on XIAO but optional on the battery-powered final PCB. */
    (void)usb_enable(NULL);
    ESP_LOGI(TAG, "Smart Motion Keychain nRF52840 port started");
    /*
     * Three images exist for two boards. Saying which one is running turns a
     * confusing log into an obvious one - especially "why does USB update not
     * work", whose answer is usually that this is the bare image.
     */
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
    ESP_LOGI(TAG, "Build: MCUboot slot0 image; USB update available");
#else
    ESP_LOGI(TAG, "Build: bare image at flash origin; SWD update only");
#endif

    flip_animation_stats_t flip_stats;
    if (flip_animation_prepare(&flip_stats) != ESP_OK) {
        ESP_LOGE(TAG, "FLIP model preparation failed");
        return 0;
    }

    int64_t now_us = uptime_us();
    int64_t previous_frame_us = now_us;
    int64_t previous_sleep_frame_us = 0;
    int64_t previous_time_frame_us = 0;
    int64_t last_sensor_probe_us = now_us - HARDWARE_RETRY_INTERVAL_US;
    int64_t last_display_probe_us = now_us - HARDWARE_RETRY_INTERVAL_US;
    int64_t last_sensorless_ble_request_us =
        now_us - SENSORLESS_BLE_RETRY_US;
    uint32_t frames_until_log = 0;
    int demo_direction = 1;
    float demo_tilt = -0.7f;
    float last_tilt_x = 0.0f;
    int previous_tilt_zone = 0;
    int smoothed_tilt_x = 0;
    int smoothed_tilt_y = 0;
    int64_t play_gesture_started_us = 0;
    bool play_gesture_fired = false;
    int64_t free_fall_started_us = 0;
    int64_t last_free_fall_us = 0;
    int64_t upside_down_started_us = 0;
    int upright_z_sign = 0;
    int64_t last_nearby_motion_us = 0;
    int64_t rocking_window_started_us = 0;
    int rocking_reversals = 0;
    int rocking_direction = 0;
    bool being_rocked = false;
    bool upside_down = false;
    bool previous_charger_attached = false;
    bool sensorless_ble_announced = false;
    int64_t last_battery_sample_us = 0;
    uint8_t battery_percent = BATTERY_PERCENT_UNKNOWN;
    uint8_t last_logged_percent = BATTERY_PERCENT_UNKNOWN;
    int32_t battery_millivolts = 0;
    pet_behavior_id_t previous_pet_id = PET_ID_COUNT;

    device_clock_t clock;
    device_clock_init(&clock, now_us);
    device_clock_datetime_t datetime;
    device_clock_get_datetime(&clock, now_us, &datetime);
    step_counter_t steps;
    step_counter_init(&steps, device_clock_date_key(&datetime));
    motion_detector_t detector;
    motion_detector_init(&detector, now_us,
                         MOTION_MOVEMENT_DELTA_THRESHOLD,
                         MOTION_SHAKE_DELTA_THRESHOLD,
                         MOTION_SHAKE_COUNT_REQUIRED,
                         MOTION_SHAKE_WINDOW_US,
                         SLEEP_STILLNESS_TIMEOUT_US);
    motion_state_machine_t state_machine;
    motion_state_init(&state_machine, now_us);
    breakout_game_t game;

    /* The companion behaviour engine keeps its own clock in milliseconds. */
    pet_behavior_t pet;
    pet_behavior_init(&pet, (uint32_t)(now_us / 1000), sys_rand32_get());
    pet_face_reset(sys_rand32_get());

    if (oled_display_init() == ESP_OK) {
        (void)oled_display_set_contrast(OLED_ACTIVE_CONTRAST);
        (void)oled_display_show_startup();
        k_sleep(K_MSEC(STARTUP_SCREEN_DELAY_MS));
        flip_animation_reset();
    } else {
        ESP_LOGW(TAG, "OLED absent; the application stays alive");
    }
    last_display_probe_us = uptime_us();

    if (mpu6050_init() != ESP_OK) {
        ESP_LOGW(TAG,
                 "Accelerometer absent; display uses demo motion and BLE stays visible");
    }
    last_sensor_probe_us = uptime_us();

    accel_int_init();
    (void)battery_init();

    /* Both drivers have reported by now; this says what is really on the bus,
     * which is the difference between a loose wire and a driver problem. */
    i2c_scan_log();

    const bool phone_sync_available = phone_sync_init() == ESP_OK;
    if (!phone_sync_available) {
        ESP_LOGW(TAG, "Phone synchronization is unavailable");
    }
    bool phone_sync_window_active = false;
    int64_t phone_sync_window_deadline_us = 0;

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
    /*
     * A no-op with the current update path: serial recovery writes slot0 in
     * place, so the image already counts as confirmed on its first boot. It is
     * kept because uploading to the secondary slot instead would make the image
     * a test image, and then reaching this line - display up, sensor probed,
     * BLE running - is exactly the evidence worth confirming on.
     */
    if (!boot_is_img_confirmed()) {
        if (boot_write_img_confirmed() == 0) {
            ESP_LOGI(TAG, "Image confirmed; MCUboot will keep it");
        } else {
            ESP_LOGE(TAG, "Could not confirm image; MCUboot will revert it");
        }
    }
#endif

    previous_charger_attached = charger_is_attached();
    pet_behavior_post(&pet, PET_EVENT_BOOT_COMPLETED,
                      (uint32_t)(uptime_us() / 1000));

    previous_frame_us = uptime_us();
    while (true) {
        /* Preserve convenient USB reflashing after the SoC is moved. */
        usb_dfu_touch_poll();
        now_us = uptime_us();
        float frame_seconds =
            (float)(now_us - previous_frame_us) / MICROSECONDS_PER_SECOND;
        /* Do not inject a huge physics step after USB/BLE delays. */
        if (frame_seconds < 0.001f || frame_seconds > 0.1f) {
            frame_seconds = 0.025f;
        }
        previous_frame_us = now_us;
        const uint32_t now_ms = (uint32_t)(now_us / 1000);
        motion_state_t current_state = motion_state_get(&state_machine);

        /* Plugging in USB on this PCB means the charger started working. */
        const bool charger_attached = charger_is_attached();
        if (charger_attached != previous_charger_attached) {
            previous_charger_attached = charger_attached;
            pet_behavior_post(&pet,
                              charger_attached ? PET_EVENT_CHARGER_ATTACHED
                                               : PET_EVENT_CHARGER_DETACHED,
                              now_ms);
        }

        /* Both I2C devices may be connected after boot; retry without reset. */
        if (!oled_display_is_available() &&
            now_us - last_display_probe_us >= HARDWARE_RETRY_INTERVAL_US) {
            last_display_probe_us = now_us;
            if (oled_display_init() == ESP_OK) {
                ESP_LOGI(TAG, "OLED connected after boot");
                apply_state_entry_actions(current_state, now_us,
                                          &previous_frame_us,
                                          &previous_sleep_frame_us,
                                          &previous_time_frame_us);
            }
        }

        if (!mpu6050_is_available() &&
            now_us - last_sensor_probe_us >= HARDWARE_RETRY_INTERVAL_US) {
            last_sensor_probe_us = now_us;
            if (mpu6050_init() == ESP_OK) {
                ESP_LOGI(TAG, "Accelerometer connected after boot");
                sensorless_ble_announced = false;
                previous_tilt_zone = 0;
                motion_detector_init(&detector, now_us,
                                     MOTION_MOVEMENT_DELTA_THRESHOLD,
                                     MOTION_SHAKE_DELTA_THRESHOLD,
                                     MOTION_SHAKE_COUNT_REQUIRED,
                                     MOTION_SHAKE_WINDOW_US,
                                     SLEEP_STILLNESS_TIMEOUT_US);
                phone_sync_stop_advertising();
            }
        }

        /* With no accelerometer there is no triple-shake gesture. */
        if (phone_sync_available && !mpu6050_is_available() &&
            now_us - last_sensorless_ble_request_us >=
                SENSORLESS_BLE_RETRY_US) {
            last_sensorless_ble_request_us = now_us;
            if (phone_sync_request_advertising() == ESP_OK &&
                !sensorless_ble_announced) {
                /*
                 * Advertising is retried every few seconds here, so the pet is
                 * told only once: otherwise it would restart the looking-for-
                 * the-phone animation forever and never show anything else.
                 */
                sensorless_ble_announced = true;
                pet_behavior_post(&pet, PET_EVENT_PHONE_WINDOW_OPEN, now_ms);
            }
        }

        if (phone_sync_window_active &&
            now_us >= phone_sync_window_deadline_us) {
            phone_sync_stop_advertising();
            phone_sync_window_active = false;
            pet_behavior_post(&pet, PET_EVENT_PHONE_WINDOW_CLOSED, now_ms);
            ESP_LOGI(TAG, "Phone sync window expired");
        }

        if (phone_sync_available && phone_sync_take_connected_event()) {
            pet_behavior_post(&pet, PET_EVENT_PHONE_CONNECTED, now_ms);
        }

        phone_sync_command_t command;
        if (phone_sync_available && phone_sync_get_command(&command) &&
            command == PHONE_SYNC_COMMAND_START_GAME) {
            phone_sync_window_active = false;
            phone_sync_stop_advertising();
            k_sleep(K_MSEC(PHONE_SYNC_SHUTDOWN_GRACE_MS));
            (void)phone_sync_shutdown();

            const motion_state_t previous_state = current_state;
            current_state = motion_state_handle_event(
                &state_machine, MOTION_EVENT_GAME_REQUESTED, true, now_us);
            breakout_game_start(&game, now_us, sys_rand32_get(), last_tilt_x);
            if (current_state != previous_state) {
                apply_state_entry_actions(current_state, now_us,
                                          &previous_frame_us,
                                          &previous_sleep_frame_us,
                                          &previous_time_frame_us);
            }
            ESP_LOGW(TAG, "Breakout started from phone command");
        }

        device_clock_datetime_t phone_datetime;
        if (phone_sync_available &&
            phone_sync_get_datetime_update(&phone_datetime)) {
            device_clock_set_datetime(&clock, &phone_datetime, now_us);
            if (step_counter_get_date_key(&steps) !=
                device_clock_date_key(&phone_datetime)) {
                step_counter_init(&steps,
                                  device_clock_date_key(&phone_datetime));
            }
            phone_sync_window_active = false;
            phone_sync_stop_advertising();
            k_sleep(K_MSEC(PHONE_SYNC_SHUTDOWN_GRACE_MS));
            (void)phone_sync_shutdown();

            const motion_state_t previous_state = current_state;
            current_state = motion_state_handle_event(
                &state_machine, MOTION_EVENT_TIME_REQUESTED, true, now_us);
            if (current_state != previous_state) {
                apply_state_entry_actions(current_state, now_us,
                                          &previous_frame_us,
                                          &previous_sleep_frame_us,
                                          &previous_time_frame_us);
            } else if (current_state == MOTION_STATE_TIME) {
                previous_time_frame_us = 0;
            }
            pet_behavior_post(&pet, PET_EVENT_PHONE_SYNCED, now_ms);
            ESP_LOGW(TAG, "Clock synchronized from phone");
        }

        if (current_state == MOTION_STATE_TIME &&
            now_us - motion_state_entered_at_us(&state_machine) >=
                TIME_STATE_DURATION_US) {
            const bool movement_recent = !mpu6050_is_available() ||
                motion_detector_has_recent_movement(&detector, now_us);
            (void)motion_state_handle_event(&state_machine,
                                            MOTION_EVENT_TIME_TIMEOUT,
                                            movement_recent, now_us);
            current_state = motion_state_get(&state_machine);
            apply_state_entry_actions(current_state, now_us,
                                      &previous_frame_us,
                                      &previous_sleep_frame_us,
                                      &previous_time_frame_us);
        }

        mpu6050_accel_data_t raw_data = {0, 0, 256};
        motion_detector_result_t detector_result = {0};
        motion_event_t event = MOTION_EVENT_NONE;
        bool sensor_sample_valid = false;
        if (mpu6050_is_available()) {
            if (mpu6050_read_accel(&raw_data) == ESP_OK) {
                sensor_sample_valid = true;
                device_clock_get_datetime(&clock, now_us, &datetime);
                step_counter_update(&steps, &raw_data,
                                    device_clock_date_key(&datetime), now_us);
                detector_result = motion_detector_update(
                    &detector, &raw_data, now_us);
                event = detector_result_to_event(&detector_result);
            } else {
                ESP_LOGW(TAG, "Accelerometer disconnected; continuing safely");
                last_sensor_probe_us = now_us;
                if (current_state == MOTION_STATE_SLEEP) {
                    current_state = motion_state_handle_event(
                        &state_machine, MOTION_EVENT_MOVEMENT_DETECTED,
                        true, now_us);
                    apply_state_entry_actions(current_state, now_us,
                                              &previous_frame_us,
                                              &previous_sleep_frame_us,
                                              &previous_time_frame_us);
                }
            }
        }

        /* A single jolt startles the pet even when it is not a full gesture. */
        if (sensor_sample_valid && detector_result.shake_peak &&
            !detector_result.shake_detected) {
            pet_behavior_post(&pet, PET_EVENT_JOLT, now_ms);
        }

        if (sensor_sample_valid && event == MOTION_EVENT_SHAKE_DETECTED) {
            /* The pet reacts to being shaken whatever the screen is doing. */
            pet_behavior_post(&pet, PET_EVENT_SHAKEN, now_ms);
            if (current_state == MOTION_STATE_GAME) {
                current_state = motion_state_handle_event(
                    &state_machine, MOTION_EVENT_GAME_FINISHED, true, now_us);
                apply_state_entry_actions(current_state, now_us,
                                          &previous_frame_us,
                                          &previous_sleep_frame_us,
                                          &previous_time_frame_us);
            }
            if (!phone_sync_window_active && phone_sync_available &&
                phone_sync_request_advertising() == ESP_OK) {
                phone_sync_window_active = true;
                phone_sync_window_deadline_us = now_us + PHONE_SYNC_WINDOW_US;
                pet_behavior_post(&pet, PET_EVENT_PHONE_WINDOW_OPEN, now_ms);
                ESP_LOGW(TAG, "Triple shake: BLE window open for 60 seconds");
            }
        } else if (sensor_sample_valid && event != MOTION_EVENT_NONE) {
            /*
             * Movement that ends a SLEEP means the keychain was picked up; the
             * same event during normal use only keeps the idle timers reset.
             */
            if (event == MOTION_EVENT_MOVEMENT_DETECTED) {
                pet_behavior_post(&pet,
                                  (current_state == MOTION_STATE_SLEEP)
                                      ? PET_EVENT_PICKED_UP
                                      : PET_EVENT_MOVEMENT,
                                  now_ms);
            }
            const motion_state_t previous_state = current_state;
            current_state = motion_state_handle_event(
                &state_machine, event,
                motion_detector_has_recent_movement(&detector, now_us), now_us);
            if (current_state != previous_state) {
                apply_state_entry_actions(current_state, now_us,
                                          &previous_frame_us,
                                          &previous_sleep_frame_us,
                                          &previous_time_frame_us);
            }
        }

        float tilt_x;
        float tilt_y;
        if (sensor_sample_valid) {
            tilt_x = mpu6050_accel_to_g(raw_data.x);
            tilt_y = mpu6050_accel_to_g(raw_data.y);
        } else {
            /* A triangle wave keeps FLUID and GAME visibly testable. */
            demo_tilt += (float)demo_direction * 0.02f;
            if (demo_tilt >= 0.7f || demo_tilt <= -0.7f) {
                demo_direction = -demo_direction;
            }
            tilt_x = demo_tilt;
            tilt_y = 0.35f;
        }
        last_tilt_x = tilt_x;

        /*
         * Smooth the live tilt the face follows. Raw samples jitter by a few
         * counts, and at 40 frames per second that reads as trembling eyes
         * rather than as a steady gaze.
         */
        smoothed_tilt_x = (smoothed_tilt_x * 3 + tilt_to_percent(tilt_x)) / 4;
        smoothed_tilt_y = (smoothed_tilt_y * 3 + tilt_to_percent(tilt_y)) / 4;

        if (sensor_sample_valid) {
            const float tilt_z = mpu6050_accel_to_g(raw_data.z);
            const float magnitude =
                sqrtf(tilt_x * tilt_x + tilt_y * tilt_y + tilt_z * tilt_z);

            /* Free fall: gravity disappears from the vector. */
            if (magnitude < FREE_FALL_MAGNITUDE_G) {
                if (free_fall_started_us == 0) {
                    free_fall_started_us = now_us;
                } else if (now_us - free_fall_started_us >= FREE_FALL_HOLD_US &&
                           now_us - last_free_fall_us >= FREE_FALL_REARM_US) {
                    last_free_fall_us = now_us;
                    pet_behavior_post(&pet, PET_EVENT_FREE_FALL, now_ms);
                    ESP_LOGW(TAG, "Free fall detected (%.2f g)",
                             (double)magnitude);
                }
            } else {
                free_fall_started_us = 0;
            }

            /* Learn which way up it started; that becomes "upright". */
            if (upright_z_sign == 0 &&
                (tilt_z >= UPRIGHT_REFERENCE_MIN_G ||
                 tilt_z <= -UPRIGHT_REFERENCE_MIN_G)) {
                upright_z_sign = (tilt_z > 0.0f) ? 1 : -1;
                ESP_LOGI(TAG, "Upright reference learned: Z = %.2f g",
                         (double)tilt_z);
            }

            /* Held inverted relative to that, long enough to be deliberate. */
            if (upright_z_sign != 0 &&
                (float)upright_z_sign * tilt_z <= -UPSIDE_DOWN_G) {
                if (upside_down_started_us == 0) {
                    upside_down_started_us = now_us;
                } else if (now_us - upside_down_started_us >=
                           UPSIDE_DOWN_HOLD_US) {
                    if (!upside_down) {
                        ESP_LOGI(TAG, "Upside down (Z = %.2f g)",
                                 (double)tilt_z);
                    }
                    upside_down = true;
                }
            } else {
                upside_down_started_us = 0;
                upside_down = false;
            }

            /*
             * Rocking: count direction reversals of a gentle tilt. A shake
             * reverses too, but its amplitude is far outside this band.
             */
            const float swing = (tilt_x < 0.0f) ? -tilt_x : tilt_x;
            if (swing >= ROCKING_MIN_TILT_G && swing <= ROCKING_MAX_TILT_G) {
                const int direction = (tilt_x > 0.0f) ? 1 : -1;
                if (rocking_direction != 0 && direction != rocking_direction) {
                    if (rocking_window_started_us == 0 ||
                        now_us - rocking_window_started_us > ROCKING_WINDOW_US) {
                        rocking_window_started_us = now_us;
                        rocking_reversals = 0;
                    }
                    ++rocking_reversals;
                }
                rocking_direction = direction;
            }
            if (rocking_window_started_us != 0 &&
                now_us - rocking_window_started_us > ROCKING_WINDOW_US) {
                rocking_window_started_us = 0;
                rocking_reversals = 0;
                being_rocked = false;
            }
            being_rocked = rocking_reversals >= ROCKING_REVERSALS_REQUIRED;

            /* A tremor through the surface, with nobody holding it. */
            const int delta = detector_result.raw_delta;
            if (delta >= NEARBY_MOTION_MIN_DELTA &&
                delta <= NEARBY_MOTION_MAX_DELTA && !being_rocked &&
                now_us - last_nearby_motion_us >= NEARBY_MOTION_REARM_US) {
                last_nearby_motion_us = now_us;
                pet_behavior_post(&pet, PET_EVENT_NEARBY_MOTION, now_ms);
            }
        } else {
            upside_down = false;
            being_rocked = false;
        }

        /*
         * Hold it steeply tipped to ask for the particles. The latch makes the
         * gesture fire once per tip rather than continuously while held.
         */
        /*
         * Measured across both horizontal axes rather than along X alone. The
         * keychain does not rest perfectly flat, so a single-axis threshold is
         * nearly reached in one direction and unreachable in the other; the
         * combined tilt asks the same thing of every direction.
         */
        const float sideways = sensor_sample_valid
            ? sqrtf(tilt_x * tilt_x + tilt_y * tilt_y) : 0.0f;
        if (sensor_sample_valid && sideways >= PLAY_GESTURE_TILT_G) {
            if (play_gesture_started_us == 0) {
                play_gesture_started_us = now_us;
                /* Say so, so a tip that is deep enough but too brief is
                 * distinguishable from one that never got deep enough. */
                ESP_LOGI(TAG, "Play gesture: tilted far enough (%.2f g), hold",
                         (double)sideways);
            } else if (!play_gesture_fired &&
                       now_us - play_gesture_started_us >=
                           PLAY_GESTURE_HOLD_US) {
                play_gesture_fired = true;
                pet_behavior_post(&pet, PET_EVENT_PLAY_REQUESTED, now_ms);
                ESP_LOGI(TAG, "Play gesture: particles requested (%.2f g)",
                         (double)sideways);
            }
        } else {
            play_gesture_started_us = 0;
            play_gesture_fired = false;
        }

        /*
         * A hardware edge on ACC_INT1 means the sensor itself decided the
         * keychain moved, which is exactly the "picked up" moment.
         */
        if (accel_int_take()) {
            pet_behavior_post(&pet, PET_EVENT_PICKED_UP, now_ms);
        }

        /* Only a real sensor produces a tilt gesture; the demo wave must not. */
        if (sensor_sample_valid) {
            const int zone = tilt_zone(tilt_x, previous_tilt_zone);
            if (zone != previous_tilt_zone && zone != 0) {
                pet_behavior_post(&pet,
                                  (zone < 0) ? PET_EVENT_TILT_LEFT
                                             : PET_EVENT_TILT_RIGHT,
                                  now_ms);
            }
            previous_tilt_zone = zone;
        }

        if (battery_is_available() &&
            now_us - last_battery_sample_us >= BATTERY_SAMPLE_INTERVAL_US) {
            last_battery_sample_us = now_us;
            battery_reading_t reading;
            if (battery_sample(&reading, charger_attached)) {
                battery_percent = reading.percent;
                battery_millivolts = reading.cell_millivolts;
                /*
                 * Only on a change: a line every ten seconds forever buries
                 * everything else. The pin figure is kept because that is what
                 * the divider ratio would be corrected from.
                 */
                if (reading.percent != last_logged_percent) {
                    last_logged_percent = reading.percent;
                    ESP_LOGI(TAG,
                             "Battery: pin=%d mV cell=%d mV resting=%d mV -> %u%%",
                             reading.pin_millivolts, reading.cell_millivolts,
                             reading.resting_millivolts,
                             (unsigned)reading.percent);
                }
            }
        }

        const pet_context_t pet_context = {
            .sensor_present = mpu6050_is_available(),
            .charging = charger_attached,
            .battery_percent = battery_percent,
            .battery_millivolts = battery_millivolts,
            /* Only a real sensor may steer the gaze; the demo wave must not. */
            .tilt_x = sensor_sample_valid ? (int8_t)smoothed_tilt_x : 0,
            .tilt_y = sensor_sample_valid ? (int8_t)smoothed_tilt_y : 0,
            .upside_down = upside_down,
            .being_rocked = being_rocked,
        };
        pet_behavior_update(&pet, &pet_context, now_ms);
        const pet_view_t *pet_view = pet_behavior_view(&pet);
        if (pet_view->id != previous_pet_id) {
            if (pet_view->id == PET_ACT_FLUID) {
                /* Start the particle activity from a clean, known state. */
                flip_animation_reset();
            }
            ESP_LOGI(TAG, "pet: %s -> %s priority=P%d bond=%u",
                     pet_behavior_name(previous_pet_id),
                     pet_behavior_name(pet_view->id),
                     (int)pet_view->priority, (unsigned)pet_view->bond);
            previous_pet_id = pet_view->id;
        }

        if (sensor_sample_valid && frames_until_log == 0) {
            ESP_LOGI(TAG, "State=%s event=%s steps=%lu raw=%d,%d,%d",
                     motion_state_name(current_state),
                     motion_event_name(event),
                     (unsigned long)step_counter_get_steps_today(&steps),
                     raw_data.x, raw_data.y, raw_data.z);
            frames_until_log = RAW_LOG_INTERVAL_FRAMES;
        }
        if (frames_until_log > 0) --frames_until_log;

        esp_err_t render_err = ESP_OK;
        switch (current_state) {
        case MOTION_STATE_FLUID:
            if (oled_display_is_available()) {
                /*
                 * The companion owns the active screen. FLIP is still here, but
                 * now as one of the activities the pet picks when left alone.
                 */
                render_err = (pet_view->id == PET_ACT_FLUID)
                    ? flip_animation_render(tilt_x, tilt_y, frame_seconds)
                    : pet_face_render(pet_view, now_ms);
            }
            log_render_error("FLUID", render_err);
            k_sleep(K_MSEC(ACTIVE_FRAME_DELAY_MS));
            break;
        case MOTION_STATE_SLEEP:
            render_err = render_pet_sleep_frame_if_due(
                now_us, &previous_sleep_frame_us, pet_view,
                tilt_x, tilt_y, frame_seconds);
            log_render_error("SLEEP", render_err);
            k_sleep(K_MSEC(LOW_POWER_LOOP_DELAY_MS));
            break;
        case MOTION_STATE_TIME:
            render_err = render_time_frame_if_due(
                now_us, motion_state_entered_at_us(&state_machine),
                &previous_time_frame_us, &clock, &steps);
            log_render_error("TIME", render_err);
            k_sleep(K_MSEC(LOW_POWER_LOOP_DELAY_MS));
            break;
        case MOTION_STATE_GAME: {
            bool game_finished = false;
            render_err = breakout_game_update_and_render(
                &game, tilt_x, frame_seconds, now_us, &game_finished);
            log_render_error("GAME", render_err);
            if (game_finished) {
                current_state = motion_state_handle_event(
                    &state_machine, MOTION_EVENT_GAME_FINISHED, true, now_us);
                apply_state_entry_actions(current_state, now_us,
                                          &previous_frame_us,
                                          &previous_sleep_frame_us,
                                          &previous_time_frame_us);
            }
            k_sleep(K_MSEC(GAME_FRAME_DELAY_MS));
            break;
        }
        default:
            ESP_LOGE(TAG, "Unknown motion state; restarting FLUID state");
            motion_state_init(&state_machine, now_us);
            break;
        }
    }

    return 0;
}
