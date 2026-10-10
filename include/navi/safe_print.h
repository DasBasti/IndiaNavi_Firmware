/*
 * printf into a buffer from several tasks
 *
 * Copyright (c) 2021, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NAVI_SAFE_PRINT_H_
#define NAVI_SAFE_PRINT_H_

#include <stdio.h>

#ifdef LINUX
#    define save_sprintf(dest, size, format, ...) sprintf(dest, size, format, ##__VA_ARGS__)
#    define save_snprintf(dest, size, format, ...) snprintf(dest, size, format, ##__VA_ARGS__)
#else
#    include <freertos/FreeRTOS.h>
#    include <freertos/semphr.h>

/* serializes the formatting, created by app_main before any task starts */
extern SemaphoreHandle_t print_semaphore;

#    define save_sprintf(dest, format, ...)                 \
        do {                                                \
            xSemaphoreTake(print_semaphore, portMAX_DELAY); \
            sprintf(dest, format, ##__VA_ARGS__);           \
            xSemaphoreGive(print_semaphore);                \
        } while (0);
#    define save_snprintf(dest, size, format, ...)          \
        do {                                                \
            xSemaphoreTake(print_semaphore, portMAX_DELAY); \
            snprintf(dest, size, format, ##__VA_ARGS__);    \
            xSemaphoreGive(print_semaphore);                \
        } while (0);
#    define save_vsnprintf(dest, size, format, args)        \
        do {                                                \
            xSemaphoreTake(print_semaphore, portMAX_DELAY); \
            vsnprintf(dest, size, format, args);            \
            xSemaphoreGive(print_semaphore);                \
        } while (0);
#endif

#endif /* NAVI_SAFE_PRINT_H_ */
