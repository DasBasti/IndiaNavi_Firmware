/*
 * Interface to malloc and other RTOS related functions
 *
 *  Created on: Jan 2, 2021
 *      Author: bastian
 */

#ifndef PLATINENMACHER_MEMORY_H_
#define PLATINENMACHER_MEMORY_H_

#if defined(TESTING) || defined(LINUX)
#include <stdlib.h>
#else
#include "rtos.h"
#include <freertos/FreeRTOS.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#endif /* TESTING */

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
/*
 * Allocate zeroed memory for data that is never used for DMA, like track
 * points or file contents. PSRAM is preferred so the small internal RAM stays
 * free for WiFi, SD card and display. Falls back to internal RAM on boards
 * without PSRAM. Free with RTOS_Free().
 */
inline static void *RTOS_Malloc_Large(size_t size)
{
#if defined(TESTING) || defined(LINUX)
    return RTOS_Malloc(size);
#else
    void *mem = heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    if (mem)
        memset(mem, 0, size);
    return mem;
#endif
}

inline static void RTOS_Free(void *pointer)
{
    if (pointer)
    {
#ifdef PM_MEMORY_DEBUG
        ESP_LOGI("MALLOC", "free: 0x%p %d free", pointer, heap_caps_get_free_size(MALLOC_CAP_8BIT));
#endif
        free(pointer);
        //pointer = NULL;
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
