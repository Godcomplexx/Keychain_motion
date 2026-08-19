#ifndef XIAO_ESP_LOG_COMPAT_H
#define XIAO_ESP_LOG_COMPAT_H

#include <zephyr/sys/printk.h>

/* Keep existing log call sites readable while Zephyr owns the console. */
#define ESP_LOGI(tag, format, ...) \
    printk("I (%s): " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) \
    printk("W (%s): " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, format, ...) \
    printk("E (%s): " format "\n", tag, ##__VA_ARGS__)

#endif /* XIAO_ESP_LOG_COMPAT_H */
