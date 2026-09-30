#ifndef ROUND_REACTIONS_H
#define ROUND_REACTIONS_H

#include <stdbool.h>
#include <stdint.h>

/* Portable gesture recognition; no GPIO, I2C, ESP-IDF or screen calls here. */
typedef enum {
    ROUND_GESTURE_NONE,
    ROUND_GESTURE_PICKED_UP,
    ROUND_GESTURE_SMALL_MOVEMENT,
    ROUND_GESTURE_JOLT,
    ROUND_GESTURE_SHAKE,
    ROUND_GESTURE_TILT_LEFT,
    ROUND_GESTURE_TILT_RIGHT,
} round_gesture_t;

typedef struct {
    bool has_sample;
    float previous_x;
    float previous_y;
    float previous_z;
    uint32_t last_handled_ms;
    uint32_t last_any_motion_ms;
    uint32_t last_curious_ms;
    uint32_t last_peak_ms;
    uint32_t peak_window_ms;
    uint8_t peak_count;
    int8_t tilt_zone;
} round_reactions_t;

void round_reactions_init(round_reactions_t *state, uint32_t now_ms);
round_gesture_t round_reactions_update(round_reactions_t *state,
                                       float x_g, float y_g, float z_g,
                                       uint32_t now_ms);
int8_t round_reactions_tilt_zone(const round_reactions_t *state);
uint32_t round_reactions_still_ms(const round_reactions_t *state,
                                  uint32_t now_ms);

#endif
