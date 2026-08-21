#include "pet_behavior.h"

#include <stddef.h>

enum {
    /* Reaction lengths in milliseconds. Changing these changes the character. */
    WAKE_UP_MS = 1400,
    LOOK_MS = 1200,
    SURPRISED_MS = 1200,
    ANGRY_MS = 1800,
    HAPPY_MS = 2000,
    ACKNOWLEDGE_MS = 450,
    CONNECTING_MAX_MS = 10000,
    /* Asked for by hand, so long enough to actually play with. */
    ACT_FLUID_MS = 30000,
    ACT_STARGAZE_MS = 9000,
    ACT_WANDER_MS = 7000,

    /* A second shake inside this window means "annoyed", not "surprised". */
    SHAKE_ESCALATION_MS = 5000,

    /* Idle thresholds for the level-triggered base states. */
    BORED_AFTER_MS = 30000,
    ASLEEP_AFTER_MS = 300000,
    LOW_BATTERY_PERCENT = 15,

    /* Self-directed activity scheduling. */
    ACT_IDLE_WITH_SENSOR_MS = 45000,
    ACT_IDLE_SENSORLESS_MS = 20000,
    ACT_IDLE_MIN_MS = 10000,
    ACT_RETRY_MIN_MS = 40000,
    ACT_RETRY_SPAN_MS = 80000,
    ACT_RETRY_SENSORLESS_MIN_MS = 20000,
    ACT_RETRY_SENSORLESS_SPAN_MS = 40000,
    ACT_COOLDOWN_MS = 5 * 60 * 1000,
    ACT_MIN_SHOWN_MS = 1000,

    /* Memory of recent interaction. */
    BOND_GAIN = 5,
    BOND_GAIN_COOLDOWN_MS = 5000,
    BOND_DECAY_INTERVAL_MS = 10 * 60 * 1000,
    BOND_DECAY_STEP = 3,
    BOND_MAX = 100,
};

/*
 * Only the two solitary activities are scheduled. The particle game is not one
 * of them: it is played by tilting, so it needs a person. The pet asking for a
 * game nobody is there to play would be a stranger thing than silence.
 */
#define ACT_POOL_SIZE 2

/*
 * Wrap-safe comparison. A plain "now >= deadline" breaks once the millisecond
 * counter overflows; the signed difference keeps working across the wrap.
 */
static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static uint32_t elapsed_since(uint32_t now_ms, uint32_t start_ms)
{
    return now_ms - start_ms;
}

/* xorshift32: deterministic, so host tests can replay the same choices. */
static uint32_t random_next(pet_behavior_t *pet)
{
    uint32_t value = pet->random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    pet->random_state = value;
    return value;
}

static bool is_activity(pet_behavior_id_t id)
{
    return id == PET_ACT_FLUID || id == PET_ACT_STARGAZE ||
           id == PET_ACT_WANDER;
}

/* Position in the scheduler pool, or -1 for an activity it never picks. */
static int activity_index(pet_behavior_id_t id)
{
    switch (id) {
    case PET_ACT_STARGAZE: return 0;
    case PET_ACT_WANDER: return 1;
    default: return -1;
    }
}

static pet_behavior_id_t activity_at(int index)
{
    return (index == 0) ? PET_ACT_STARGAZE : PET_ACT_WANDER;
}

static uint32_t duration_for(pet_behavior_id_t id)
{
    switch (id) {
    case PET_WAKE_UP: return WAKE_UP_MS;
    case PET_LOOK_LEFT:
    case PET_LOOK_RIGHT: return LOOK_MS;
    case PET_SURPRISED: return SURPRISED_MS;
    case PET_ANGRY: return ANGRY_MS;
    case PET_HAPPY: return HAPPY_MS;
    case PET_ACKNOWLEDGE: return ACKNOWLEDGE_MS;
    case PET_CONNECTING: return CONNECTING_MAX_MS;
    case PET_ACT_FLUID: return ACT_FLUID_MS;
    case PET_ACT_STARGAZE: return ACT_STARGAZE_MS;
    case PET_ACT_WANDER: return ACT_WANDER_MS;
    default: return 0;
    }
}

