#include "pet_face.h"

#include <math.h>
#include <stddef.h>

#include "oled_display.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define EYE_LEFT_X 42
#define EYE_RIGHT_X 86
#define EYE_CENTER_Y 32
#define EYE_BASE_WIDTH 26
#define EYE_BASE_HEIGHT 28
#define EYE_BASE_RADIUS 8

#define BLINK_DURATION_MS 140U
#define BLINK_INTERVAL_MIN_MS 2600U
#define BLINK_INTERVAL_SPAN_MS 2600U
#define BLINK_INTERVAL_FLOOR_MS 1200U

#define TWO_PI 6.28318531f

typedef enum {
    OVERLAY_NONE,
    OVERLAY_SLEEP_Z,
    OVERLAY_BATTERY,
    OVERLAY_SPARKLE,
    OVERLAY_SCAN,
    OVERLAY_STARS,
} pet_overlay_t;

/* Everything the renderer needs for one frame, derived from id + elapsed_ms. */
typedef struct {
    int eye_width;
    int eye_height;
    int radius;
    int gaze_x;
    int gaze_y;
    int group_dx;
    int group_dy;
    /*
     * Lids as a fraction of eye height rather than pixels, so a mood keeps its
     * proportions when the eye grows or shrinks. Ported from the Pip engine.
     */
    int lid_top_percent;
    int lid_bottom_percent;
    /* Non-zero draws an inner-down brow instead of a flat top lid. */
    int glare_percent;
    bool arc_eyes;
    bool allow_blink;
    /*
     * Whether the gaze should follow the live tilt. True for the resting
     * states, so a keychain held at an angle keeps the eyes turned that way
     * instead of snapping back to centre once a scripted reaction ends.
     */
    bool track_tilt;
    /*
     * Rotation of the whole eye pair about the centre of the face, in radians.
     * Only the tumble uses it, but it belongs to the frame rather than to that
     * one case, so any later behaviour can lean the face over.
     */
    float rotation;
    pet_overlay_t overlay;
} face_frame_t;

/* How far the gaze travels at full tilt, in pixels. */
#define TILT_GAZE_RANGE_X 12
#define TILT_GAZE_RANGE_Y 5

/*
 * The eased pose, and the spontaneous glances, are where the Pip engine's
 * liveliness actually comes from - not from the lid shapes. Two things it does
 * that this renderer did not: every gaze and size target is approached
 * exponentially rather than jumped to, and the eyes dart somewhere and come
 * back every few seconds even when nothing has happened.
 *
 * Originally created by Hamza Yeşilmen (HamzaYslmn).
 * Source:  https://github.com/HamzaYslmn/
 * Sponsor: https://github.com/sponsors/HamzaYslmn
 */
#define POSE_TAU_GAZE_MS 90.0f
#define POSE_TAU_SIZE_MS 110.0f
#define IDLE_GLANCE_MIN_MS 1500U
#define IDLE_GLANCE_SPAN_MS 3500U
#define IDLE_GLANCE_HOLD_MS 620U

static float s_pose_gaze_x;
static float s_pose_gaze_y;
static float s_pose_eye_width;
static float s_pose_eye_height;
static uint32_t s_pose_last_ms;
static bool s_pose_started;

static uint32_t s_next_glance_ms;
static uint32_t s_glance_started_ms;
static int s_glance_x;
static int s_glance_y;
static bool s_glancing;

static uint32_t s_random_state = 0x9E3779B9U;
static uint32_t s_next_blink_ms;
static uint32_t s_blink_started_ms;
static bool s_blinking;
static bool s_blink_scheduled;

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static uint32_t random_next(void)
{
    uint32_t value = s_random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    s_random_state = value;
    return value;
}

static float clamp01(float value)
{
    if (value < 0.0f) { return 0.0f; }
    if (value > 1.0f) { return 1.0f; }
    return value;
}

/* Position inside a repeating cycle, 0.0 at the start and 1.0 at the end. */
static float phase(uint32_t now_ms, uint32_t period_ms)
{
    if (period_ms == 0U) { return 0.0f; }
    return (float)(now_ms % period_ms) / (float)period_ms;
}

