#ifndef MAPSFORGE_TILE_CACHE_H
#define MAPSFORGE_TILE_CACHE_H

#include <stdint.h>
#include "mapsforge/mapsforge.h"

/**
 * Generate the cache file path for a tile.
 *
 * Directory structure: /caching/<zoom>/<x_hex_high3>/<x_hex_low3>/<y_hex_high3>/<y_hex_low3>.bin
 *
 * This splits x and y into two hex subdirectory levels each, ensuring no
 * directory exceeds 4,096 entries (FAT32 limit is 65,534, exFAT is ~8M).
 *
 * Path depth: 5 (caching / zoom / x_hi / x_lo / y_hi / y_lo.bin)
 * FF_PATH_DEPTH must be >= 5 (default is 10, so this is safe).
 *
 * @param base_path   Cache root (e.g. "/caching")
 * @param zoom        Zoom level
 * @param tile_x      Tile X coordinate
 * @param tile_y      Tile Y coordinate
 * @param out         Output buffer (must be at least 128 bytes)
 * @param out_size    Size of output buffer
 */
void tile_cache_get_path(const char *base_path, uint8_t zoom,
                         uint32_t tile_x, uint32_t tile_y,
                         char *out, size_t out_size);

/**
 * Try to load a tile from the cache.
 *
 * Opens the cache file with LOCKED_FCALL, reads the full tile buffer
 * (256x256 RGB565 = 131,072 bytes), and verifies the file size matches.
 *
 * @param base_path  Cache root (e.g. "/caching")
 * @param zoom       Zoom level
 * @param tile_x     Tile X coordinate
 * @param tile_y     Tile Y coordinate
 * @param buff       Destination buffer (must be 131,072 bytes)
 * @return 1 if cache hit and tile loaded, 0 if cache miss or error
 */
int tile_cache_try_load(const char *base_path, uint8_t zoom,
                        uint32_t tile_x, uint32_t tile_y,
                        uint16_t *buff);

/**
 * Save a rendered tile to the cache.
 *
 * Creates all intermediate directories (using LOCKED_FCALL), writes the full
 * tile buffer (256x256 RGB565 = 131,072 bytes), and closes the file.
 *
 * If directory creation fails, the function silently returns 0 (no cache file
 * is created, but no error is raised - caching is best-effort).
 *
 * @param base_path  Cache root (e.g. "/caching")
 * @param zoom       Zoom level
 * @param tile_x     Tile X coordinate
 * @param tile_y     Tile Y coordinate
 * @param buff       Source buffer (must be 131,072 bytes)
 * @return 1 on success, 0 on failure (silently, e.g. disk full)
 */
int tile_cache_save(const char *base_path, uint8_t zoom,
                    uint32_t tile_x, uint32_t tile_y,
                    const uint16_t *buff);

/**
 * Get the tile size in bytes (256x256 RGB565).
 */
#define TILE_CACHE_SIZE (256 * 256 * 2)  // 131072

#endif // MAPSFORGE_TILE_CACHE_H
