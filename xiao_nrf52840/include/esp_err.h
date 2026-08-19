#ifndef XIAO_ESP_ERR_COMPAT_H
#define XIAO_ESP_ERR_COMPAT_H

/*
 * Portable application components use ESP-IDF-style result names. Keeping this
 * tiny compatibility type avoids copying otherwise platform-neutral logic.
 */
typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_NO_MEM (-2)
#define ESP_ERR_INVALID_ARG (-3)
#define ESP_ERR_INVALID_STATE (-4)
#define ESP_ERR_INVALID_SIZE (-5)
#define ESP_ERR_NOT_FOUND (-6)
#define ESP_ERR_INVALID_RESPONSE (-7)
#define ESP_ERR_TIMEOUT (-8)
#define ESP_ERR_NOT_SUPPORTED (-9)

static inline const char *esp_err_to_name(esp_err_t error)
{
    switch (error) {
    case ESP_OK: return "ESP_OK";
    case ESP_FAIL: return "ESP_FAIL";
    case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_INVALID_SIZE: return "ESP_ERR_INVALID_SIZE";
    case ESP_ERR_NOT_FOUND: return "ESP_ERR_NOT_FOUND";
    case ESP_ERR_INVALID_RESPONSE: return "ESP_ERR_INVALID_RESPONSE";
    case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
    case ESP_ERR_NOT_SUPPORTED: return "ESP_ERR_NOT_SUPPORTED";
    default: return "ESP_ERR_UNKNOWN";
    }
}

#endif /* XIAO_ESP_ERR_COMPAT_H */