/* Smooth -1..+1 oscillation, used for breathing and drifting. */
static float oscillate(uint32_t now_ms, uint32_t period_ms)
{
    return sinf(phase(now_ms, period_ms) * TWO_PI);
}

/* Fast at the start, gentle at the end: the way living things move. */
static float ease_out(float t)
{
    const float inverted = 1.0f - clamp01(t);
    return 1.0f - inverted * inverted;
}

static int lerp_int(int from, int to, float t)
{
    return from + (int)((float)(to - from) * clamp01(t) + 0.5f);
}

/* Frame-rate independent exponential approach of current towards target. */
static float ease_towards(float current, float target, float dt_ms, float tau_ms)
{
    return target + (current - target) * expf(-dt_ms / tau_ms);
}

/*
 * A spontaneous glance: the eyes dart somewhere and come back, every few
 * seconds, whether or not anything happened. This is the single strongest cue
 * that something is alive rather than merely displayed.
 */
static void update_idle_glance(uint32_t now_ms, int *gaze_x, int *gaze_y)
{
    if (s_next_glance_ms == 0U) {
        s_next_glance_ms = now_ms + IDLE_GLANCE_MIN_MS;
    }

    if (!s_glancing && time_reached(now_ms, s_next_glance_ms)) {
        s_glancing = true;
        s_glance_started_ms = now_ms;
        /* Off to one side, a little up or down, never far. */
        s_glance_x = (int)(random_next() % 19U) - 9;
        s_glance_y = (int)(random_next() % 7U) - 3;
    }

    if (!s_glancing) {
        return;
    }

    const uint32_t elapsed = now_ms - s_glance_started_ms;
    if (elapsed >= IDLE_GLANCE_HOLD_MS) {
        s_glancing = false;
        s_next_glance_ms = now_ms + IDLE_GLANCE_MIN_MS +
                           random_next() % (IDLE_GLANCE_SPAN_MS + 1U);
        return;
    }

    /* Hold near the target for most of it; the ease does the travelling. */
    *gaze_x += s_glance_x;
    *gaze_y += s_glance_y;
}

static void fill_rect(int x, int y, int width, int height, bool on)
{
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            oled_display_set_pixel(x + column, y + row, on);
        }
    }
}

static void draw_rect_outline(int x, int y, int width, int height)
{
    for (int column = 0; column < width; ++column) {
        oled_display_set_pixel(x + column, y, true);
        oled_display_set_pixel(x + column, y + height - 1, true);
    }
    for (int row = 0; row < height; ++row) {
        oled_display_set_pixel(x, y + row, true);
        oled_display_set_pixel(x + width - 1, y + row, true);
    }
}

static void draw_vertical_line(int x, int y_start, int y_end)
{
    for (int y = y_start; y <= y_end; ++y) {
        oled_display_set_pixel(x, y, true);
    }
}

static void draw_dot(int x, int y)
{
    oled_display_set_pixel(x, y, true);
    oled_display_set_pixel(x + 1, y, true);
}

static void draw_sparkle(int x, int y, int size)
{
    for (int offset = -size; offset <= size; ++offset) {
        oled_display_set_pixel(x + offset, y, true);
        oled_display_set_pixel(x, y + offset, true);
    }
}

/*
 * Rounded rectangle centred on (cx, cy). The corner inset comes from a circle,
 * which is what makes the eye read as a soft shape instead of a box.
 */
static void fill_round_rect(int cx, int cy, int width, int height,
                            int radius, bool on)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    if (radius * 2 > width) { radius = width / 2; }
    if (radius * 2 > height) { radius = height / 2; }

    const int x0 = cx - width / 2;
    const int y0 = cy - height / 2;
    for (int row = 0; row < height; ++row) {
        int inset = 0;
        const int from_top = radius - row;
        const int from_bottom = row - (height - 1 - radius);
        const int corner = (from_top > 0) ? from_top
                         : ((from_bottom > 0) ? from_bottom : 0);
        if (corner > 0) {
            const float chord =
                sqrtf((float)(radius * radius - corner * corner));
            inset = radius - (int)(chord + 0.5f);
        }
        for (int column = inset; column < width - inset; ++column) {
            oled_display_set_pixel(x0 + column, y0 + row, on);
        }
    }
}

