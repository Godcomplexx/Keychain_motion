#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/*
 * I2C GPIO mapping selected for the ESP32-C3 Super Mini prototype.
 * Recheck these values if the development board changes.
 */
#define BOARD_I2C_SDA_GPIO 5
#define BOARD_I2C_SCL_GPIO 6
#define BOARD_I2C_PORT 0
#define BOARD_I2C_FREQUENCY_HZ 100000

/* Verified by the I2C scanner for the connected 128x64 OLED module. */
#define BOARD_OLED_I2C_ADDRESS 0x3C
#define BOARD_OLED_WIDTH 128
#define BOARD_OLED_HEIGHT 64
#define BOARD_OLED_RESET_GPIO -1

#endif /* BOARD_CONFIG_H */
