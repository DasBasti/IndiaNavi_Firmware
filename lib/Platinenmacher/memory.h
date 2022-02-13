/*
 * Interface to malloc and other RTOS related functions
 *
 *  Created on: Jan 2, 2021
 *      Author: bastian
 */

#ifndef PLATINENMACHER_MEMORY_H_
#define PLATINENMACHER_MEMORY_H_

#include "rtos.h"
#include <freertos/FreeRTOS.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

#include <string.h>

inline static void *RTOS_Malloc(size_t size)
{
#ifdef PM_MEMORY_DEBUG
    ESP_LOGI("MALLOC", "allocate: %d from %d", size, heap_caps_get_free_size(MALLOC_CAP_8BIT));
#endif
    void *mem = malloc(size);
    if (mem)
        memset(mem, 0, size);
#ifdef PM_MEMORY_DEBUG
    else
        ESP_LOGI("MALLOC", "failed to allocate: %d of %d", size, heap_caps_get_free_size(MALLOC_CAP_8BIT));
#endif
    return mem;
}
inline static void RTOS_Free(void *pointer)
{
    if (pointer)
    {
#ifdef PM_MEMORY_DEBUG
        ESP_LOGI("MALLOC", "free: 0x%x %d free", (uint32_t)pointer, heap_caps_get_free_size(MALLOC_CAP_8BIT));
#endif
        free(pointer);
        pointer = NULL;
    }
#ifdef PM_MEMORY_DEBUG
    else
        ESP_LOGI("MALLOC", "free called with zero pointer!");
#endif
}

#define bit_set(data, pos) (data |= (1U << pos))
#define bit_clear(data, pos) (data &= (~(1U << pos)))
#define bit_toggle(data, pos) (data ^= (1U << pos))
#endif /* PLATINENMACHER_MEMORY_H_ */