/*
 * The three lid shapes below are ported from the Pip eye engine, whose whole
 * mood vocabulary is a handful of lid carves over a rounded rectangle. A mood
 * there is one line - anger is a glare, boredom is flat half-lids - and the
 * expressiveness comes from having the right few shapes rather than from many
 * of them.
 *
 * Originally created by Hamza Yeşilmen (HamzaYslmn).
 * Source:  https://github.com/HamzaYslmn/
 * Sponsor: https://github.com/sponsors/HamzaYslmn
 * Licence: THIRD_PARTY_LICENSES/esp-bridge-mcp-robot-LICENSE.txt
 * Adapted to C for a 128x64 mono display; not the original implementation.
 */

/*
 * Flat lids: cover down to top_percent of the eye, and up from bottom_percent.
 * A lower lid is what separates a squint from a droop, and this renderer had
 * no way to draw one before.
 */
static void paint_lids(int cx, int cy, int width, int height,
                       int top_percent, int bottom_percent)
{
    const int x0 = cx - width / 2;
    const int y0 = cy - height / 2;

    if (top_percent > 0) {
        const int depth = (height * top_percent) / 100;
        fill_rect(x0, y0, width, depth, false);
    }
    if (bottom_percent < 100) {
        const int edge = (height * bottom_percent) / 100;
        fill_rect(x0, y0 + edge, width, height - edge, false);
    }
}

/*
 * Inner-down brow: a triangle whose tip drops towards the nose. This is the
 * shape that reads as a glare, and it is markedly angrier than shaving a
 * slanted band off the top.
 */
static void paint_glare(int cx, int cy, int width, int height,
                        int depth_percent, bool is_right)
{
    const int x0 = cx - width / 2;
    const int y0 = cy - height / 2;
    const int tip_depth = (height * depth_percent) / 100;

    for (int column = 0; column < width; ++column) {
        /* Full height at the outer edge, tapering to the tip at the nose. */
        const int from_outer = is_right ? (width - 1 - column) : column;
        const int depth = (tip_depth * from_outer) / (width > 1 ? width - 1 : 1);
        for (int row = 0; row < depth && row < height; ++row) {
            oled_display_set_pixel(x0 + column, y0 + row, false);
        }
    }
}

/*
 * Erase a slanted band from the top of an eye. A positive slant makes the lid
 * dig deeper on the right-hand side, which is how an angry brow is built.
 */
static void cut_lid(int cx, int cy, int width, int height,
                    int depth, int slant)
{
    if (width <= 1 || depth <= 0) {
        return;
    }
    const int x0 = cx - width / 2;
    const int y0 = cy - height / 2;
    for (int column = 0; column < width; ++column) {
        const int across = 2 * column - (width - 1);
        const int local_depth = depth + (slant * across) / (width - 1);
        for (int row = 0; row < local_depth && row < height; ++row) {
            oled_display_set_pixel(x0 + column, y0 + row, false);
        }
    }
}

/* A happy eye is an arch: a parabola peaking in the middle, drawn thick. */
static void draw_arc_eye(int cx, int cy, int width, int height, int thickness)
{
    const int half = width / 2;
    if (half <= 0) {
        return;
    }
    for (int dx = -half; dx <= half; ++dx) {
        const int y = cy - height / 2 + (dx * dx * height) / (half * half);
        for (int layer = 0; layer < thickness; ++layer) {
            oled_display_set_pixel(cx + dx, y + layer, true);
        }
    }
}

static void schedule_blink(uint32_t now_ms, uint8_t bond)
{
    uint32_t interval =
        BLINK_INTERVAL_MIN_MS + random_next() % (BLINK_INTERVAL_SPAN_MS + 1U);
    /* A pet that is handled often blinks a little more, so it looks awake. */
    const uint32_t bond_speedup = (uint32_t)bond * 8U;
    interval = (interval > bond_speedup) ? interval - bond_speedup : interval;
    if (interval < BLINK_INTERVAL_FLOOR_MS) {
        interval = BLINK_INTERVAL_FLOOR_MS;
    }
    s_next_blink_ms = now_ms + interval;
    s_blink_scheduled = true;
}

