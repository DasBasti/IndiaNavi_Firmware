// Logging functions


#ifdef TESTING
#include <stdio.h>
#define ESP_LOGI(tag, format_str, ...)        printf("I[%s] " format_str "\n", tag, ##__VA_ARGS__);
#define ESP_LOGE(tag, format_str, ...)        printf("E[%s] " format_str "\n", tag, ##__VA_ARGS__);
#else
#include "esp_log.h"
#define LOGI(tag, format_str, ...)        ESP_LOGI(tag, format_str, ##__VA_ARGS__);
#define LOGE(tag, format_str, ...)        ESP_LOGE(tag, format_str, ##__VA_ARGS__);

#endif