static pet_priority_t priority_for(pet_behavior_id_t id)
{
    if (is_activity(id)) {
        return PET_PRIORITY_P3;
    }
    switch (id) {
    case PET_NEUTRAL:
    case PET_BORED:
    case PET_ASLEEP:
    case PET_SLEEPY:
    case PET_CHARGING:
        return PET_PRIORITY_P2;
    default:
        return PET_PRIORITY_P1;
    }
}

/* How long the pet must be left alone before it invents something to do. */
static uint32_t activity_idle_threshold(const pet_behavior_t *pet)
{
    uint32_t threshold = pet->sensor_present ? (uint32_t)ACT_IDLE_WITH_SENSOR_MS
                                             : (uint32_t)ACT_IDLE_SENSORLESS_MS;
    /* A well-handled pet is livelier: it starts its own activities sooner. */
    const uint32_t bond_bonus = (uint32_t)pet->bond * 100U;
    threshold = (threshold > bond_bonus) ? threshold - bond_bonus : 0U;
    return (threshold < ACT_IDLE_MIN_MS) ? (uint32_t)ACT_IDLE_MIN_MS : threshold;
}

static void schedule_activity_retry(pet_behavior_t *pet, uint32_t now_ms)
{
    const uint32_t base = pet->sensor_present
        ? (uint32_t)ACT_RETRY_MIN_MS : (uint32_t)ACT_RETRY_SENSORLESS_MIN_MS;
    const uint32_t span = pet->sensor_present
        ? (uint32_t)ACT_RETRY_SPAN_MS : (uint32_t)ACT_RETRY_SENSORLESS_SPAN_MS;
    pet->next_activity_ms = now_ms + base + random_next(pet) % (span + 1U);
}

static bool activity_in_history(const pet_behavior_t *pet, pet_behavior_id_t id)
{
    return pet->act_history[0] == id || pet->act_history[1] == id;
}

static bool activity_cooled_down(const pet_behavior_t *pet,
                                 pet_behavior_id_t id,
                                 uint32_t now_ms)
{
    const int index = activity_index(id);
    if (index < 0) {
        return false;
    }
    return !pet->act_seen[index] ||
           time_reached(now_ms, pet->act_last_done_ms[index] + ACT_COOLDOWN_MS);
}

static void record_activity(pet_behavior_t *pet,
                            pet_behavior_id_t id,
                            uint32_t now_ms)
{
    const int index = activity_index(id);
    if (index < 0) {
        return;
    }
    pet->act_history[1] = pet->act_history[0];
    pet->act_history[0] = id;
    pet->act_last_done_ms[index] = now_ms;
    pet->act_seen[index] = true;
}

/*
 * Prefer an activity that is both cooled down and not in the recent history.
 * If every cooled-down candidate was shown recently, take the oldest one so the
 * pet never repeats itself back to back.
 */
static pet_behavior_id_t pick_activity(pet_behavior_t *pet, uint32_t now_ms)
{
    pet_behavior_id_t fresh[ACT_POOL_SIZE];
    int fresh_count = 0;
    pet_behavior_id_t oldest = PET_NEUTRAL;
    uint32_t oldest_age = 0;

    for (int index = 0; index < ACT_POOL_SIZE; ++index) {
        const pet_behavior_id_t id = activity_at(index);
        if (!activity_cooled_down(pet, id, now_ms)) {
            continue;
        }
        if (!activity_in_history(pet, id)) {
            fresh[fresh_count++] = id;
            continue;
        }
        const uint32_t age = elapsed_since(now_ms, pet->act_last_done_ms[index]);
        if (oldest == PET_NEUTRAL || age > oldest_age) {
            oldest = id;
            oldest_age = age;
        }
    }

    if (fresh_count > 0) {
        return fresh[random_next(pet) % (uint32_t)fresh_count];
    }
    return oldest;
}

static void finish_transient(pet_behavior_t *pet,
                             uint32_t now_ms,
                             bool allow_followup)
{
    const pet_behavior_id_t finished = pet->transient_id;
    if (is_activity(finished) &&
        elapsed_since(now_ms, pet->transient_started_ms) >= ACT_MIN_SHOWN_MS) {
        /* Only a properly seen activity spends its cooldown. */
        record_activity(pet, finished, now_ms);
        schedule_activity_retry(pet, now_ms);
    }

    const pet_behavior_id_t followup = pet->followup_id;
    pet->transient_id = PET_NEUTRAL;
    pet->followup_id = PET_NEUTRAL;
    pet->transient_deadline_ms = 0;

    if (allow_followup && followup != PET_NEUTRAL) {
        pet->transient_id = followup;
        pet->transient_priority = priority_for(followup);
        pet->transient_started_ms = now_ms;
        pet->transient_deadline_ms = now_ms + duration_for(followup);
    }
}

