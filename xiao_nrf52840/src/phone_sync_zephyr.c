#include "phone_sync.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>

#include "esp_log.h"

#define PHONE_SYNC_DEVICE_NAME "KeychainSync"
#define PHONE_SYNC_WRITE_MAX_LEN 31

static const char *TAG = "phone_sync";
static bool s_initialized;
static bool s_advertising;
static bool s_has_pending_datetime;
static phone_sync_command_t s_pending_command;
static device_clock_datetime_t s_pending_datetime;
static struct bt_conn *s_connection;
/* Set in BLE callback context, consumed by the application loop. */
static volatile bool s_connected_event;
K_MUTEX_DEFINE(s_state_lock);

static struct bt_uuid_128 s_service_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x11223344, 0x5566, 0x7788, 0x9a49,
                       0x315b10371342));
static struct bt_uuid_128 s_characteristic_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x11223344, 0x5566, 0x7788, 0x9a49,
                       0x315b10371343));

static bool is_digit(char value)
{
    return value >= '0' && value <= '9';
}

static uint8_t parse_two_digits(const char *text)
{
    return (uint8_t)((text[0] - '0') * 10 + (text[1] - '0'));
}

static uint16_t parse_four_digits(const char *text)
{
    return (uint16_t)((text[0] - '0') * 1000 +
                      (text[1] - '0') * 100 +
                      (text[2] - '0') * 10 +
                      (text[3] - '0'));
}

static bool parse_datetime_text(const char *text,
                                device_clock_datetime_t *datetime)
{
    if (text == NULL || datetime == NULL || strlen(text) != 19U) {
        return false;
    }

    if (!is_digit(text[0]) || !is_digit(text[1]) ||
        !is_digit(text[2]) || !is_digit(text[3]) || text[4] != '-' ||
        !is_digit(text[5]) || !is_digit(text[6]) || text[7] != '-' ||
        !is_digit(text[8]) || !is_digit(text[9]) ||
        (text[10] != ' ' && text[10] != 'T') ||
        !is_digit(text[11]) || !is_digit(text[12]) || text[13] != ':' ||
        !is_digit(text[14]) || !is_digit(text[15]) || text[16] != ':' ||
        !is_digit(text[17]) || !is_digit(text[18])) {
        return false;
    }

    const device_clock_datetime_t parsed = {
        .year = parse_four_digits(&text[0]),
        .month = parse_two_digits(&text[5]),
        .day = parse_two_digits(&text[8]),
        .hour = parse_two_digits(&text[11]),
        .minute = parse_two_digits(&text[14]),
        .second = parse_two_digits(&text[17]),
    };

    if (parsed.year < 2024U || parsed.year > 2099U ||
        parsed.month < 1U || parsed.month > 12U ||
        parsed.day < 1U || parsed.day > 31U ||
        parsed.hour > 23U || parsed.minute > 59U || parsed.second > 59U) {
        return false;
    }

    *datetime = parsed;
    return true;
}

static ssize_t read_characteristic(struct bt_conn *conn,
                                   const struct bt_gatt_attr *attr,
                                   void *buffer,
                                   uint16_t length,
                                   uint16_t offset)
{
    static const char help[] = "time or GAME:START";
    return bt_gatt_attr_read(conn, attr, buffer, length, offset,
                             help, sizeof(help) - 1U);
}

static ssize_t write_characteristic(struct bt_conn *conn,
                                    const struct bt_gatt_attr *attr,
                                    const void *buffer,
                                    uint16_t length,
                                    uint16_t offset,
                                    uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(flags);

    if (offset != 0U || length > PHONE_SYNC_WRITE_MAX_LEN) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    char text[PHONE_SYNC_WRITE_MAX_LEN + 1U];
    memcpy(text, buffer, length);
    text[length] = '\0';

    if (strcmp(text, "GAME:START") == 0) {
        k_mutex_lock(&s_state_lock, K_FOREVER);
        s_pending_command = PHONE_SYNC_COMMAND_START_GAME;
        k_mutex_unlock(&s_state_lock);
        ESP_LOGW(TAG, "Breakout start command received");
        return length;
    }

    device_clock_datetime_t datetime;
    if (!parse_datetime_text(text, &datetime)) {
        ESP_LOGW(TAG, "Invalid phone payload: %s", text);
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }

    k_mutex_lock(&s_state_lock, K_FOREVER);
    s_pending_datetime = datetime;
    s_has_pending_datetime = true;
    k_mutex_unlock(&s_state_lock);
    ESP_LOGW(TAG, "Phone time received: %s", text);
    return length;
}

BT_GATT_SERVICE_DEFINE(
    keychain_service,
    BT_GATT_PRIMARY_SERVICE(&s_service_uuid.uuid),
    BT_GATT_CHARACTERISTIC(&s_characteristic_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           read_characteristic,
                           write_characteristic,
                           NULL));

