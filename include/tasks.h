/*
 * System Tasks
 *
 *  Created on: Jan 7, 2021
 *      Author: bastian
 */

#ifndef INC_TASKS_H_
#define INC_TASKS_H_

#include <freertos/semphr.h>
#include "gui.h"
//Create semphore
extern SemaphoreHandle_t print_semaphore;
extern SemaphoreHandle_t gui_semaphore;
extern SemaphoreHandle_t sd_semaphore;

extern QueueHandle_t mapLoadQueueHandle;
extern QueueHandle_t fileLoadQueueHandle;

typedef struct
{
    char *filename;
    char *dest;
    uint8_t loaded;
} async_file_t;

#define save_sprintf(dest, format, ...)                 \
    do                                                  \
    {                                                   \
        xSemaphoreTake(print_semaphore, portMAX_DELAY); \
        sprintf(dest, format, ##__VA_ARGS__);           \
        xSemaphoreGive(print_semaphore);                \
    } while (0);

error_code_t loadTile(map_tile_t *tile);
error_code_t loadFile(async_file_t *file);

#endif /* INC_TASKS_H_ */
