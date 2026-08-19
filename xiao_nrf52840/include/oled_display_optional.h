#ifndef XIAO_OLED_DISPLAY_OPTIONAL_H
#define XIAO_OLED_DISPLAY_OPTIONAL_H

#include <stdbool.h>

/* The main loop uses this to retry a display connected after boot. */
bool oled_display_is_available(void);

#endif /* XIAO_OLED_DISPLAY_OPTIONAL_H */
