#ifndef HOST_ESP_LOG_STUB_H
#define HOST_ESP_LOG_STUB_H

/*
 * The portable components log through ESP-IDF's macros, and each platform
 * supplies its own shim - the nRF one forwards to Zephyr's printk, which is
 * not host code. Tests want the logic, not the logging, so here the macros
 * evaluate their arguments for warnings and produce nothing.
 */
#define ESP_LOG_SWALLOW(...) do { (void)sizeof((int[]){0, ##__VA_ARGS__}); } \
    while (0)

#define ESP_LOGE(tag, ...) ESP_LOG_SWALLOW()
#define ESP_LOGW(tag, ...) ESP_LOG_SWALLOW()
#define ESP_LOGI(tag, ...) ESP_LOG_SWALLOW()
#define ESP_LOGD(tag, ...) ESP_LOG_SWALLOW()
#define ESP_LOGV(tag, ...) ESP_LOG_SWALLOW()

#endif /* HOST_ESP_LOG_STUB_H */
