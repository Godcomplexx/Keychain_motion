#include <stdbool.h>
#include <stdio.h>

#include "pet_behavior.h"

/*
 * These tests use explicit timestamps, so no real clock or RTOS is needed.
 * Returning false keeps failures readable even when assertions are disabled.
 */
#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("FAIL line %d: %s\n", __LINE__, #condition);              \
            return false;                                                      \
        }                                                                      \
    } while (0)

static pet_context_t normal_context(void)
{
    return (pet_context_t) {
        .sensor_present = true,
        .charging = false,
        .battery_percent = PET_BATTERY_UNKNOWN,
        .battery_millivolts = 0,
    };
}

static bool test_boot_reaction_expires(void)
{
    pet_behavior_t pet;
    pet_context_t context = normal_context();

    pet_behavior_init(&pet, 1000U, 1U);
    pet_behavior_post(&pet, PET_EVENT_BOOT_COMPLETED, 1000U);
    pet_behavior_update(&pet, &context, 1000U);
    CHECK(pet_behavior_view(&pet)->id == PET_WAKE_UP);

    /* WAKE_UP lasts 1400 ms, then the neutral base state becomes visible. */
    pet_behavior_update(&pet, &context, 2400U);
    CHECK(pet_behavior_view(&pet)->id == PET_NEUTRAL);
    return true;
}

static bool test_jolt_and_shake_are_different_feelings(void)
{
    pet_behavior_t pet;
    pet_context_t context = normal_context();

    /*
     * One knock is a fright; sustained shaking is provocation. They used to be
     * the same event, with the second one escalating - which meant a single
     * knock could never look angry and a deliberate shake had to be repeated
     * before it did. The adapter now tells them apart before they get here.
     */
    pet_behavior_init(&pet, 1000U, 2U);
    pet_behavior_post(&pet, PET_EVENT_JOLT, 1000U);
    pet_behavior_update(&pet, &context, 1000U);
    CHECK(pet_behavior_view(&pet)->id == PET_SURPRISED);

    pet_behavior_post(&pet, PET_EVENT_SHAKEN, 2000U);
    pet_behavior_update(&pet, &context, 2000U);
    CHECK(pet_behavior_view(&pet)->id == PET_ANGRY);
    return true;
}

static bool test_sleep_requires_a_wake_sensor(void)
{
    pet_behavior_t pet;
    pet_context_t context = normal_context();

    pet_behavior_init(&pet, 0U, 3U);
    pet_behavior_update(&pet, &context, 300000U);
    CHECK(pet_behavior_view(&pet)->id == PET_ASLEEP);

    pet_behavior_init(&pet, 0U, 3U);
    context.sensor_present = false;
    pet_behavior_update(&pet, &context, 300000U);
    CHECK(pet_behavior_view(&pet)->id != PET_ASLEEP);
    return true;
}

static bool test_low_battery_is_sleepy(void)
{
    pet_behavior_t pet;
    pet_context_t context = normal_context();

    pet_behavior_init(&pet, 0U, 4U);
    context.battery_percent = 15U;
    context.battery_millivolts = 3500;
    pet_behavior_update(&pet, &context, 1000U);
    CHECK(pet_behavior_view(&pet)->id == PET_SLEEPY);
    CHECK(pet_behavior_view(&pet)->battery_millivolts == 3500);
    return true;
}

/*
 * Pressing the button in the app is what causes the phone to connect, and
 * connecting raises a greeting. The greeting is P1 and an activity is P3, so
 * on priority alone the request that caused the connection was refused - the
 * press did nothing at all, which reads as a broken button.
 */
static bool test_a_request_outranks_the_greeting_it_caused(void)
{
    pet_behavior_t pet;
    pet_context_t context = normal_context();

    pet_behavior_init(&pet, 1000U, 3U);
    pet_behavior_post(&pet, PET_EVENT_PHONE_CONNECTED, 1000U);
    pet_behavior_update(&pet, &context, 1000U);
    CHECK(pet_behavior_view(&pet)->id == PET_GREETING);

    /* The command lands inside the greeting, which is where it always lands. */
    pet_behavior_post(&pet, PET_EVENT_PLAY_REQUESTED, 1100U);
    pet_behavior_update(&pet, &context, 1100U);
    CHECK(pet_behavior_view(&pet)->id == PET_ACT_FLUID);
    return true;
}

/* Being dropped is the one thing a button press does not interrupt. */
static bool test_a_request_waits_for_a_fall_to_end(void)
{
    pet_behavior_t pet;
    pet_context_t context = normal_context();

    pet_behavior_init(&pet, 1000U, 4U);
    pet_behavior_post(&pet, PET_EVENT_FREE_FALL, 1000U);
    pet_behavior_update(&pet, &context, 1000U);
    CHECK(pet_behavior_view(&pet)->id == PET_STARTLED);

    pet_behavior_post(&pet, PET_EVENT_PLAY_REQUESTED, 1100U);
    pet_behavior_update(&pet, &context, 1100U);
    CHECK(pet_behavior_view(&pet)->id == PET_STARTLED);
    return true;
}

/*
 * The other direction of the same mistake. The phone reconnects by itself
 * every so often, and greeting it over a requested activity cut the particles
 * from thirty seconds to eleven - measured on hardware, not imagined.
 */
static bool test_a_greeting_does_not_cut_short_a_request(void)
{
    pet_behavior_t pet;
    pet_context_t context = normal_context();

    pet_behavior_init(&pet, 1000U, 5U);
    pet_behavior_post(&pet, PET_EVENT_PLAY_REQUESTED, 1000U);
    pet_behavior_update(&pet, &context, 1000U);
    CHECK(pet_behavior_view(&pet)->id == PET_ACT_FLUID);

    pet_behavior_post(&pet, PET_EVENT_PHONE_CONNECTED, 2000U);
    pet_behavior_update(&pet, &context, 2000U);
    CHECK(pet_behavior_view(&pet)->id == PET_ACT_FLUID);

    /* A clock correction arrives on the phone's schedule too. */
    pet_behavior_post(&pet, PET_EVENT_PHONE_SYNCED, 3000U);
    pet_behavior_update(&pet, &context, 3000U);
    CHECK(pet_behavior_view(&pet)->id == PET_ACT_FLUID);
    return true;
}

int main(void)
{
    CHECK(test_boot_reaction_expires());
    CHECK(test_jolt_and_shake_are_different_feelings());
    CHECK(test_sleep_requires_a_wake_sensor());
    CHECK(test_low_battery_is_sleepy());
    CHECK(test_a_request_outranks_the_greeting_it_caused());
    CHECK(test_a_request_waits_for_a_fall_to_end());
    CHECK(test_a_greeting_does_not_cut_short_a_request());

    puts("PASS: pet_behavior host tests");
    return 0;
}
