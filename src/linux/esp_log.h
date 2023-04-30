// ESP Logging functions for use in linux build 

#ifdef LINUX
#include <stdio.h>
#define ESP_LOGI(tag, format_str, ...)        printf("I[%s] " format_str "\n", tag, ##__VA_ARGS__);
#define ESP_LOGE(tag, format_str, ...)        printf("E[%s] " format_str "\n", tag, ##__VA_ARGS__);
#endif