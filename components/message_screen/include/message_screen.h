#ifndef MESSAGE_SCREEN_H
#define MESSAGE_SCREEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * A short note from the phone, shown in the same framed panel as the clock.
 *
 * The length is what one BLE command write carries, which is also about as
 * much as fits on a 128x64 screen at a size worth reading. That the transport
 * and the screen agree on "short" is a happy accident, but it does mean
 * neither has to explain itself to the other.
 */
#define MESSAGE_SCREEN_MAX_LENGTH 26

/*
 * Two sizes, chosen by how much there is to say. A few words get the large
 * one; a longer note drops to the small one rather than being cut off, since
 * a message that does not fit is worse than a message that is smaller.
 *
 * Neither of these is a free choice: at twice the 5x7 font a line is 14 pixels
 * tall, and only two of those fit between the title rule and the progress bar.
 */
#define MESSAGE_SCREEN_LARGE_SCALE 2
#define MESSAGE_SCREEN_LARGE_COLUMNS 9
#define MESSAGE_SCREEN_LARGE_ROWS 2

#define MESSAGE_SCREEN_SMALL_SCALE 1
#define MESSAGE_SCREEN_SMALL_COLUMNS 19
#define MESSAGE_SCREEN_SMALL_ROWS 3

/* The widest line buffer either size needs, plus its terminator. */
#define MESSAGE_SCREEN_MAX_COLUMNS MESSAGE_SCREEN_SMALL_COLUMNS
#define MESSAGE_SCREEN_MAX_ROWS MESSAGE_SCREEN_SMALL_ROWS

/*
 * Breaks text into lines that fit, on spaces where it can and mid-word when a
 * word is longer than a line. Exposed so the wrapping can be tested without a
 * display. Returns how many lines were produced, never more than max_rows.
 */
int message_screen_wrap(const char *text,
                        int columns,
                        int max_rows,
                        char lines[MESSAGE_SCREEN_MAX_ROWS]
                                  [MESSAGE_SCREEN_MAX_COLUMNS + 1]);

esp_err_t message_screen_render(const char *text,
                                int64_t elapsed_us,
                                int64_t duration_us);

#endif /* MESSAGE_SCREEN_H */
