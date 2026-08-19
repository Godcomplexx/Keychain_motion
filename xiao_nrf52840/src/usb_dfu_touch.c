#include "usb_dfu_touch.h"

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/reboot.h>

#include "esp_log.h"

#define USB_DFU_TOUCH_BAUD_RATE 1200U

#if DT_HAS_COMPAT_STATUS_OKAY(zephyr_cdc_acm_uart)

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#include <zephyr/retention/bootmode.h>
#else
#include <nrf.h>
/* Adafruit/Seeed nRF52 bootloaders interpret 0x57 as a request for USB DFU. */
#define XIAO_UF2_GPREGRET_MAGIC 0x57U
#endif

static const char *TAG = "usb_dfu";
static const struct device *const s_usb_uart =
    DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
static bool s_reboot_requested;

/*
 * Ask whichever bootloader is installed to stay in update mode after the reset.
 * The two supported ones read a different place, but both survive the reset:
 * MCUboot reads the retention boot mode, the factory UF2 bootloader reads
 * GPREGRET directly.
 */
static void request_bootloader(void)
{
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
    (void)bootmode_set(BOOT_MODE_TYPE_BOOTLOADER);
#else
    NRF_POWER->GPREGRET = XIAO_UF2_GPREGRET_MAGIC;
    __DSB();
#endif
}

void usb_dfu_touch_poll(void)
{
    if (s_reboot_requested || !device_is_ready(s_usb_uart)) {
        return;
    }

    uint32_t baud_rate = 0;
    if (uart_line_ctrl_get(s_usb_uart, UART_LINE_CTRL_BAUD_RATE,
                           &baud_rate) != 0 ||
        baud_rate != USB_DFU_TOUCH_BAUD_RATE) {
        return;
    }

    s_reboot_requested = true;
    ESP_LOGW(TAG, "1200-baud touch detected; entering USB bootloader");

    request_bootloader();
    sys_reboot(SYS_REBOOT_COLD);
}

#else

void usb_dfu_touch_poll(void)
{
    /* No CDC node in this build; SWD remains its only programming path. */
}

#endif
