#ifndef FATFS_LOCK_H
#define FATFS_LOCK_H

#include "FreeRTOS.h"
#include "semphr.h"

/**
 * Helper macro to execute a FatFs call while holding the fatfs_lock.
 * Usage:
 *   LOCKED_FCALL(f_open(&file, "path", FA_READ));
 *   LOCKED_FCALL(f_read(&file, buffer, len, &bytesRead));
 */
#define LOCKED_FCALL(call) \
    do { \
        xSemaphoreTake(fatfs_lock, portMAX_DELAY); \
        call; \
        xSemaphoreGive(fatfs_lock); \
    } while(0)

#endif // FATFS_LOCK_H
