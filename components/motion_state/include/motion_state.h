#ifndef MOTION_STATE_H
#define MOTION_STATE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MOTION_STATE_FLUID = 0,
    MOTION_STATE_SLEEP,
    MOTION_STATE_TIME,
    MOTION_STATE_GAME,
} motion_state_t;

typedef enum {
    MOTION_EVENT_NONE = 0,
    MOTION_EVENT_MOVEMENT_DETECTED,
    MOTION_EVENT_STILLNESS_TIMEOUT,
    MOTION_EVENT_SHAKE_DETECTED,
    MOTION_EVENT_TIME_REQUESTED,
    MOTION_EVENT_TIME_TIMEOUT,
    MOTION_EVENT_GAME_REQUESTED,
    MOTION_EVENT_GAME_FINISHED,
} motion_event_t;

typedef struct {
    motion_state_t current_state;
    int64_t entered_at_us;
} motion_state_machine_t;

void motion_state_init(motion_state_machine_t *machine, int64_t now_us);

motion_state_t motion_state_get(const motion_state_machine_t *machine);

int64_t motion_state_entered_at_us(const motion_state_machine_t *machine);

motion_state_t motion_state_handle_event(motion_state_machine_t *machine,
                                         motion_event_t event,
                                         bool movement_recent,
                                         int64_t now_us);

const char *motion_state_name(motion_state_t state);

const char *motion_event_name(motion_event_t event);

#endif /* MOTION_STATE_H */
