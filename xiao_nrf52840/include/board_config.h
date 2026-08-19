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

#endif /* XIAO_BOARD_CONFIG_H */
