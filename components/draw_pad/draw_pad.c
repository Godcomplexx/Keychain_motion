#include "draw_pad.h"

#include <string.h>

#include "oled_display.h"

#define PEN_UP (-1)

static void set_canvas_pixel(draw_pad_t *pad, int x, int y)
{
    if (x < 0 || x >= DRAW_PAD_WIDTH || y < 0 || y >= DRAW_PAD_HEIGHT) {
        return;
    }

    const int index = y * (DRAW_PAD_WIDTH / 8) + (x / 8);
    const uint8_t mask = (uint8_t)(0x80U >> (x % 8));
    if ((pad->canvas[index] & mask) == 0U) {
        pad->canvas[index] |= mask;
        pad->dirty = true;
    }
}

static bool canvas_pixel(const draw_pad_t *pad, int x, int y)
{
    const int index = y * (DRAW_PAD_WIDTH / 8) + (x / 8);
    return (pad->canvas[index] & (uint8_t)(0x80U >> (x % 8))) != 0U;
}

/*
 * A round nib rather than a square one: a square pen turns a diagonal stroke
 * into a visibly wider line than a horizontal one, which reads as the pen
 * changing size as you draw.
 */
static void stamp_nib(draw_pad_t *pad, int x, int y)
{
    const int radius = (int)pad->pen_radius;
    if (radius <= 0) {
        set_canvas_pixel(pad, x, y);
        return;
    }

    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= radius * radius) {
                set_canvas_pixel(pad, x + dx, y + dy);
            }
        }
    }
}

static int clamp(int value, int low, int high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

static void stamp_line(draw_pad_t *pad, int x0, int y0, int x1, int y1)
{
    /* Bresenham. The phone samples touches far apart at speed, so the gaps
     * between reported points have to be filled here or fast strokes come out
     * as dotted lines. */
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    const int step_x = x0 < x1 ? 1 : -1;
    const int step_y = y0 < y1 ? 1 : -1;
    int error = dx - dy;

    while (true) {
        stamp_nib(pad, x0, y0);
        if (x0 == x1 && y0 == y1) {
            return;
        }
        const int doubled = error * 2;
        if (doubled > -dy) {
            error -= dy;
            x0 += step_x;
        }
        if (doubled < dx) {
            error += dx;
            y0 += step_y;
        }
    }
}

void draw_pad_init(draw_pad_t *pad)
{
    if (pad == NULL) {
        return;
    }

    memset(pad->canvas, 0, sizeof(pad->canvas));
    pad->last_x = PEN_UP;
    pad->last_y = PEN_UP;
    pad->pen_radius = 0U;
    pad->dirty = true;
}

void draw_pad_clear(draw_pad_t *pad)
{
    if (pad == NULL) {
        return;
    }

    memset(pad->canvas, 0, sizeof(pad->canvas));
    pad->last_x = PEN_UP;
    pad->last_y = PEN_UP;
    pad->dirty = true;
}

void draw_pad_lift_pen(draw_pad_t *pad)
{
    if (pad == NULL) {
        return;
    }

    pad->last_x = PEN_UP;
    pad->last_y = PEN_UP;
}

void draw_pad_draw_to(draw_pad_t *pad, int x, int y)
{
    if (pad == NULL) {
        return;
    }

    x = clamp(x, 0, DRAW_PAD_WIDTH - 1);
    y = clamp(y, 0, DRAW_PAD_HEIGHT - 1);

    if (pad->last_x < 0) {
        stamp_nib(pad, x, y);
    } else {
        stamp_line(pad, pad->last_x, pad->last_y, x, y);
    }

    pad->last_x = (int16_t)x;
    pad->last_y = (int16_t)y;
}

bool draw_pad_apply_packet(draw_pad_t *pad, const uint8_t *data, size_t length)
{
    if (pad == NULL || data == NULL || length == 0U) {
        return false;
    }

    const uint8_t opcode = data[0];
    const uint8_t *payload = &data[1];
    const size_t payload_length = length - 1U;

    switch (opcode) {
    case DRAW_PAD_OP_CLEAR:
        draw_pad_clear(pad);
        return true;

    case DRAW_PAD_OP_PEN:
        if (payload_length < 1U) {
            return false;
        }
        pad->pen_radius = payload[0] > DRAW_PAD_PEN_RADIUS_MAX
                              ? DRAW_PAD_PEN_RADIUS_MAX
                              : payload[0];
        return true;

    case DRAW_PAD_OP_BEGIN:
    case DRAW_PAD_OP_CONTINUE:
        /* An odd payload means a point was cut in half in transit; drawing the
         * whole packet anyway would put a stray line somewhere random. */
        if (payload_length == 0U || (payload_length % 2U) != 0U) {
            return false;
        }
        if (opcode == DRAW_PAD_OP_BEGIN) {
            draw_pad_lift_pen(pad);
        }
        for (size_t index = 0U; index + 1U < payload_length; index += 2U) {
            draw_pad_draw_to(pad, payload[index], payload[index + 1U]);
        }
        return true;

    default:
        return false;
    }
}

esp_err_t draw_pad_render(draw_pad_t *pad)
{
    if (pad == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!pad->dirty) {
        return ESP_OK;
    }

    oled_display_clear();
    for (int y = 0; y < DRAW_PAD_HEIGHT; ++y) {
        for (int x = 0; x < DRAW_PAD_WIDTH; ++x) {
            if (canvas_pixel(pad, x, y)) {
                oled_display_set_pixel(x, y, true);
            }
        }
    }

    const esp_err_t err = oled_display_present();
    if (err == ESP_OK) {
        pad->dirty = false;
    }
    return err;
}