/* Returns how open the eyes are right now, 0..100 percent. */
static int blink_openness(uint32_t now_ms, bool allow_blink, uint8_t bond)
{
    if (!s_blink_scheduled) {
        schedule_blink(now_ms, bond);
    }
    if (!allow_blink) {
        s_blinking = false;
        return 100;
    }
    if (!s_blinking && time_reached(now_ms, s_next_blink_ms)) {
        s_blinking = true;
        s_blink_started_ms = now_ms;
    }
    if (!s_blinking) {
        return 100;
    }

    const uint32_t elapsed = now_ms - s_blink_started_ms;
    if (elapsed >= BLINK_DURATION_MS) {
        s_blinking = false;
        schedule_blink(now_ms, bond);
        return 100;
    }

    const uint32_t half = BLINK_DURATION_MS / 2U;
    if (elapsed < half) {
        return 100 - (int)((90U * elapsed) / half);
    }
    return 10 + (int)((90U * (elapsed - half)) / half);
}

static void build_frame(const pet_view_t *view, uint32_t now_ms,
                        face_frame_t *frame)
{
    /* Defaults describe a calm, centred, normally blinking face. */
    frame->eye_width = EYE_BASE_WIDTH;
    frame->eye_height = EYE_BASE_HEIGHT;
    frame->radius = EYE_BASE_RADIUS;
    frame->gaze_x = 0;
    frame->gaze_y = 0;
    frame->group_dx = 0;
    frame->group_dy = 0;
    frame->lid_top_percent = 0;
    frame->lid_bottom_percent = 100;
    frame->glare_percent = 0;
    frame->arc_eyes = false;
    frame->allow_blink = true;
    frame->track_tilt = false;
    frame->rotation = 0.0f;
    frame->overlay = OVERLAY_NONE;

    const uint32_t elapsed = view->elapsed_ms;
    const float progress = (view->duration_ms > 0U)
        ? clamp01((float)elapsed / (float)view->duration_ms) : 0.0f;

    switch (view->id) {
    case PET_NEUTRAL:
        /* One pixel of drift stops a still face from looking like a freeze. */
        frame->group_dy = (int)(oscillate(now_ms, 4200U) * 1.4f);
        /* Familiarity shows up as slightly wider, friendlier eyes. */
        frame->eye_width += view->bond / 25;
        frame->track_tilt = true;
        break;

    case PET_BORED: {
        frame->eye_width = 24;
        frame->eye_height = 22;
        frame->lid_top_percent = 50;   /* Pip's bored: flat half-lids. */

        /*
         * The gaze parks off to one side and stays there for a few seconds
         * before moving on. A continuous sweep reads as searching for
         * something, and boredom is the opposite of searching.
         */
        static const int parks[4] = {-9, 7, -6, 9};
        frame->gaze_x = parks[(now_ms / 3200U) % 4U];

        /* And every few seconds, a whole-face sigh. */
        const float sigh_phase = phase(now_ms, 5000U);
        if (sigh_phase > 0.86f) {
            const float sigh =
                sinf((sigh_phase - 0.86f) / 0.14f * (TWO_PI / 2.0f));
            frame->group_dy = (int)(sigh * 5.0f);
            frame->eye_height -= (int)(sigh * 7.0f);
        }
        frame->track_tilt = true;
        break;
    }

    case PET_ASLEEP:
        frame->eye_width = 26;
        frame->eye_height = 4;
        frame->radius = 2;
        frame->allow_blink = false;
        frame->group_dy = (int)(oscillate(now_ms, 3600U) * 1.5f);
        frame->overlay = OVERLAY_SLEEP_Z;
        break;

    case PET_SLEEPY: {
        frame->eye_height = 26;
        frame->gaze_y = 3;

        /*
         * Nodding off and catching itself: the lids creep shut over about two
         * and a half seconds while the head sinks, then both snap back. That
         * is a fight against sleep, which is a different thing from being
         * asleep and from being bored, and it is legible from across a room in
         * a way that a fixed eyelid depth is not.
         */
        const float nod = phase(now_ms, 3000U);
        if (nod < 0.85f) {
            const float falling = nod / 0.85f;
            frame->lid_top_percent = 15 + (int)(falling * 65.0f);
            frame->group_dy = (int)(falling * 5.0f);
        } else {
            const float waking = (nod - 0.85f) / 0.15f;
            frame->lid_top_percent = 80 - (int)(waking * 65.0f);
            frame->group_dy = (int)((1.0f - waking) * 5.0f);
        }

        /* A low battery is exactly when the actual level is worth showing. */
        frame->overlay = OVERLAY_BATTERY;
        frame->track_tilt = true;
        break;
    }

    case PET_CHARGING:
        frame->eye_height = 26;
        frame->group_dy = (int)(oscillate(now_ms, 1800U) * 2.0f);
        frame->overlay = OVERLAY_BATTERY;
        frame->track_tilt = true;
        break;

    case PET_WAKE_UP: {
        /* The eyes snap open, the pet glances up, then everything settles. */
        const float opening = ease_out((float)elapsed / 400.0f);
        frame->eye_width = lerp_int(20, EYE_BASE_WIDTH + 2, opening);
        frame->eye_height = lerp_int(10, EYE_BASE_HEIGHT + 4, opening);
        frame->gaze_y = lerp_int(-7, 0, ease_out(progress));
        frame->group_dy = lerp_int(-3, 0, ease_out(progress));
        break;
    }

    case PET_LOOK_LEFT:
    case PET_LOOK_RIGHT: {
        /*
         * A quick snap toward the new direction, then hold. It deliberately
         * does not return to centre: when this expires the resting state picks
         * the gaze up from the live tilt, so a keychain still held at an angle
         * keeps its eyes turned rather than flicking back and forth.
         */
        const int target = (view->id == PET_LOOK_LEFT) ? -11 : 11;
        frame->gaze_x = (elapsed < 150U)
            ? lerp_int(0, target, ease_out(elapsed / 150.0f))
            : target;
        break;
    }

    case PET_SURPRISED: {
        frame->eye_width = 32;
        frame->eye_height = 34;
        frame->radius = 12;
        /* One quick hop whose amplitude dies out over the reaction. */
        const float damping = 1.0f - progress;
        frame->group_dy = (int)(sinf((float)elapsed * 0.018f) * -5.0f * damping);
        break;
    }

    case PET_ANGRY:
        frame->eye_width = 30;
        frame->eye_height = 24;
        frame->glare_percent = 60;     /* Pip's angry: inner-down brow. */
        if (elapsed < 350U) {
            /* A short tremble, then the pet just glares. */
            frame->group_dx = ((elapsed / 45U) % 2U == 0U) ? 3 : -3;
        }
        break;

    case PET_HAPPY:
        frame->arc_eyes = true;
        frame->eye_width = 30;
        frame->eye_height = 12;
        frame->allow_blink = false;
        frame->group_dy = (int)(oscillate(now_ms, 700U) * 2.0f);
        frame->overlay = OVERLAY_SPARKLE;
        break;

    case PET_ACKNOWLEDGE: {
        /* A single nod: down, squash, back up. */
        const float nod = sinf(progress * (TWO_PI / 2.0f));
        frame->group_dy = (int)(nod * 5.0f);
        frame->eye_height =
            lerp_int(EYE_BASE_HEIGHT, (EYE_BASE_HEIGHT * 3) / 4, nod);
        break;
    }

    case PET_CONNECTING: {
        frame->eye_width = 24;
        frame->eye_height = 24;
        const int scan_x = 8 + (int)(phase(now_ms, 800U) * 112.0f);
        /* The eyes track the scanning line instead of staring blankly. */
        frame->gaze_x = (scan_x - SCREEN_WIDTH / 2) / 6;
        frame->overlay = OVERLAY_SCAN;
        break;
    }

    case PET_ACT_STARGAZE:
        frame->eye_width = 24;
        frame->eye_height = 24;
        frame->gaze_y = -6;
        frame->gaze_x = (int)(oscillate(now_ms, 5200U) * 4.0f);
        frame->overlay = OVERLAY_STARS;
        break;

    case PET_UPSIDE_DOWN: {
        /*
         * Indignant. The eyes tumble over during the first second and then stay
         * where they have no business being, until the keychain is turned back.
         */
        const float turn = ease_out((float)elapsed / 1000.0f);
        frame->rotation = turn * (TWO_PI / 2.0f);
        frame->eye_width = 30;
        frame->eye_height = 24;
        frame->glare_percent = 45;
        /* A slow indignant wobble once it has settled upside down. */
        frame->group_dx = (int)(oscillate(now_ms, 900U) * 2.0f * turn);
        break;
    }

    case PET_SOOTHED:
        /*
         * Told apart from bored and sleepy by motion rather than by shape.
         * All three are "heavy lids", and on a 128x64 display the difference
         * between four and thirteen pixels of eyelid is not something anyone
         * can name. So this one leans along with the rocking: the giveaway is
         * that the face is going with you, not that it looks tired.
         */
        frame->eye_width = 28;
        /* Breathing, slow enough to read as calm rather than as animation. */
        frame->eye_height = 22 + (int)(oscillate(now_ms, 3400U) * 3.0f);
        frame->lid_top_percent = 45;   /* Pip's chill: heavy-lidded. */
        frame->gaze_y = 2;
        /*
         * A deliberately large lean. Rocking measures about a third of a g,
         * which is a tilt reading near 35, so a gentle coefficient turned into
         * two or three pixels of eye offset - invisible. At this rate the same
         * rocking swings the eyes through a third of the display height, which
         * is the whole point: the face is visibly going along with you.
         */
        frame->rotation = (float)view->tilt_x * 0.012f;
        if (frame->rotation > 0.55f) { frame->rotation = 0.55f; }
        if (frame->rotation < -0.55f) { frame->rotation = -0.55f; }
        frame->group_dx = (view->tilt_x * 8) / 100;
        break;

    case PET_STARTLED: {
        /* Eyes as wide as they go, and a hard shake that decays. */
        const float damping = 1.0f - progress;
        frame->eye_width = 36;
        frame->eye_height = 40;
        frame->radius = 14;
        frame->allow_blink = false;
        frame->group_dx = (int)(sinf((float)elapsed * 0.09f) * 5.0f * damping);
        frame->group_dy = (int)(sinf((float)elapsed * 0.07f) * 4.0f * damping);
        break;
    }

    case PET_CURIOUS: {
        /*
         * Looking around: the gaze holds three positions rather than sweeping,
         * because a search reads as a series of glances, not as a pan.
         */
        enum { CURIOUS_STOP_COUNT = 4 };
        static const int stops[CURIOUS_STOP_COUNT] = {-9, 7, -4, 0};
        const uint32_t span = (view->duration_ms > 0U) ? view->duration_ms : 1U;
        uint32_t step = (elapsed * CURIOUS_STOP_COUNT) / span;
        if (step >= CURIOUS_STOP_COUNT) {
            step = CURIOUS_STOP_COUNT - 1U;
        }
        frame->gaze_x = stops[step];
        frame->gaze_y = -2;
        frame->eye_width = 28;
        frame->eye_height = 30;
        /* A small head tilt sells the curiosity more than the eyes do. */
        frame->rotation = 0.12f;
        break;
    }

    case PET_GREETING:
        /* Arcs like the happy face, plus a wave: the group leans side to side. */
        frame->arc_eyes = true;
        frame->eye_width = 30;
        frame->eye_height = 12;
        frame->allow_blink = false;
        frame->group_dx = (int)(oscillate(now_ms, 500U) * 4.0f);
        frame->rotation = oscillate(now_ms, 500U) * 0.10f;
        frame->overlay = OVERLAY_SPARKLE;
        break;

    case PET_ACT_WANDER: {
        /* The gaze walks a slow circle: the pet is busy with its own thoughts. */
        const float angle = phase(now_ms, 2400U) * TWO_PI;
        frame->gaze_x = (int)(cosf(angle) * 9.0f);
        frame->gaze_y = (int)(sinf(angle) * 5.0f);
        break;
    }

    default:
        break;
    }

    /*
     * Follow the keychain. Added on top of whatever the state already does, so
     * a bored wander still happens but a deliberate tilt pulls the gaze with
     * it and holds it there for as long as the angle is held.
     */
    if (frame->track_tilt) {
        frame->gaze_x += (view->tilt_x * TILT_GAZE_RANGE_X) / 100;
        frame->gaze_y += (view->tilt_y * TILT_GAZE_RANGE_Y) / 100;
    }

    /*
     * The gauge rides on top of whatever the pet is doing, rather than the
     * charger replacing it. Behaviours with something of their own to show -
     * sparkles, a scan line - keep it.
     */
    if (view->charging && frame->overlay == OVERLAY_NONE) {
        frame->overlay = OVERLAY_BATTERY;
    }
}

