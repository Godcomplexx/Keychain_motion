#include <stdbool.h>
#include <stdint.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pet_face.h"
#include "round_display.h"
#include "round_motion.h"
#include "round_orientation.h"
#include "round_reactions.h"
#include "round_touch.h"

#define ORIENTATION_HOLD_MS 1000U
/* 40 frames per second. Motion is still sampled every 50 ms (two frames), so
 * the gesture thresholds keep the sample spacing they were tuned for. */
#define FRAME_PERIOD_MS 25U

static const char *TAG = "round_pet";

static const char *gesture_name(round_gesture_t gesture)
{
    switch (gesture) {
    case ROUND_GESTURE_PICKED_UP: return "picked_up";
    case ROUND_GESTURE_SMALL_MOVEMENT: return "small_movement";
    case ROUND_GESTURE_JOLT: return "jolt";
    case ROUND_GESTURE_SHAKE: return "shake";
    case ROUND_GESTURE_TILT_LEFT: return "tilt_left";
    case ROUND_GESTURE_TILT_RIGHT: return "tilt_right";
    default: return "none";
    }
}

static int8_t tilt_percent(float acceleration_g)
{
    if (acceleration_g > 1.0f) { return 100; }
    if (acceleration_g < -1.0f) { return -100; }
    return (int8_t)(acceleration_g * 100.0f);
}

