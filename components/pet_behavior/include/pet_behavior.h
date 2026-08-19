#ifndef PET_BEHAVIOR_H
#define PET_BEHAVIOR_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Procedural companion behaviour engine.
 *
 * The engine answers one question: "what is the character doing right now?".
 * It owns no pixels, no I2C and no Zephyr types, so the priority rules and the
 * timers can be compiled and tested on a host PC before the board is flashed.
 *
 * Two kinds of input exist on purpose:
 *   - events   (edge-triggered): "the keychain was shaken", posted once;
 *   - context  (level-triggered): "the charger is attached", passed every tick.
 * Mixing them up is the classic bug: a level condition sent as an event keeps
 * restarting its own animation and it never finishes.
 */

typedef enum {
    /* Lower number wins. P0 is reserved for protective reactions. */
    PET_PRIORITY_P0 = 0,
    PET_PRIORITY_P1 = 1,
    PET_PRIORITY_P2 = 2,
    PET_PRIORITY_P3 = 3,
} pet_priority_t;

typedef enum {
    /* Base states: level-triggered, they last while their condition holds. */
    PET_NEUTRAL,
    PET_BORED,
    PET_ASLEEP,
    PET_SLEEPY,
    PET_CHARGING,
    /* Reactions: transient, they expire on their own deadline. */
    PET_WAKE_UP,
    PET_LOOK_LEFT,
    PET_LOOK_RIGHT,
    PET_SURPRISED,
    PET_ANGRY,
    PET_HAPPY,
    PET_ACKNOWLEDGE,
    PET_CONNECTING,
    /* Self-directed activities, chosen by the P3 scheduler when left alone. */
    PET_ACT_FLUID,
    PET_ACT_STARGAZE,
    PET_ACT_WANDER,
    PET_ID_COUNT,
} pet_behavior_id_t;

typedef enum {
    PET_SOURCE_NONE,
    PET_SOURCE_BOOT,
    PET_SOURCE_MOTION,
    PET_SOURCE_POWER,
    PET_SOURCE_PHONE,
    PET_SOURCE_SCHEDULER,
} pet_source_t;

typedef enum {
    PET_EVENT_BOOT_COMPLETED,
    /* Movement after a period of stillness: the keychain was picked up. */
    PET_EVENT_PICKED_UP,
    /* Ordinary movement: resets the idle timers without a new reaction. */
    PET_EVENT_MOVEMENT,
    PET_EVENT_TILT_LEFT,
    PET_EVENT_TILT_RIGHT,
    PET_EVENT_SHAKEN,
    PET_EVENT_CHARGER_ATTACHED,
    PET_EVENT_CHARGER_DETACHED,
    /* The BLE advertising window opened and the phone may connect. */
    PET_EVENT_PHONE_WINDOW_OPEN,
    PET_EVENT_PHONE_WINDOW_CLOSED,
    PET_EVENT_PHONE_SYNCED,
    PET_EVENT_COUNT,
} pet_event_t;

typedef struct {
    /*
     * False while the accelerometer is not soldered yet. The engine then stops
     * expecting motion events: deep sleep is disabled so the face never freezes,
     * and self-directed activities are scheduled more often so it stays alive.
     */
    bool sensor_present;
    bool charging;
    /* 0..100, or PET_BATTERY_UNKNOWN while no ADC channel exists yet. */
    uint8_t battery_percent;
} pet_context_t;

#define PET_BATTERY_UNKNOWN 255U

typedef struct {
    pet_behavior_id_t id;
    pet_priority_t priority;
    pet_source_t source;
    /* How long the current behaviour has been running. Drives all animation. */
    uint32_t elapsed_ms;
    /* 0 for level-triggered base states, otherwise the planned length. */
    uint32_t duration_ms;
    /* Memory of recent interaction, 0..100. Higher = friendlier and livelier. */
    uint8_t bond;
    uint8_t battery_percent;
    bool charging;
} pet_view_t;

typedef struct {
    pet_view_t view;
    pet_behavior_id_t transient_id;
    pet_behavior_id_t followup_id;
    pet_priority_t transient_priority;
    pet_source_t transient_source;
    uint32_t transient_started_ms;
    uint32_t transient_deadline_ms;
    uint32_t base_started_ms;
    pet_behavior_id_t base_id;
    uint32_t last_activity_ms;
    uint32_t last_bond_gain_ms;
    uint32_t next_bond_decay_ms;
    uint32_t next_activity_ms;
    uint32_t shake_window_ms;
    uint32_t random_state;
    uint32_t act_last_done_ms[3];
    pet_behavior_id_t act_history[2];
    bool act_seen[3];
    uint8_t bond;
    bool phone_window_open;
    bool charging;
    bool sensor_present;
    bool initialized;
} pet_behavior_t;

void pet_behavior_init(pet_behavior_t *pet, uint32_t now_ms, uint32_t seed);

/* Report an edge-triggered event. Safe to call from the main loop only. */
void pet_behavior_post(pet_behavior_t *pet, pet_event_t event, uint32_t now_ms);

/* Advance timers and resolve the winning behaviour. Never blocks. */
void pet_behavior_update(pet_behavior_t *pet,
                         const pet_context_t *context,
                         uint32_t now_ms);

const pet_view_t *pet_behavior_view(const pet_behavior_t *pet);

/* Stable short names for logging: "bored", "angry", ... */
const char *pet_behavior_name(pet_behavior_id_t id);

#endif /* PET_BEHAVIOR_H */