static void draw_overlay(const face_frame_t *frame, const pet_view_t *view,
                         uint32_t now_ms)
{
    switch (frame->overlay) {
    case OVERLAY_SLEEP_Z:
        for (int index = 0; index < 3; ++index) {
            const float rise = phase(now_ms + (uint32_t)index * 900U, 2700U);
            if (rise >= 0.85f) {
                continue;
            }
            oled_display_draw_text(100 + index * 5,
                                   30 - (int)(rise * 22.0f), "z");
        }
        break;

    case OVERLAY_BATTERY: {
        const int x = 50;
        const int y = 2;
        const int width = 28;
        const int height = 9;
        draw_rect_outline(x, y, width, height);
        fill_rect(x + width, y + 3, 2, 3, true);

        if (view->battery_percent == PET_BATTERY_UNKNOWN) {
            /* Nothing measured: sweep rather than invent a level. */
            const int sweep = (int)((float)(width - 4) * phase(now_ms, 2000U));
            if (sweep > 0) {
                fill_rect(x + 2, y + 2, sweep, height - 4, true);
            }
            break;
        }

        const int fill = ((width - 4) * view->battery_percent) / 100;
        if (fill > 0) {
            fill_rect(x + 2, y + 2, fill, height - 4, true);
        }

        /* The number the gauge stands for, written out. Right-aligned against
         * the gauge so it does not shift as it goes from 100 to 9. */
        char text[5];
        int length = 0;
        if (view->battery_percent >= 100U) {
            text[length++] = '1';
            text[length++] = '0';
            text[length++] = '0';
        } else {
            if (view->battery_percent >= 10U) {
                text[length++] = (char)('0' + view->battery_percent / 10U);
            }
            text[length++] = (char)('0' + view->battery_percent % 10U);
        }
        text[length++] = '%';
        text[length] = '\0';
        oled_display_draw_text(x - 2 - length * 6, y + 1, text);
        break;
    }

    case OVERLAY_SPARKLE: {
        static const int spots[4][2] = {
            {22, 14}, {106, 16}, {18, 48}, {110, 46},
        };
        for (int index = 0; index < 4; ++index) {
            if (phase(now_ms + (uint32_t)index * 300U, 1200U) < 0.6f) {
                draw_sparkle(spots[index][0], spots[index][1], 3);
            }
        }
        break;
    }

    case OVERLAY_SCAN:
        draw_vertical_line(8 + (int)(phase(now_ms, 800U) * 112.0f), 4, 60);
        break;

    case OVERLAY_STARS:
        for (int index = 0; index < 6; ++index) {
            const float rise = phase(now_ms + (uint32_t)index * 700U, 4200U);
            draw_dot(10 + index * 21, 60 - (int)(rise * 56.0f));
        }
        break;

    default:
        break;
    }
}

