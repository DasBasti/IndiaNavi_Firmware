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
    size_t dest_size; /// size of dest buffer if provided by caller
    uint8_t loaded;
    void* file;
} async_file_t;

#    define save_sprintf(dest, size, format, ...) sprintf(dest, size, format, ##__VA_ARGS__)
#    define save_snprintf(dest, size, format, ...) snprintf(dest, size, format, ##__VA_ARGS__)
#else
#    include <freertos/semphr.h>

#    include <ff.h>

// Create semphore
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
    TASK_EVENT_NO_EVENT = 0,
    TASK_EVENT_ENTER_LOW_POWER = 50,
    TASK_EVENT_ENABLE_GPS,
    TASK_EVENT_DISABLE_GPS,
    TASK_EVENT_ENABLE_DISPLAY,
    TASK_EVENT_DISABLE_DISPLAY,
    TASK_EVENT_ENABLE_WIFI,
    TASK_EVENT_DISABLE_WIFI,
    TASK_EVENT_BUTTON_DOWN,
    TASK_EVENT_BUTTON_UP,
    TASK_EVENT_START_CHARGING,
    TASK_EVENT_STOP_CHARGING,
} task_events_e;

typedef struct
{
    char* filename;
    char* dest;       /// buffer for loadFile. Allocated by loadFile if NULL
    size_t dest_size; /// size of dest buffer if provided by caller
    uint8_t loaded;
    FIL* file;
} async_file_t;

/* timeout for waiting on the SD card mutex */
#    define SD_MUTEX_TIMEOUT pdMS_TO_TICKS(1000)

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

// From sd.c
error_code_t waitForSDInit();
error_code_t loadTile(map_tile_t* tile);
error_code_t loadFile(async_file_t* file);
error_code_t fileExists(async_file_t* file);
error_code_t createFileBuffer(async_file_t* file);
error_code_t openFileForWriting(async_file_t* file);
error_code_t openFileForUpdate(async_file_t* file);
error_code_t seekFile(async_file_t* file, uint32_t offset);
error_code_t readFromFile(async_file_t* file, void* out_data, uint32_t count, uint32_t* read);
async_file_t* createPhysicalFile();
error_code_t writeToFile(async_file_t* file, void* in_data, uint32_t count, uint32_t* written);
error_code_t closeFile(async_file_t* file);
error_code_t deleteFile(async_file_t* file);
char* readline(char* c, char* d);
void closePhysicalFile(async_file_t* file);
#ifndef LINUX
bool sd_lock(void);
void sd_unlock(void);
error_code_t sd_get_info(uint64_t* total, uint64_t* free);
#endif

// From upload_server.c
typedef enum {
    UPLOAD_IDLE,    /// no transfer announced
    UPLOAD_ACTIVE,  /// app announced a transfer, files are coming in
    UPLOAD_DONE,    /// all announced files are stored
    UPLOAD_ABORTED, /// no upload for too long
} upload_state_t;

typedef struct {
    upload_state_t state;
    uint32_t files_total;
    uint32_t files_done;
    uint64_t bytes_total;
    uint64_t bytes_done;
    bool result_shown; /// "done"/"aborted" was on the display for one refresh
} upload_progress_t;

void upload_server_start(void);
void upload_server_stop(void);
upload_progress_t upload_get_progress(void);
void upload_progress_result_shown(upload_state_t shown);

// From gps.c
void gps_screen_element(const display_t* dsp);
bool gps_is_position_known();
void gps_stop_parser();
void gps_enter_standby();

// From main.c
error_code_t enter_deep_sleep_if_not_charging();
void set_short_press_event(void (*event)(void));
void set_long_press_event(void (*event)(void));

// From gui.c
void trigger_rendering();
void gui_reload_track(void);

// From wifi.c
bool isConnected();
void wifi_ap_credentials_init(void);
const char* wifi_ap_ssid(void);
const char* wifi_ap_password(void);
bool wifi_ap_running(void);
uint8_t wifi_ap_station_count(void);
const uint8_t* wifi_ap_qrcode(void);
void wifi_notify_activity(void);
void wifi_start_task(void);
void wifi_request_stop(void);

// From map_loader.c
void maploader_screen_element(const display_t* dsp);

error_code_t do_background_ota(void* pvParameter);

#endif /* INC_TASKS_H_ */
