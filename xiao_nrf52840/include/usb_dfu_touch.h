#ifndef XIAO_USB_DFU_TOUCH_H
#define XIAO_USB_DFU_TOUCH_H

/* Call from the cooperative loop to support Arduino-style 1200-baud reset. */
void usb_dfu_touch_poll(void);

#endif /* XIAO_USB_DFU_TOUCH_H */
