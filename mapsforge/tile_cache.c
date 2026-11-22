/**
 * Tile Cache Implementation
 *
 * Stores rendered tiles as raw RGB565 binary files on the SD card.
 * Cache path structure: /caching/<zoom>/<x_hi>/<x_lo>/<y_hi>/<y_lo>.bin
 *
 * Each directory level uses 3 hex digit subdirectories, keeping the number
 * of entries per directory at a maximum of 4,096 (FAT32 limit: 65,534).
 *
 * Directories are created incrementally as we build the path — creating each
 * level before descending deeper.
 *
 * All FatFs calls are wrapped with LOCKED_FCALL for thread safety.
 *
 * Copyright (c) Manuel Jedinger
 * SPDX-License-Identifier: GPL-2.0
 */

#include "tile_cache.h"

#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "Drivers/FatFs/fatfs_lock.h"

// extern declaration for the global fatfs_lock (defined in main.c)
extern SemaphoreHandle_t fatfs_lock;


// split to two times 12bit
static void split_coord_hex(uint32_t coord, char *out_high, char *out_low) {
    uint32_t low = coord & 0xFFF;
    uint32_t high = coord >> 12;         // Remaining bits

    // Format: 3 hex digits each, zero-padded
    snprintf(out_high, 4, "%03x", (unsigned)high);
    snprintf(out_low, 4, "%03x", (unsigned)low);
}

void tile_cache_get_path(const char *base_path, uint8_t zoom,
                         uint32_t tile_x, uint32_t tile_y,
                         char *out, size_t out_size) {
    char x_hi[4], x_lo[4], y_hi[4], y_lo[4];
    split_coord_hex(tile_x, x_hi, x_lo);
    split_coord_hex(tile_y, y_hi, y_lo);

    // Path: /caching/19/4a3/e27/1a5/420.bin
    snprintf(out, out_size, "%s/%d/%s/%s/%s/%s.bin",
             base_path, zoom, x_hi, x_lo, y_hi, y_lo);
}

/**
 * Create all intermediate directories for a cache file.
 *
 * Builds the directory path component by component, creating each level
 * via f_mkdir before descending deeper.
 *
 * Path structure: /caching/<zoom>/<x_hi>/<x_lo>/<y_hi>
 *
 * @param base_path  Cache root (e.g. "/caching")
 * @param zoom       Zoom level
 * @param tile_x     Tile X coordinate
 * @param tile_y     Tile Y coordinate
 * @return FR_OK on success, error code on failure
 */
static FRESULT create_cache_dirs(const char *base_path, uint8_t zoom,
                                 uint32_t tile_x, uint32_t tile_y) {
    char path[128];
    char x_hi[4], x_lo[4], y_hi[4], y_lo[4];
    split_coord_hex(tile_x, x_hi, x_lo);
    split_coord_hex(tile_y, y_hi, y_lo);

    // Build path incrementally, creating each directory as we go
    const char *components[] = {
        NULL,                               // placeholder for zoom
        x_hi,                               // /4a3
        x_lo,                               // /e27
        y_hi                                // /1a5
    };

    // Start with base path
    int pos = snprintf(path, sizeof(path), "%s", base_path);
    if (pos < 0 || (size_t)pos >= sizeof(path)) return FR_INVALID_PARAMETER;

    for (int i = 0; i < 4; i++) {
        if (i == 0) {
            // Insert zoom level
            pos += snprintf(path + pos, sizeof(path) - pos, "/%d", zoom);
        } else {
            // Append component: /<name>
            pos += snprintf(path + pos, sizeof(path) - pos, "/%s", components[i]);
        }

        FRESULT res;
        LOCKED_FCALL(res = f_mkdir(path));
        if (res != FR_OK && res != FR_EXIST) {
            return res;
        }
    }

    return FR_OK;
}

int tile_cache_try_load(const char *base_path, uint8_t zoom,
                        uint32_t tile_x, uint32_t tile_y,
                        uint16_t *buff) {
    char path[128];
    tile_cache_get_path(base_path, zoom, tile_x, tile_y, path, sizeof(path));

    FIL cache_fp;
    FRESULT res;

    LOCKED_FCALL(res = f_open(&cache_fp, path, FA_READ));
    if (res != FR_OK) return 0;  // Cache miss (file not found)

    size_t bytes_read;
    LOCKED_FCALL(res = f_read(&cache_fp, buff, TILE_CACHE_SIZE, &bytes_read));

    LOCKED_FCALL(f_close(&cache_fp));

    if (res != FR_OK || bytes_read != TILE_CACHE_SIZE) {
        return 0;  // Corrupt or truncated cache file
    }

    return 1;  // Cache hit
}

int tile_cache_save(const char *base_path, uint8_t zoom,
                    uint32_t tile_x, uint32_t tile_y,
                    const uint16_t *buff) {
    // Create all intermediate directories as we traverse the tree
    FRESULT res = create_cache_dirs(base_path, zoom, tile_x, tile_y);
    if (res != FR_OK && res != FR_EXIST) {
        return 0;  // Could not create directories (e.g. disk full)
    }

    char path[128];
    tile_cache_get_path(base_path, zoom, tile_x, tile_y, path, sizeof(path));

    FIL cache_fp;
    size_t bytes_written;

    LOCKED_FCALL(res = f_open(&cache_fp, path, FA_WRITE | FA_CREATE_ALWAYS));
    if (res != FR_OK) return 0;  // Could not open/create file

    LOCKED_FCALL(res = f_write(&cache_fp, buff, TILE_CACHE_SIZE, &bytes_written));
    LOCKED_FCALL(f_close(&cache_fp));

    if (res != FR_OK || bytes_written != TILE_CACHE_SIZE) {
        return 0;  // Write failed (e.g. disk full)
    }

    return 1;  // Cache saved successfully
}