static pet_behavior_id_t face_for_gesture(round_gesture_t gesture,
                                          uint32_t *duration_ms)
{
    switch (gesture) {
    case ROUND_GESTURE_PICKED_UP:
        *duration_ms = 2000U;
        return PET_HAPPY;
    case ROUND_GESTURE_SMALL_MOVEMENT:
        *duration_ms = 2200U;
        return PET_CURIOUS;
    case ROUND_GESTURE_JOLT:
        *duration_ms = 1200U;
        return PET_SURPRISED;
    case ROUND_GESTURE_SHAKE:
        *duration_ms = 1800U;
        return PET_ANGRY;
    default:
        *duration_ms = 0;
        return PET_NEUTRAL; /* Tilt is a held base state, not a timed reaction. */
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(round_display_init());
    ESP_ERROR_CHECK(round_touch_init());
    const bool motion_ready = round_motion_init();
    const gpio_config_t button = {
        .pin_bit_mask = 1ULL << ROUND_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, /* BOOT pulls the pin low. */
    };
    ESP_ERROR_CHECK(gpio_config(&button));
    bool orientation_mode = false;
    bool button_was_down = false;
    bool button_handled = false;
    uint32_t button_down_ms = 0;

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    round_reactions_t gestures;
    round_reactions_init(&gestures, now_ms);
    round_motion_sample_t sample = {0.0f, 0.0f, 1.0f};
    pet_behavior_id_t reaction = PET_NEUTRAL;
    pet_behavior_id_t shown = PET_NEUTRAL;
    uint32_t reaction_started_ms = now_ms;
    uint32_t reaction_duration_ms = 0;
    uint32_t shown_started_ms = now_ms;
    uint32_t last_motion_read_ms = now_ms - 50U;
    uint32_t last_motion_log_ms = now_ms;
    uint32_t last_motion_success_ms = now_ms;
    uint32_t last_touch_ms = 0;
    bool reacting = false;
    pet_face_reset(now_ms);

    ESP_LOGI(TAG, "Eyes follow MPU6050; touch only draws a diagnostic cross");
    TickType_t frame_wake = xTaskGetTickCount();
    while (true) {
        /* A fixed period, not a pause after each frame, keeps motion even. */
        xTaskDelayUntil(&frame_wake, pdMS_TO_TICKS(FRAME_PERIOD_MS));
        now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        bool reaction_started = false;

        /* Holding BOOT for a second opens or closes the mounting check. */
        const bool button_down = gpio_get_level(ROUND_BUTTON_GPIO) == 0;
        if (button_down && !button_was_down) {
            button_down_ms = now_ms;
            button_handled = false;
        }
        button_was_down = button_down;
        if (button_down && !button_handled &&
            (uint32_t)(now_ms - button_down_ms) >= ORIENTATION_HOLD_MS) {
            button_handled = true;
            orientation_mode = !orientation_mode;
            if (orientation_mode) {
                round_orientation_start(now_ms);
            }
        }
        if (orientation_mode) {
            round_motion_sample_t raw;
            const bool have_raw = motion_ready && round_motion_read_raw(&raw);
            const esp_err_t err = round_orientation_render(
                have_raw ? &raw : NULL, now_ms);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Orientation render failed: %s", esp_err_to_name(err));
            }
            continue;
        }

        if (motion_ready &&
            (uint32_t)(now_ms - last_motion_read_ms) >= 50U) {
            last_motion_read_ms = now_ms;
            if (round_motion_read(&sample)) {
                last_motion_success_ms = now_ms;
                const round_gesture_t gesture = round_reactions_update(
                    &gestures, sample.x_g, sample.y_g, sample.z_g, now_ms);
                if ((uint32_t)(now_ms - last_motion_log_ms) >= 1000U) {
                    last_motion_log_ms = now_ms;
                    ESP_LOGI(TAG, "accel x=%.2fg y=%.2fg z=%.2fg still=%lums",
                             sample.x_g, sample.y_g, sample.z_g,
                             (unsigned long)round_reactions_still_ms(&gestures,
                                                                       now_ms));
                }
                if (gesture != ROUND_GESTURE_NONE) {
                    ESP_LOGI(TAG, "gesture=%s", gesture_name(gesture));
                    uint32_t duration_ms;
                    const pet_behavior_id_t next = face_for_gesture(
                        gesture, &duration_ms);
                    /* A full shake stays visible despite its later jolts. */
                    if (next != PET_NEUTRAL &&
                        (!reacting || reaction != PET_ANGRY ||
                         next == PET_ANGRY)) {
                        reaction = next;
                        reaction_duration_ms = duration_ms;
                        reaction_started_ms = now_ms;
                        reacting = true;
                        reaction_started = true;
                    }
                }
            }
        }

        round_touch_point_t touch;
        if (round_touch_read(&touch)) {
            last_touch_ms = now_ms;
            round_display_touch_marker(touch.x, touch.y, true);
            ESP_LOGI(TAG, "touch x=%u y=%u gesture=0x%02x",
                     touch.x, touch.y, touch.gesture);
        }
        if ((uint32_t)(now_ms - last_touch_ms) >= 1500U) {
            round_display_touch_marker(0, 0, false);
        }

        if (reacting &&
            (uint32_t)(now_ms - reaction_started_ms) >= reaction_duration_ms) {
            reacting = false;
        }

        /* Even boredom and sleep come from measured stillness, not a demo timer. */
        const bool motion_live = motion_ready && gestures.has_sample &&
            (uint32_t)(now_ms - last_motion_success_ms) < 1000U;
        pet_behavior_id_t base = PET_NEUTRAL;
        if (motion_live) {
            const int8_t zone = round_reactions_tilt_zone(&gestures);
            if (zone < 0) { base = PET_LOOK_LEFT; }
            if (zone > 0) { base = PET_LOOK_RIGHT; }
            if (zone == 0) {
                const uint32_t still_ms = round_reactions_still_ms(
                    &gestures, now_ms);
                if (still_ms >= 300000U) { base = PET_ASLEEP; }
                else if (still_ms >= 30000U) { base = PET_BORED; }
            }
        }
        const pet_behavior_id_t visible = reacting ? reaction : base;
        if (visible != shown || reaction_started) {
            shown = visible;
            shown_started_ms = now_ms;
            ESP_LOGI(TAG, "face=%u", (unsigned)visible);
        }
        const pet_view_t view = {
            .id = visible,
            .priority = reacting ? PET_PRIORITY_P1 : PET_PRIORITY_P2,
            .source = reacting ? PET_SOURCE_MOTION : PET_SOURCE_NONE,
            .elapsed_ms = now_ms - shown_started_ms,
            .duration_ms = reacting ? reaction_duration_ms : 0,
            .battery_percent = PET_BATTERY_UNKNOWN,
            .tilt_x = motion_live ? tilt_percent(sample.x_g) : 0,
            .tilt_y = motion_live ? tilt_percent(sample.y_g) : 0,
        };
        const esp_err_t err = pet_face_render(&view, now_ms);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Face render failed: %s", esp_err_to_name(err));
        }
    }
}
