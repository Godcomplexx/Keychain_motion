#include "esp_log.h"

#include "board_config.h"

static const char *TAG = "keychain";

void app_main(void)
{
    ESP_LOGI(TAG, "Smart Motion Keychain started");
    ESP_LOGI(TAG, "I2C GPIO placeholders: SDA=%d, SCL=%d",
             BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO);
}
