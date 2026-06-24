#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "adxl345.h"
#include "flip_animation.h"
#include "fluid_animation.h"
#include "i2c_bus.h"
#include "oled_display.h"

#define ANIMATION_FRAME_DELAY_MS 5
#define STARTUP_SCREEN_DELAY_MS 1000
#define PHYSICS_STEP_SECONDS 0.02f
#define RAW_LOG_INTERVAL_FRAMES 25

static const char *TAG = "keychain";

void app_main(void)
{
    ESP_LOGI(TAG, "Smart Motion Keychain started");

    /* Keep the cellular renderer active while validating FLIP memory usage. */
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

    err = oled_display_show_startup();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED drawing failed: %s", esp_err_to_name(err));
        oled_display_deinit();
        adxl345_deinit();
        i2c_bus_deinit();
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(STARTUP_SCREEN_DELAY_MS));
    fluid_animation_reset();
    ESP_LOGI(TAG, "Starting ADXL345-controlled particle animation");

    uint32_t frames_until_log = 0;
    while (true) {
        adxl345_raw_data_t raw_data;
        err = adxl345_read_raw(&raw_data);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ADXL345 read failed: %s", esp_err_to_name(err));
            break;
        }

        const float tilt_x = adxl345_raw_to_g(raw_data.x);
        const float tilt_y = adxl345_raw_to_g(raw_data.y);

        if (frames_until_log == 0) {
            const int tilt_x_milligravity = (int)(tilt_x * 1000.0f);
            const int tilt_y_milligravity = (int)(tilt_y * 1000.0f);
            ESP_LOGI(TAG,
                     "ADXL345 raw: X=%d (%d mg), Y=%d (%d mg), Z=%d",
                     raw_data.x, tilt_x_milligravity,
                     raw_data.y, tilt_y_milligravity,
                     raw_data.z);
            frames_until_log = RAW_LOG_INTERVAL_FRAMES;
        }
        --frames_until_log;

        err = fluid_animation_render(tilt_x, tilt_y,
                                     PHYSICS_STEP_SECONDS);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Particle rendering failed: %s",
                     esp_err_to_name(err));
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(ANIMATION_FRAME_DELAY_MS));
    }

    oled_display_deinit();
    adxl345_deinit();
    i2c_bus_deinit();
}
