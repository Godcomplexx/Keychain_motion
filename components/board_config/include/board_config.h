#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "driver/spi_master.h"

/*
 * I2C GPIO mapping selected for the ESP32-C3 Super Mini prototype.
 * Recheck these values if the development board changes.
 */
#define BOARD_I2C_SDA_GPIO 5
#define BOARD_I2C_SCL_GPIO 6
#define BOARD_I2C_PORT 0
#define BOARD_I2C_FREQUENCY_HZ 100000

/* Verified with SDO connected to GND on the GY-291 module. */
#define BOARD_ADXL345_I2C_ADDRESS 0x53

/*
 * SPI OLED wiring selected for the replacement 7-pin module.
 * The module pin marked SDA is SPI MOSI, not I2C SDA.
 */
#define BOARD_OLED_SPI_HOST SPI2_HOST
#define BOARD_OLED_SPI_SCLK_GPIO 4
#define BOARD_OLED_SPI_MOSI_GPIO 10
#define BOARD_OLED_SPI_RESET_GPIO 1
#define BOARD_OLED_SPI_DC_GPIO 3
#define BOARD_OLED_SPI_CS_GPIO 7
/* Start conservatively for the breadboard test; raise after verification. */
#define BOARD_OLED_SPI_CLOCK_HZ (1 * 1000 * 1000)
#define BOARD_OLED_WIDTH 128
#define BOARD_OLED_HEIGHT 64

#endif /* BOARD_CONFIG_H */