static bool start_transient(pet_behavior_t *pet,
                            pet_behavior_id_t id,
                            pet_source_t source,
                            uint32_t now_ms)
{
    const pet_priority_t priority = priority_for(id);
    if (pet->transient_id != PET_NEUTRAL) {
        /* A strictly more important behaviour is never pushed aside. */
        if (pet->transient_priority < priority) {
            return false;
        }
        finish_transient(pet, now_ms, false);
    }

    pet->transient_id = id;
    pet->transient_priority = priority;
    pet->transient_source = source;
    pet->transient_started_ms = now_ms;
    pet->transient_deadline_ms = now_ms + duration_for(id);
    pet->followup_id = PET_NEUTRAL;
    return true;
}

/*
 * Any deliberate interaction cancels a self-directed activity immediately.
 * The particle game is the exception: it was asked for, and it is played by
 * handling the keychain, so the very act of playing must not end it.
 */
static void register_activity_reset(pet_behavior_t *pet, uint32_t now_ms)
{
    if (is_activity(pet->transient_id) &&
        pet->transient_id != PET_ACT_FLUID) {
        finish_transient(pet, now_ms, false);
    }
    pet->last_activity_ms = now_ms;
    pet->next_activity_ms = now_ms + activity_idle_threshold(pet);
}

static void gain_bond(pet_behavior_t *pet, uint32_t now_ms)
{
    if (!time_reached(now_ms, pet->last_bond_gain_ms + BOND_GAIN_COOLDOWN_MS)) {
        return;
    }
    pet->last_bond_gain_ms = now_ms;
    const uint32_t raised = (uint32_t)pet->bond + BOND_GAIN;
    pet->bond = (raised > BOND_MAX) ? BOND_MAX : (uint8_t)raised;
}

static void decay_bond(pet_behavior_t *pet, uint32_t now_ms)
{
    if (!time_reached(now_ms, pet->next_bond_decay_ms)) {
        return;
    }
    pet->next_bond_decay_ms = now_ms + BOND_DECAY_INTERVAL_MS;
    pet->bond = (pet->bond > BOND_DECAY_STEP)
        ? (uint8_t)(pet->bond - BOND_DECAY_STEP) : 0U;
}

static pet_behavior_id_t resolve_base(const pet_behavior_t *pet,
                                      const pet_context_t *context,
                                      uint32_t now_ms)
{
    const uint32_t idle_ms = elapsed_since(now_ms, pet->last_activity_ms);

    /*
     * Deep sleep is only safe when the accelerometer can wake the pet up again.
     * Without it the face would freeze forever, which reads as a crash.
     */
    if (context->sensor_present && idle_ms >= ASLEEP_AFTER_MS) {
        return PET_ASLEEP;
    }
    if (!context->charging &&
        context->battery_percent != PET_BATTERY_UNKNOWN &&
        context->battery_percent <= LOW_BATTERY_PERCENT) {
        return PET_SLEEPY;
    }
    if (context->charging) {
        return PET_CHARGING;
    }
    /* A pet that is handled often takes longer to get bored. */
    if (idle_ms >= (uint32_t)BORED_AFTER_MS + (uint32_t)pet->bond * 200U) {
        return PET_BORED;
    }
    return PET_NEUTRAL;
}

