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

inline void *RTOS_Malloc(size_t size)
{
    void *mem = malloc(size);
    memset(mem, 0, size);
    return mem;
}
#define RTOS_Free(size) free(size)

#define bit_set(data, pos) (data |= (1U << pos))
#define bit_clear(data, pos) (data &= (~(1U << pos)))
#define bit_toggle(data, pos) (data ^= (1U << pos))

#endif /* PLATINENMACHER_MEMORY_H_ */
