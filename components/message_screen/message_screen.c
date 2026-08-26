#include "message_screen.h"

#include <string.h>

#include "oled_display.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define FRAME_INSET 3
#define TITLE_Y 7
#define RULE_Y 17
#define PROGRESS_Y 59
#define PROGRESS_HEIGHT 2
/* The band the lines are centred in, between the rule and the bar. */
#define TEXT_TOP 20
#define TEXT_BOTTOM 57
#define FONT_HEIGHT 7
/* Breathing room between lines, so descender-less capitals still separate. */
#define LINE_GAP 3

static bool is_space(char value)
{
    return value == ' ' || value == '\t' || value == '\n';
}

int message_screen_wrap(const char *text,
                        int columns,
                        int max_rows,
                        char lines[MESSAGE_SCREEN_MAX_ROWS]
                                  [MESSAGE_SCREEN_MAX_COLUMNS + 1])
{
    if (columns > MESSAGE_SCREEN_MAX_COLUMNS) {
        columns = MESSAGE_SCREEN_MAX_COLUMNS;
    }
    if (max_rows > MESSAGE_SCREEN_MAX_ROWS) {
        max_rows = MESSAGE_SCREEN_MAX_ROWS;
    }
    for (int row = 0; row < MESSAGE_SCREEN_MAX_ROWS; ++row) {
        lines[row][0] = '\0';
    }
    if (text == NULL || columns <= 0 || max_rows <= 0) {
        return 0;
    }

    int row = 0;
    int column = 0;
    size_t index = 0;

    while (text[index] != '\0' && row < max_rows) {
        if (is_space(text[index])) {
            /* Spaces never start a line: they would look like an indent. */
            if (column > 0 && column < columns) {
                lines[row][column] = ' ';
                ++column;
            }
            ++index;
            continue;
        }

        size_t word_end = index;
        while (text[word_end] != '\0' && !is_space(text[word_end])) {
            ++word_end;
        }
        const int word_length = (int)(word_end - index);

        /*
         * A word that does not fit moves down, unless it would not fit on a
         * line of its own either - a word longer than the screen has to break
         * somewhere, and the edge is the least surprising place.
         */
        if (column > 0 && column + word_length > columns &&
            word_length <= columns) {
            lines[row][column] = '\0';
            ++row;
            column = 0;
            continue;
        }

        while (index < word_end && row < max_rows) {
            if (column == columns) {
                lines[row][column] = '\0';
                ++row;
                column = 0;
                continue;
            }
            lines[row][column] = text[index];
            ++column;
            ++index;
        }
    }

    if (row < max_rows) {
        lines[row][column] = '\0';
        if (column > 0) {
            ++row;
        }
    }

    /* Trailing spaces would push a centred line off its centre. */
    for (int line = 0; line < row; ++line) {
        int length = (int)strlen(lines[line]);
        while (length > 0 && lines[line][length - 1] == ' ') {
            lines[line][length - 1] = '\0';
            --length;
        }
    }
    return row;
}

/* Spaces do not count: the wrap drops them at line ends by design. */
static int printable_count(const char *text)
{
    int total = 0;
    for (size_t index = 0; text[index] != '\0'; ++index) {
        if (!is_space(text[index])) {
            ++total;
        }
    }
    return total;
}

static int printable_in_lines(char lines[MESSAGE_SCREEN_MAX_ROWS]
                                        [MESSAGE_SCREEN_MAX_COLUMNS + 1],
                              int line_count)
{
    int total = 0;
    for (int line = 0; line < line_count; ++line) {
        total += printable_count(lines[line]);
    }
    return total;
}

static void draw_progress_bar(int64_t elapsed_us, int64_t duration_us)
{
    const int x = FRAME_INSET + 3;
    const int width = SCREEN_WIDTH - 2 * x;
    int remaining = width;

    if (duration_us > 0) {
        int64_t used = elapsed_us;
        if (used < 0) {
            used = 0;
        }
        if (used > duration_us) {
            used = duration_us;
        }
        remaining = (int)(((duration_us - used) * width) / duration_us);
    }

    oled_display_fill_rect(x, PROGRESS_Y, remaining, PROGRESS_HEIGHT, true);
}

esp_err_t message_screen_render(const char *text,
                                int64_t elapsed_us,
                                int64_t duration_us)
{
    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char lines[MESSAGE_SCREEN_MAX_ROWS][MESSAGE_SCREEN_MAX_COLUMNS + 1];

    /*
     * Try the large size first and accept it only if the whole note fits.
     * message_screen_wrap stops at max_rows, so a message that needed a third
     * large line comes back having silently lost its tail - the check is that
     * the wrap consumed everything, not that it produced some lines.
     */
    int scale = MESSAGE_SCREEN_LARGE_SCALE;
    int line_count = message_screen_wrap(text,
                                         MESSAGE_SCREEN_LARGE_COLUMNS,
                                         MESSAGE_SCREEN_LARGE_ROWS, lines);
    if (printable_count(text) > printable_in_lines(lines, line_count)) {
        scale = MESSAGE_SCREEN_SMALL_SCALE;
        line_count = message_screen_wrap(text,
                                         MESSAGE_SCREEN_SMALL_COLUMNS,
                                         MESSAGE_SCREEN_SMALL_ROWS, lines);
    }

    oled_display_clear();
    oled_display_draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, true);
    oled_display_draw_text(FRAME_INSET + 3, TITLE_Y, "MESSAGE");
    oled_display_fill_rect(FRAME_INSET + 3, RULE_Y,
                           SCREEN_WIDTH - 2 * (FRAME_INSET + 3), 1, true);

    /*
     * The block of lines is centred in the band, so a one-line note does not
     * sit high on an otherwise empty screen.
     */
    const int line_height = FONT_HEIGHT * scale + LINE_GAP;
    const int block_height = line_count * line_height - LINE_GAP;
    const int band = TEXT_BOTTOM - TEXT_TOP;
    int y = TEXT_TOP;
    if (block_height < band) {
        y += (band - block_height) / 2;
    }

    for (int line = 0; line < line_count; ++line) {
        const int width = oled_display_text_width(lines[line], scale);
        oled_display_draw_text_scaled((SCREEN_WIDTH - width) / 2,
                                      y + line * line_height,
                                      lines[line], scale);
    }

    draw_progress_bar(elapsed_us, duration_us);
    return oled_display_present();
}
