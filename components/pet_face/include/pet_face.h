#ifndef PET_FACE_H
#define PET_FACE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "pet_behavior.h"

/*
 * Procedural face renderer for the 128x64 OLED.
 *
 * The renderer owns no behaviour logic. It receives "which behaviour and how
 * long it has been running" and derives every pixel from that, so no sprite
 * sheets are needed and a new mood costs one switch case instead of an asset.
 */

/* Reset blink timing and per-frame noise. Call when the face becomes visible. */
void pet_face_reset(uint32_t seed);

/*
 * Draw one frame for the given behaviour view.
 * Returns ESP_ERR_NOT_SUPPORTED for PET_ACT_FLUID: that activity is rendered by
 * the existing flip animation, so the caller draws it instead.
 */
esp_err_t pet_face_render(const pet_view_t *view, uint32_t now_ms);

#endif /* PET_FACE_H */
