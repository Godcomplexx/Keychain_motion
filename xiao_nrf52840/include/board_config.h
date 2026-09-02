#ifndef XIAO_BOARD_CONFIG_H
#define XIAO_BOARD_CONFIG_H

/* Display geometry is shared by every renderer in the original application. */
#define BOARD_OLED_WIDTH 128
#define BOARD_OLED_HEIGHT 64

/* Hardware addresses stay centralized instead of being repeated in drivers. */
#define BOARD_OLED_I2C_ADDRESS 0x3C
#define BOARD_LIS2DW12_I2C_ADDRESS 0x18
#define BOARD_MPU6050_I2C_ADDRESS 0x68

/* Each board overlay maps this alias to its physically routed I2C controller. */
#define BOARD_I2C_NODE DT_ALIAS(keychain_i2c)

/*
 * The bodge-wired round GC9107 display: bus and control lines defined in
 * nrf52840dk_nrf52840.overlay. Physical panel resolution, distinct from
 * BOARD_OLED_WIDTH/HEIGHT above, which stays 128x64 - every renderer still
 * draws into that logical canvas, and the driver places it inside the round
 * panel's larger physical frame at present() time.
 */
#define BOARD_GC9107_SPI_NODE DT_NODELABEL(spi2)
#define BOARD_GC9107_WIDTH 128
#define BOARD_GC9107_HEIGHT 115

#endif /* XIAO_BOARD_CONFIG_H */
