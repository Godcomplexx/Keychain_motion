#ifndef PHONE_SYNC_H
#define PHONE_SYNC_H

#include <stdbool.h>
#include <stdint.h>

#include "device_clock.h"
#include "esp_err.h"

typedef enum {
    PHONE_SYNC_COMMAND_NONE = 0,
    PHONE_SYNC_COMMAND_START_GAME,
    /*
     * Setting the clock and putting the clock on screen are separate things.
     * A background sync should correct the time without taking the display
     * away from whatever the companion was doing; showing it is a request the
     * person makes, so it has a command of its own.
     */
    PHONE_SYNC_COMMAND_SHOW_TIME,
    /* The phone opened its drawing pad, and later closed it. */
    PHONE_SYNC_COMMAND_START_DRAW,
    PHONE_SYNC_COMMAND_STOP_DRAW,
} phone_sync_command_t;

/*
 * Drawing has a characteristic of its own, written without a response. The
 * command characteristic is text and answers every write, which is the right
 * shape for "show the clock" and the wrong one for a finger moving: an
 * acknowledged write per packet halves the rate and the stroke visibly lags
 * the finger.
 */
#define PHONE_SYNC_DRAW_PACKET_MAX 244
#define PHONE_SYNC_DRAW_QUEUE_LENGTH 8

typedef struct {
    uint8_t length;
    uint8_t data[PHONE_SYNC_DRAW_PACKET_MAX];
} phone_sync_draw_packet_t;

esp_err_t phone_sync_init(void);

esp_err_t phone_sync_request_advertising(void);

void phone_sync_stop_advertising(void);

/* Stop and deinitialize NimBLE before entering light sleep. */
esp_err_t phone_sync_shutdown(void);

bool phone_sync_get_datetime_update(device_clock_datetime_t *datetime);

bool phone_sync_get_command(phone_sync_command_t *command);

/*
 * True once per phone connection, then false again. Distinct from a time sync:
 * the phone is simply here, which is worth greeting before it says anything.
 */
bool phone_sync_take_connected_event(void);

/*
 * True while a phone is connected. Drawing needs this: if the phone walks away
 * mid-stroke, nothing else would ever take the screen back.
 */
bool phone_sync_is_connected(void);

/* Takes the oldest queued drawing packet. False when the queue is empty. */
bool phone_sync_get_draw_packet(phone_sync_draw_packet_t *packet);

#endif /* PHONE_SYNC_H */