static void connected(struct bt_conn *connection, uint8_t error)
{
    if (error != 0U) {
        ESP_LOGW(TAG, "Phone connection failed: %u", error);
        return;
    }

    k_mutex_lock(&s_state_lock, K_FOREVER);
    s_advertising = false;
    if (s_connection != NULL) {
        bt_conn_unref(s_connection);
    }
    s_connection = bt_conn_ref(connection);
    s_connected_event = true;
    k_mutex_unlock(&s_state_lock);
    ESP_LOGI(TAG, "Phone connected");
}

static void disconnected(struct bt_conn *connection, uint8_t reason)
{
    k_mutex_lock(&s_state_lock, K_FOREVER);
    if (s_connection == connection) {
        bt_conn_unref(s_connection);
        s_connection = NULL;
    }
    k_mutex_unlock(&s_state_lock);
    ESP_LOGI(TAG, "Phone disconnected: reason=%u", reason);
}

BT_CONN_CB_DEFINE(connection_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

esp_err_t phone_sync_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    const int result = bt_enable(NULL);
    if (result != 0) {
        ESP_LOGE(TAG, "Bluetooth initialization failed: %d", result);
        return ESP_FAIL;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Bluetooth GATT service ready; radio idle");
    return ESP_OK;
}

esp_err_t phone_sync_request_advertising(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    k_mutex_lock(&s_state_lock, K_FOREVER);
    const bool already_active = s_advertising || s_connection != NULL;
    k_mutex_unlock(&s_state_lock);
    if (already_active) {
        return ESP_OK;
    }

    const struct bt_data advertising_data[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
        BT_DATA(BT_DATA_NAME_COMPLETE, PHONE_SYNC_DEVICE_NAME,
                sizeof(PHONE_SYNC_DEVICE_NAME) - 1U),
    };
    const struct bt_data scan_response[] = {
        BT_DATA_BYTES(BT_DATA_UUID128_ALL,
                      BT_UUID_128_ENCODE(0x11223344, 0x5566, 0x7788, 0x9a49,
                                         0x315b10371342)),
    };

    const int result = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
                                       advertising_data,
                                       ARRAY_SIZE(advertising_data),
                                       scan_response,
                                       ARRAY_SIZE(scan_response));
    if (result != 0) {
        ESP_LOGE(TAG, "Advertising failed: %d", result);
        return ESP_FAIL;
    }

    k_mutex_lock(&s_state_lock, K_FOREVER);
    s_advertising = true;
    k_mutex_unlock(&s_state_lock);
    ESP_LOGW(TAG, "Advertising as %s", PHONE_SYNC_DEVICE_NAME);
    return ESP_OK;
}

void phone_sync_stop_advertising(void)
{
    if (!s_initialized) {
        return;
    }

    k_mutex_lock(&s_state_lock, K_FOREVER);
    const bool was_advertising = s_advertising;
    s_advertising = false;
    k_mutex_unlock(&s_state_lock);
    if (was_advertising) {
        (void)bt_le_adv_stop();
    }
}

esp_err_t phone_sync_shutdown(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    phone_sync_stop_advertising();

    k_mutex_lock(&s_state_lock, K_FOREVER);
    struct bt_conn *connection = s_connection == NULL ? NULL :
                                 bt_conn_ref(s_connection);
    k_mutex_unlock(&s_state_lock);
    if (connection != NULL) {
        (void)bt_conn_disconnect(connection,
                                 BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        bt_conn_unref(connection);
    }

    /* Zephyr keeps the host initialized; with no advertising/connection the
     * radio is idle and a later synchronization window starts immediately. */
    return ESP_OK;
}

bool phone_sync_get_datetime_update(device_clock_datetime_t *datetime)
{
    if (datetime == NULL) {
        return false;
    }

    k_mutex_lock(&s_state_lock, K_FOREVER);
    const bool has_update = s_has_pending_datetime;
    if (has_update) {
        *datetime = s_pending_datetime;
        s_has_pending_datetime = false;
    }
    k_mutex_unlock(&s_state_lock);
    return has_update;
}

bool phone_sync_take_connected_event(void)
{
    if (!s_connected_event) {
        return false;
    }
    s_connected_event = false;
    return true;
}

bool phone_sync_get_command(phone_sync_command_t *command)
{
    if (command == NULL) {
        return false;
    }

    k_mutex_lock(&s_state_lock, K_FOREVER);
    const bool has_command = s_pending_command != PHONE_SYNC_COMMAND_NONE;
    if (has_command) {
        *command = s_pending_command;
        s_pending_command = PHONE_SYNC_COMMAND_NONE;
    }
    k_mutex_unlock(&s_state_lock);
    return has_command;
}
