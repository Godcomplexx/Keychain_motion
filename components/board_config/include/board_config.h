#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "driver/spi_master.h"

/*
 * OLED transport selection.
 * Use I2C for the 4-pin SSD1306 module, or SPI for the older 7-pin module.
 */
#define BOARD_OLED_TRANSPORT_I2C 1
#define BOARD_OLED_TRANSPORT_SPI 2
#define BOARD_OLED_TRANSPORT BOARD_OLED_TRANSPORT_I2C

/*
 * I2C GPIO mapping selected for the ESP32-C3 Super Mini prototype.
 * The I2C OLED and ADXL345 can share these same SDA/SCL wires.
 * Recheck these values if the development board changes.
 */
#define BOARD_I2C_SDA_GPIO 5
#define BOARD_I2C_SCL_GPIO 6
#define BOARD_I2C_PORT 0
/*
 * Use standard-mode I2C for the breadboard prototype.
 * The OLED sends full 1024-byte frames, and the current wiring has shown
 * occasional NACK errors at 400 kHz.
 */
#define BOARD_I2C_FREQUENCY_HZ 100000

/* Verified with SDO connected to GND on the GY-291 module. */
#define BOARD_ADXL345_I2C_ADDRESS 0x53

/*
 * 4-pin I2C SSD1306 OLED wiring.
 * The common SSD1306 I2C address is 0x3C; confirm it with the scanner.
 * A 4-pin module normally has no separate reset pin, so reset is disabled.
 */
#define BOARD_OLED_I2C_ADDRESS 0x3C
#define BOARD_OLED_I2C_FREQUENCY_HZ BOARD_I2C_FREQUENCY_HZ
#define BOARD_OLED_I2C_RESET_GPIO -1

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
