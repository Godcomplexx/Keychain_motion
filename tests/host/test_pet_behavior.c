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

int main(void)
{
    CHECK(test_boot_reaction_expires());
    CHECK(test_jolt_and_shake_are_different_feelings());
    CHECK(test_sleep_requires_a_wake_sensor());
    CHECK(test_low_battery_is_sleepy());

    puts("PASS: pet_behavior host tests");
    return 0;
}
