#ifndef DRAW_PAD_H
#define DRAW_PAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * A canvas the phone draws on. The screen is 128x64 monochrome, so a point fits
 * in two bytes and a whole stroke fits in one BLE packet: the phone sends what
 * the finger did, not what the screen should look like. Sending the 1024-byte
 * framebuffer instead would need chunking and reassembly for a picture that is
 * almost entirely blank.
 */
#define DRAW_PAD_WIDTH 128
#define DRAW_PAD_HEIGHT 64
#define DRAW_PAD_CANVAS_BYTES (DRAW_PAD_WIDTH * DRAW_PAD_HEIGHT / 8)
#define DRAW_PAD_PEN_RADIUS_MAX 3

/*
 * Packet opcodes, shared with the Android app. A packet is one opcode byte
 * followed by its payload; point payloads are (x, y) pairs, one byte each,
 * which is why the screen size is capped at 255 in both directions.
 */
#define DRAW_PAD_OP_CONTINUE 0x10 /* polyline continuing the current stroke */
#define DRAW_PAD_OP_BEGIN 0x11    /* first pair puts the pen down, rest follow */
#define DRAW_PAD_OP_CLEAR 0x20
#define DRAW_PAD_OP_PEN 0x21 /* one byte: radius, 0 to DRAW_PAD_PEN_RADIUS_MAX */

typedef struct {
    uint8_t canvas[DRAW_PAD_CANVAS_BYTES];
    /* Where the stroke currently is. Negative means the pen is up. */
    int16_t last_x;
    int16_t last_y;
    uint8_t pen_radius;
    bool dirty;
} draw_pad_t;

void draw_pad_init(draw_pad_t *pad);

void draw_pad_clear(draw_pad_t *pad);

/* Ends the stroke: the next point starts a new one instead of joining this. */
void draw_pad_lift_pen(draw_pad_t *pad);

/*
 * Draws from the last point to this one, or a single dot if the pen was up.
 * Coordinates outside the canvas are clamped rather than dropped, so a stroke
 * that runs off the edge stays connected.
 */
void draw_pad_draw_to(draw_pad_t *pad, int x, int y);

/* Applies one packet from the phone. False means the packet was malformed. */
bool draw_pad_apply_packet(draw_pad_t *pad, const uint8_t *data, size_t length);

/* Sends the canvas to the OLED, but only when something changed since last. */
esp_err_t draw_pad_render(draw_pad_t *pad);

#endif /* DRAW_PAD_H */
