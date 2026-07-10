#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "driver/spi_master.h"

/*
 * OLED transport selection.
 * The active 7-pin SSD1306 board is strapped for I2C operation.
 * SPI remains available only for boards that are physically strapped for SPI.
 */
#define BOARD_OLED_TRANSPORT_I2C 1
#define BOARD_OLED_TRANSPORT_SPI 2
#define BOARD_OLED_TRANSPORT BOARD_OLED_TRANSPORT_I2C

/*
 * I2C GPIO mapping selected for the ESP32-C3 Super Mini prototype.
 * The I2C OLED and MPU-6050 can share these same SDA/SCL wires.
 * Recheck these values if the development board changes.
 */
#define BOARD_I2C_SDA_GPIO 5
#define BOARD_I2C_SCL_GPIO 6
#define BOARD_I2C_PORT 0
/*
 * Experimental smoother OLED mode. If breadboard wiring reports occasional
 * NACKs, drop this back to 200 kHz before debugging higher-level rendering.
 */
#define BOARD_I2C_FREQUENCY_HZ 300000

/* MPU-6050 uses 0x68 when AD0 is low and 0x69 when AD0 is high. */
#define BOARD_MPU6050_I2C_ADDRESS 0x68
/* MPU-6050 INT wakes the ESP32-C3 from light sleep. */
#define BOARD_MPU6050_INT_GPIO 4

/*
 * I2C SSD1306 OLED wiring.
 * The common SSD1306 I2C address is 0x3C; confirm it with the scanner.
 * The current board handles reset in hardware, so software reset is disabled.
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

#if BOARD_OLED_TRANSPORT == BOARD_OLED_TRANSPORT_SPI && \
    BOARD_MPU6050_INT_GPIO == BOARD_OLED_SPI_SCLK_GPIO
#error "MPU-6050 INT GPIO conflicts with the SPI OLED clock"
#endif

#endif /* BOARD_CONFIG_H */