void pet_behavior_init(pet_behavior_t *pet, uint32_t now_ms, uint32_t seed)
{
    if (pet == NULL) {
        return;
    }
    for (size_t index = 0; index < sizeof(*pet); ++index) {
        ((uint8_t *)pet)[index] = 0U;
    }

    pet->random_state = (seed != 0U) ? seed : 0x9E3779B9U;
    pet->transient_id = PET_NEUTRAL;
    pet->followup_id = PET_NEUTRAL;
    pet->base_id = PET_NEUTRAL;
    pet->base_started_ms = now_ms;
    pet->act_history[0] = PET_NEUTRAL;
    pet->act_history[1] = PET_NEUTRAL;
    pet->last_activity_ms = now_ms;
    pet->last_bond_gain_ms = now_ms - BOND_GAIN_COOLDOWN_MS;
    pet->next_bond_decay_ms = now_ms + BOND_DECAY_INTERVAL_MS;
    pet->sensor_present = true;
    pet->next_activity_ms = now_ms + activity_idle_threshold(pet);
    pet->view.id = PET_NEUTRAL;
    pet->view.priority = PET_PRIORITY_P2;
    pet->view.battery_percent = PET_BATTERY_UNKNOWN;
    pet->initialized = true;
}

void pet_behavior_post(pet_behavior_t *pet, pet_event_t event, uint32_t now_ms)
{
    if (pet == NULL || !pet->initialized) {
        return;
    }

    switch (event) {
    case PET_EVENT_BOOT_COMPLETED:
        register_activity_reset(pet, now_ms);
        (void)start_transient(pet, PET_WAKE_UP, PET_SOURCE_BOOT, now_ms);
        break;

    case PET_EVENT_PICKED_UP:
        register_activity_reset(pet, now_ms);
        gain_bond(pet, now_ms);
        (void)start_transient(pet, PET_WAKE_UP, PET_SOURCE_MOTION, now_ms);
        break;

    case PET_EVENT_MOVEMENT:
        /* Keeps the pet awake without interrupting whatever it is showing. */
        register_activity_reset(pet, now_ms);
        break;

    case PET_EVENT_TILT_LEFT:
    case PET_EVENT_TILT_RIGHT:
        register_activity_reset(pet, now_ms);
        gain_bond(pet, now_ms);
        /*
         * During the particle game a tilt is the controller, not a request to
         * look somewhere. Starting a P1 glance here would outrank the game and
         * end it on the first move the player made.
         */
        if (pet->transient_id != PET_ACT_FLUID) {
            (void)start_transient(pet,
                                  (event == PET_EVENT_TILT_LEFT)
                                      ? PET_LOOK_LEFT : PET_LOOK_RIGHT,
                                  PET_SOURCE_MOTION, now_ms);
        }
        break;

    /*
     * One jolt startles, deliberate shaking annoys. This used to require the
     * repeated gesture twice inside five seconds - six jolts - to reach anger,
     * which fired so rarely that it read as broken.
     */
    case PET_EVENT_JOLT:
        register_activity_reset(pet, now_ms);
        gain_bond(pet, now_ms);
        (void)start_transient(pet, PET_SURPRISED, PET_SOURCE_MOTION, now_ms);
        break;

    case PET_EVENT_SHAKEN:
        register_activity_reset(pet, now_ms);
        gain_bond(pet, now_ms);
        (void)start_transient(pet, PET_ANGRY, PET_SOURCE_MOTION, now_ms);
        break;

    case PET_EVENT_PLAY_REQUESTED:
        /* Asked for by name, so it skips the scheduler entirely. */
        if (is_activity(pet->transient_id)) {
            finish_transient(pet, now_ms, false);
        }
        pet->last_activity_ms = now_ms;
        gain_bond(pet, now_ms);
        (void)start_transient(pet, PET_ACT_FLUID, PET_SOURCE_MOTION, now_ms);
        break;

    case PET_EVENT_CHARGER_ATTACHED:
        register_activity_reset(pet, now_ms);
        gain_bond(pet, now_ms);
        (void)start_transient(pet, PET_HAPPY, PET_SOURCE_POWER, now_ms);
        break;

    case PET_EVENT_CHARGER_DETACHED:
        register_activity_reset(pet, now_ms);
        (void)start_transient(pet, PET_ACKNOWLEDGE, PET_SOURCE_POWER, now_ms);
        break;

    case PET_EVENT_PHONE_WINDOW_OPEN:
        pet->phone_window_open = true;
        register_activity_reset(pet, now_ms);
        (void)start_transient(pet, PET_CONNECTING, PET_SOURCE_PHONE, now_ms);
        break;

    case PET_EVENT_PHONE_WINDOW_CLOSED:
        pet->phone_window_open = false;
        if (pet->transient_id == PET_CONNECTING) {
            finish_transient(pet, now_ms, false);
        }
        break;

    case PET_EVENT_PHONE_SYNCED:
        register_activity_reset(pet, now_ms);
        gain_bond(pet, now_ms);
        pet->phone_window_open = false;
        if (start_transient(pet, PET_ACKNOWLEDGE, PET_SOURCE_PHONE, now_ms)) {
            /* Nod first, then be visibly pleased about the new time. */
            pet->followup_id = PET_HAPPY;
        }
        break;

    default:
        break;
    }
}