void pet_face_reset(uint32_t seed)
{
    s_random_state = (seed != 0U) ? seed : 0x9E3779B9U;
    s_blinking = false;
    s_blink_scheduled = false;
    s_next_blink_ms = 0U;
    s_blink_started_ms = 0U;
}

esp_err_t pet_face_render(const pet_view_t *view, uint32_t now_ms)
{
    if (view == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /* FLIP already owns the whole framebuffer; the caller renders that one. */
    if (view->id == PET_ACT_FLUID) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    face_frame_t frame;
    build_frame(view, now_ms, &frame);

    /* Idle glances belong to the resting states; a reaction owns its own gaze. */
    if (frame.track_tilt) {
        update_idle_glance(now_ms, &frame.gaze_x, &frame.gaze_y);
    }

    /*
     * Glide the gaze and the eye size towards their targets instead of jumping.
     * Everything else - the trembles, the hops - is meant to be abrupt and is
     * left alone.
     */
    if (!s_pose_started) {
        s_pose_started = true;
        s_pose_last_ms = now_ms;
        s_pose_gaze_x = (float)frame.gaze_x;
        s_pose_gaze_y = (float)frame.gaze_y;
        s_pose_eye_width = (float)frame.eye_width;
        s_pose_eye_height = (float)frame.eye_height;
    }
    float dt_ms = (float)(now_ms - s_pose_last_ms);
    s_pose_last_ms = now_ms;
    if (dt_ms < 1.0f) { dt_ms = 1.0f; }
    if (dt_ms > 120.0f) { dt_ms = 120.0f; }

    s_pose_gaze_x = ease_towards(s_pose_gaze_x, (float)frame.gaze_x,
                                 dt_ms, POSE_TAU_GAZE_MS);
    s_pose_gaze_y = ease_towards(s_pose_gaze_y, (float)frame.gaze_y,
                                 dt_ms, POSE_TAU_GAZE_MS);
    s_pose_eye_width = ease_towards(s_pose_eye_width, (float)frame.eye_width,
                                    dt_ms, POSE_TAU_SIZE_MS);
    s_pose_eye_height = ease_towards(s_pose_eye_height, (float)frame.eye_height,
                                     dt_ms, POSE_TAU_SIZE_MS);

    frame.gaze_x = (int)(s_pose_gaze_x + 0.5f);
    frame.gaze_y = (int)(s_pose_gaze_y + 0.5f);
    frame.eye_width = (int)(s_pose_eye_width + 0.5f);
    frame.eye_height = (int)(s_pose_eye_height + 0.5f);

    const int openness = blink_openness(now_ms, frame.allow_blink, view->bond);
    int eye_height = (frame.eye_height * openness) / 100;
    if (eye_height < 2) {
        eye_height = 2;
    }

    /* Parallax: the eye on the side being looked at swells, the far one thins.
     * A pair of eyes moving in perfect lockstep reads as a graphic; this reads
     * as a head turning. */
    const int parallax = (frame.gaze_x * 3) / TILT_GAZE_RANGE_X;

    oled_display_clear();

    const int face_center_x = (EYE_LEFT_X + EYE_RIGHT_X) / 2;
    const float turn_sin = sinf(frame.rotation);
    const float turn_cos = cosf(frame.rotation);

    const int center_y = EYE_CENTER_Y + frame.group_dy + frame.gaze_y;
    for (int eye = 0; eye < 2; ++eye) {
        /* Rotate each eye about the middle of the face before shifting it. */
        const float offset_x =
            (float)(((eye == 0) ? EYE_LEFT_X : EYE_RIGHT_X) - face_center_x);
        const int center_x = face_center_x +
                             (int)(offset_x * turn_cos + 0.5f) +
                             frame.group_dx + frame.gaze_x;
        const int eye_center_y = center_y + (int)(offset_x * turn_sin + 0.5f);
        /* Near eye grows, far eye thins, by the same amount. */
        const int near = (eye == 1) ? parallax : -parallax;
        int eye_width = frame.eye_width + near;
        if (eye_width < 6) { eye_width = 6; }

        if (frame.arc_eyes) {
            draw_arc_eye(center_x, eye_center_y, eye_width,
                         frame.eye_height, 4);
            continue;
        }

        fill_round_rect(center_x, eye_center_y, eye_width, eye_height,
                        frame.radius, true);
        /* Skip the lids mid-blink: they would just eat the whole closed eye. */
        if (openness > 50) {
            if (frame.glare_percent > 0) {
                paint_glare(center_x, eye_center_y, eye_width, eye_height,
                            frame.glare_percent, eye == 1);
            }
            if (frame.lid_top_percent > 0 || frame.lid_bottom_percent < 100) {
                paint_lids(center_x, eye_center_y, eye_width, eye_height,
                           frame.lid_top_percent, frame.lid_bottom_percent);
            }
        }
    }

    draw_overlay(&frame, view, now_ms);
    return oled_display_present();
}
