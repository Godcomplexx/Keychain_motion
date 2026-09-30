#ifndef ROUND_BOARD_CONFIG_H
#define ROUND_BOARD_CONFIG_H

/* ESP32-2424S012C-I board wiring; verify the exact board revision before use. */
#define ROUND_LCD_WIDTH 240
#define ROUND_LCD_HEIGHT 240
#define ROUND_LCD_SCLK_GPIO 6
#define ROUND_LCD_MOSI_GPIO 7
#define ROUND_LCD_DC_GPIO 2
#define ROUND_LCD_CS_GPIO 10
#define ROUND_LCD_BL_GPIO 3

/* CST816S/D capacitive touch controller. GPIO0 is its active-low IRQ. */
#define ROUND_TOUCH_SDA_GPIO 4
#define ROUND_TOUCH_SCL_GPIO 5
#define ROUND_TOUCH_RST_GPIO 1
#define ROUND_TOUCH_INT_GPIO 0
#define ROUND_TOUCH_I2C_ADDRESS 0x15

/* P1 exposes UART0 pins as GPIO. The USB console uses GPIO18/19 instead.
 * Confirm P1's 3V3 pin with a meter before powering the external MPU-6050. */
#define ROUND_MPU_SDA_GPIO 20
#define ROUND_MPU_SCL_GPIO 21

/* How the MPU is stuck behind the screen: the signed chip axis pointing to the
 * screen's right, to its top edge, and out of the glass toward the viewer.
 * 1=X, 2=Y, 3=Z, negative for the opposite direction. Hold BOOT for one
 * second to open the orientation check, which measures these three. */
#define ROUND_MPU_AXIS_RIGHT 1
#define ROUND_MPU_AXIS_UP 2
#define ROUND_MPU_AXIS_OUT 3

/* On-board BOOT button. A strapping pin: never hold it during reset. */
#define ROUND_BUTTON_GPIO 9

#endif