void pet_behavior_update(pet_behavior_t *pet,
                         const pet_context_t *context,
                         uint32_t now_ms)
{
    if (pet == NULL || !pet->initialized || context == NULL) {
        return;
    }

    pet->sensor_present = context->sensor_present;
    pet->charging = context->charging;
    decay_bond(pet, now_ms);

    if (pet->transient_id != PET_NEUTRAL &&
        time_reached(now_ms, pet->transient_deadline_ms)) {
        finish_transient(pet, now_ms, true);
    }

    const pet_behavior_id_t base = resolve_base(pet, context, now_ms);
    if (base != pet->base_id) {
        pet->base_id = base;
        pet->base_started_ms = now_ms;
    }

    /*
     * The pet invents things to do while it is awake and unbothered. Charging
     * counts: a keychain on a charger is sitting on a desk being looked at,
     * which is the worst possible moment to freeze on one screen. Only the
     * sleepy and asleep states suppress activities.
     */
    const bool base_allows_activity =
        (base == PET_NEUTRAL || base == PET_BORED || base == PET_CHARGING);
    if (pet->transient_id == PET_NEUTRAL && base_allows_activity &&
        time_reached(now_ms, pet->next_activity_ms)) {
        const pet_behavior_id_t chosen = pick_activity(pet, now_ms);
        if (chosen != PET_NEUTRAL) {
            (void)start_transient(pet, chosen, PET_SOURCE_SCHEDULER, now_ms);
        } else {
            /* Nothing is off cooldown: retry later instead of spinning. */
            schedule_activity_retry(pet, now_ms);
        }
    }

    if (pet->transient_id != PET_NEUTRAL) {
        pet->view.id = pet->transient_id;
        pet->view.priority = pet->transient_priority;
        pet->view.source = pet->transient_source;
        pet->view.elapsed_ms = elapsed_since(now_ms, pet->transient_started_ms);
        pet->view.duration_ms = duration_for(pet->transient_id);
    } else {
        pet->view.id = base;
        pet->view.priority = PET_PRIORITY_P2;
        pet->view.source = PET_SOURCE_NONE;
        pet->view.elapsed_ms = elapsed_since(now_ms, pet->base_started_ms);
        pet->view.duration_ms = 0U;
    }
    pet->view.bond = pet->bond;
    pet->view.battery_percent = context->battery_percent;
    pet->view.battery_millivolts = context->battery_millivolts;
    pet->view.tilt_x = context->tilt_x;
    pet->view.tilt_y = context->tilt_y;
    pet->view.charging = context->charging;
}

const pet_view_t *pet_behavior_view(const pet_behavior_t *pet)
{
    return (pet != NULL) ? &pet->view : NULL;
}

const char *pet_behavior_name(pet_behavior_id_t id)
{
    switch (id) {
    case PET_NEUTRAL: return "neutral";
    case PET_BORED: return "bored";
    case PET_ASLEEP: return "asleep";
    case PET_SLEEPY: return "sleepy";
    case PET_CHARGING: return "charging";
    case PET_WAKE_UP: return "wake_up";
    case PET_LOOK_LEFT: return "look_left";
    case PET_LOOK_RIGHT: return "look_right";
    case PET_SURPRISED: return "surprised";
    case PET_ANGRY: return "angry";
    case PET_HAPPY: return "happy";
    case PET_ACKNOWLEDGE: return "acknowledge";
    case PET_CONNECTING: return "connecting";
    case PET_ACT_FLUID: return "act_fluid";
    case PET_ACT_STARGAZE: return "act_stargaze";
    case PET_ACT_WANDER: return "act_wander";
    default: return "unknown";
    }
}
