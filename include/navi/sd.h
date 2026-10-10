/*
 * SD card
 *
 * The SD task mounts the card and owns its mutex. FatFs is used directly
 * only between sd_lock() and sd_unlock().
 *
 * Copyright (c) 2021, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NAVI_SD_H_
#define NAVI_SD_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "Platinenmacher.h"

#ifdef LINUX
typedef struct
{
    char* filename;
    char* dest;
    size_t dest_size; /// size of dest buffer if provided by caller
    uint8_t loaded;
    void* file;
} async_file_t;
#else
#    include <freertos/FreeRTOS.h>

#    include <ff.h>

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

/* starts the SD task and returns when its mutex exists */
void sd_start_task(void);

/**
 * Lock the SD card for direct FatFs access from other modules.
 *
 * returns false if no card is mounted or the card is busy
 */
bool sd_lock(void);
void sd_unlock(void);
/* true if a card is mounted and nobody uses it, does not wait */
bool sd_is_free(void);
error_code_t sd_get_info(uint64_t* total, uint64_t* free);
#endif

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
error_code_t renameFile(const char* from, const char* to);
char* readline(char* c, char* d);
void closePhysicalFile(async_file_t* file);

#endif /* NAVI_SD_H_ */
