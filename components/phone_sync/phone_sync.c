#include "phone_sync.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"

#define PHONE_SYNC_DEVICE_NAME "KeychainSync"
#define PHONE_SYNC_WRITE_MAX_LEN 24

static const char *TAG = "phone_sync";

/* ESP-IDF NimBLE examples provide this function from the store config module. */
void ble_store_config_init(void);

static uint8_t s_own_addr_type;
static bool s_host_synced;
static bool s_advertising_requested;
static bool s_has_pending_datetime;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_time_chr_handle;
static device_clock_datetime_t s_pending_datetime;

static const ble_uuid128_t s_phone_sync_service_uuid =
    BLE_UUID128_INIT(0x42, 0x13, 0x37, 0x10, 0x5b, 0x31, 0x49, 0x9a,
                     0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11);
static const ble_uuid128_t s_phone_sync_time_uuid =
    BLE_UUID128_INIT(0x43, 0x13, 0x37, 0x10, 0x5b, 0x31, 0x49, 0x9a,
                     0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11);

static int gap_event(struct ble_gap_event *event, void *arg);
static int time_access(uint16_t conn_handle,
                       uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt,
                       void *arg);

static const struct ble_gatt_chr_def s_time_characteristics[] = {
    {
        .uuid = &s_phone_sync_time_uuid.u,
        .access_cb = time_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
        .val_handle = &s_time_chr_handle,
    },
    {
        0,
    },
};

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_phone_sync_service_uuid.u,
        .characteristics = s_time_characteristics,
    },
    {
        0,
    },
};

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
    if (text == NULL || datetime == NULL) {
        return false;
    }

    /*
     * Test format for phone apps such as nRF Connect:
     * YYYY-MM-DD HH:MM:SS or YYYY-MM-DDTHH:MM:SS
     */
    if (!is_digit(text[0]) || !is_digit(text[1]) ||
        !is_digit(text[2]) || !is_digit(text[3]) ||
        text[4] != '-' ||
        !is_digit(text[5]) || !is_digit(text[6]) ||
        text[7] != '-' ||
        !is_digit(text[8]) || !is_digit(text[9]) ||
        (text[10] != ' ' && text[10] != 'T') ||
        !is_digit(text[11]) || !is_digit(text[12]) ||
        text[13] != ':' ||
        !is_digit(text[14]) || !is_digit(text[15]) ||
        text[16] != ':' ||
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
        parsed.hour > 23U || parsed.minute > 59U ||
        parsed.second > 59U) {
        return false;
    }

    *datetime = parsed;
    return true;
}

static int start_advertising_if_needed(void)
{
    if (!s_host_synced || !s_advertising_requested ||
        s_conn_handle != BLE_HS_CONN_HANDLE_NONE ||
        ble_gap_adv_active()) {
        return 0;
    }

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)PHONE_SYNC_DEVICE_NAME;
    fields.name_len = strlen(PHONE_SYNC_DEVICE_NAME);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE adv fields failed: rc=%d", rc);
        return rc;
    }

    struct ble_hs_adv_fields response_fields;
    memset(&response_fields, 0, sizeof(response_fields));
    response_fields.uuids128 = &s_phone_sync_service_uuid;
    response_fields.num_uuids128 = 1;
    response_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&response_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE scan response fields failed: rc=%d", rc);
        return rc;
    }

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_addr_type,
                           NULL,
                           BLE_HS_FOREVER,
                           &params,
                           gap_event,
                           NULL);
    if (rc == 0) {
        ESP_LOGI(TAG, "BLE advertising started as %s",
                 PHONE_SYNC_DEVICE_NAME);
    } else {
        ESP_LOGE(TAG, "BLE advertising failed: rc=%d", rc);
    }

    return rc;
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Phone connected");
        } else {
            ESP_LOGW(TAG, "Phone connection failed: status=%d",
                     event->connect.status);
            start_advertising_if_needed();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ESP_LOGI(TAG, "Phone disconnected: reason=%d",
                 event->disconnect.reason);
        start_advertising_if_needed();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising_if_needed();
        return 0;

    default:
        return 0;
    }
}

static int time_access(uint16_t conn_handle,
                       uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt,
                       void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const char help[] = "write YYYY-MM-DD HH:MM:SS";
        ESP_LOGI(TAG, "Phone read time characteristic");
        const int rc = os_mbuf_append(ctxt->om,
                                      help,
                                      sizeof(help) - 1U);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        char text[PHONE_SYNC_WRITE_MAX_LEN + 1U] = {0};
        uint16_t length = 0;
        int rc = ble_hs_mbuf_to_flat(ctxt->om,
                                     text,
                                     PHONE_SYNC_WRITE_MAX_LEN,
                                     &length);
        if (rc != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }

        text[length] = '\0';
        ESP_LOGI(TAG, "Phone write attempt: len=%u, text=%s",
                 (unsigned int)length,
                 text);

        device_clock_datetime_t datetime;
        if (!parse_datetime_text(text, &datetime)) {
            ESP_LOGW(TAG, "Invalid phone time: %s", text);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        s_pending_datetime = datetime;
        s_has_pending_datetime = true;
        ESP_LOGI(TAG, "Phone time received: %04u-%02u-%02u %02u:%02u:%02u",
                 (unsigned int)datetime.year,
                 (unsigned int)datetime.month,
                 (unsigned int)datetime.day,
                 (unsigned int)datetime.hour,
                 (unsigned int)datetime.minute,
                 (unsigned int)datetime.second);
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE host reset: reason=%d", reason);
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE address setup failed: rc=%d", rc);
        return;
    }

    s_host_synced = true;
    start_advertising_if_needed();
}

static void host_task(void *param)
{
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t phone_sync_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
        return err;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_services);
    if (rc != 0) {
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(s_services);
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE GATT service add failed: rc=%d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "BLE GATT time service registered");

    rc = ble_svc_gap_device_name_set(PHONE_SYNC_DEVICE_NAME);
    if (rc != 0) {
        return ESP_FAIL;
    }

    ble_store_config_init();
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

void phone_sync_request_advertising(void)
{
    s_advertising_requested = true;
    start_advertising_if_needed();
}

void phone_sync_stop_advertising(void)
{
    s_advertising_requested = false;

    if (ble_gap_adv_active()) {
        (void)ble_gap_adv_stop();
    }
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        (void)ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }

    ESP_LOGI(TAG, "BLE advertising stopped");
}

bool phone_sync_get_datetime_update(device_clock_datetime_t *datetime)
{
    if (datetime == NULL || !s_has_pending_datetime) {
        return false;
    }

    *datetime = s_pending_datetime;
    s_has_pending_datetime = false;
    return true;
}
