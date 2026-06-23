#include <math.h>
#include <stdbool.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "fluid_animation.h"
#include "i2c_bus.h"
#include "oled_display.h"

#define ANIMATION_DELAY_MS 10
#define STARTUP_SCREEN_DELAY_MS 1000
#define SIMULATION_STEP_SECONDS 0.10f

static const char *TAG = "keychain";

void app_main(void)
{
    ESP_LOGI(TAG, "Smart Motion Keychain started");

    esp_err_t err = i2c_bus_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C initialization failed: %s", esp_err_to_name(err));
        return;
    }

    err = i2c_bus_scan();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C scan failed: %s", esp_err_to_name(err));
    }

    err = oled_display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED initialization failed: %s", esp_err_to_name(err));
        i2c_bus_deinit();
        return;
    }

    err = oled_display_show_startup();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED drawing failed: %s", esp_err_to_name(err));
        oled_display_deinit();
        i2c_bus_deinit();
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(STARTUP_SCREEN_DELAY_MS));
    fluid_animation_reset();
    ESP_LOGI(TAG, "Starting fluid animation with simulated tilt");

    float simulation_time = 0.0f;
    while (true) {
        /* MPU6050 values will replace these simulated inputs later. */
        const float tilt_x = sinf(simulation_time * 0.8f);
        const float tilt_y = 0.45f * cosf(simulation_time * 0.5f);

        err = fluid_animation_render(tilt_x, tilt_y,
                                     SIMULATION_STEP_SECONDS);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Fluid animation failed: %s",
                     esp_err_to_name(err));
            break;
        }

        simulation_time += SIMULATION_STEP_SECONDS;
        vTaskDelay(pdMS_TO_TICKS(ANIMATION_DELAY_MS));
    }
}
