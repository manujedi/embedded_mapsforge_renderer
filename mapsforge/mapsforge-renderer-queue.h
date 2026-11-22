#ifndef MAPSFORGE_RENDERER_MAPSFORGE_RENDERER_QUEUE_H
#define MAPSFORGE_RENDERER_MAPSFORGE_RENDERER_QUEUE_H
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

#include "pico/async_context_freertos.h"

#include "mapsforge.h"


typedef struct renderer_queue_config renderer_queue_config;
typedef struct renderer_queue_item renderer_queue_item;

#include "mapsforge/mapsforge.h"

typedef enum render_status:uint8_t {
    TODO,
    CLEARED,
    RENDERING,
    DONE,
} render_status;


struct renderer_queue_item {
    render_status status;
    uint8_t zoom;
    uint32_t tile_x;
    uint32_t tile_y;
    void *buff;
};

typedef void (*render_done_cb_t)(void*);

// Tile cache configuration
typedef struct {
    char base_path[64];       // e.g. "/caching"
    int enabled;              // 0=disabled, 1=enabled
} renderer_tile_cache_config_t;

struct renderer_queue_config {
    mapsforge_header *header;
    SemaphoreHandle_t queue_lock;
    render_done_cb_t render_done_cb;
    renderer_queue_item items[5 * 5];
    renderer_tile_cache_config_t cache;
};

void renderer_queue_init(mapsforge_header *header, renderer_queue_config *config, render_done_cb_t cb);

void renderer_queue_register_buffer(renderer_queue_config *config, void *buff);

void renderer_queue_enqueue(renderer_queue_config *config, uint8_t zoom, uint32_t tile_x, uint32_t tile_y, void *buff);

int render_queue_render_item(renderer_queue_config *config);

#endif //MAPSFORGE_RENDERER_MAPSFORGE_RENDERER_QUEUE_H
