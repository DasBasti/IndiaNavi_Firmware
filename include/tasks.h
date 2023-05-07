/*
 * System Tasks
 *
 *  Created on: Jan 7, 2021
 *      Author: bastian
 */

#ifndef INC_TASKS_H_
#define INC_TASKS_H_

#include <stdbool.h>

#include "gui.h"
#ifdef LINUX
typedef struct
{
    char* filename;
    char* dest;
    uint8_t loaded;
    void* file;
} async_file_t;

#    define save_sprintf(dest, size, format, ...) sprintf(dest, size, format, ##__VA_ARGS__)
#    define save_snprintf(dest, size, format, ...) snprintf(dest, size, format, ##__VA_ARGS__)
#else
#    include <freertos/semphr.h>

#    include <ff.h>

//Create semphore
extern SemaphoreHandle_t print_semaphore;
extern SemaphoreHandle_t gui_semaphore;
extern SemaphoreHandle_t sd_semaphore;

extern QueueHandle_t eventQueueHandle;

extern TaskHandle_t housekeepingTask_h;
extern TaskHandle_t gpsTask_h;
extern TaskHandle_t guiTask_h;
extern TaskHandle_t powerTask_h;
extern TaskHandle_t sdTask_h;
extern TaskHandle_t wifiTask_h;
extern TaskHandle_t mapLoaderTask_h;

typedef enum {
    TASK_EVENT_ENTER_LOW_POWER = 50,
    TASK_EVENT_ENABLE_GPS,
    TASK_EVENT_DISABLE_GPS,
    TASK_EVENT_ENABLE_DISPLAY,
    TASK_EVENT_DISABLE_DISPLAY,
    TASK_EVENT_ENABLE_WIFI,
    TASK_EVENT_DISABLE_WIFI,
    TASK_EVENT_BUTTON_DOWN,
    TASK_EVENT_BUTTON_UP,
} task_events_e;

typedef struct
{
    char* filename;
    char* dest;
    uint8_t loaded;
    FIL* file;
} async_file_t;

#define save_sprintf(dest, format, ...)                 \
    do {                                                \
        xSemaphoreTake(print_semaphore, portMAX_DELAY); \
        sprintf(dest, format, ##__VA_ARGS__);           \
        xSemaphoreGive(print_semaphore);                \
    } while (0);

#define save_snprintf(dest, num, format, ...)                 \
    do {                                                \
        xSemaphoreTake(print_semaphore, portMAX_DELAY); \
        snprintf(dest, num, format, ##__VA_ARGS__);           \
        xSemaphoreGive(print_semaphore);                \
    } while (0);

#endif

// From sd.c
error_code_t loadTile(map_tile_t* tile);
error_code_t loadFile(async_file_t* file);
error_code_t fileExists(async_file_t* file);
error_code_t createFileBuffer(async_file_t* file);
error_code_t openFileForWriting(async_file_t* file);
async_file_t* createPhysicalFile();
error_code_t writeToFile(async_file_t* file, void* in_data, uint32_t count, uint32_t* written);
error_code_t closeFile(async_file_t* file);
char* readline(char* c, char* d);
void closePhysicalFile(async_file_t* file);

// From gps.c
void gps_screen_element(const display_t* dsp);
bool gps_is_position_known();
void gps_stop_parser();

// From main.c
void toggleZoom();
void enter_deep_sleep();

// From gui.c
void trigger_rendering();

// From wifi.c
bool isConnected();

// From map_loader.c
void maploader_screen_element(const display_t* dsp);


#endif /* INC_TASKS_H_ */
