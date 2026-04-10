#include "mapsforge-renderer-queue.h"

#include <pthread.h>

#include "mapsforge-renderer.h"

void renderer_queue_init(mapsforge_header *header,
                         renderer_queue_config *config, render_done_cb_t cb) {
  config->header = header;
  config->queue_lock = xSemaphoreCreateMutex();
  config->render_done_cb = cb;

  for (int i = 0; i < sizeof(config->items) / sizeof(config->items[0]); ++i) {
    config->items[i].status = DONE;
    config->items[i].zoom = 0;
    config->items[i].tile_x = 0;
    config->items[i].tile_y = 0;
    config->items[i].buff = NULL;
  }
}

void renderer_queue_register_buffer(renderer_queue_config *config, void *buff) {
  xSemaphoreTake(config->queue_lock, portMAX_DELAY);
  for (int i = 0; i < sizeof(config->items) / sizeof(config->items[0]); ++i) {
    if (config->items[i].buff == NULL) {
      config->items[i].status = DONE;
      config->items[i].buff = buff;
      xSemaphoreGive(config->queue_lock);
      return;
    }
  }
  printf("Error, queue not big enough\n");
  xSemaphoreGive(config->queue_lock);
}

void renderer_queue_enqueue(renderer_queue_config *config, uint8_t zoom,
                            uint32_t tile_x, uint32_t tile_y, void *buff) {
  xSemaphoreTake(config->queue_lock, portMAX_DELAY);

  for (int i = 0; i < sizeof(config->items) / sizeof(config->items[0]); ++i) {
    if (config->items[i].buff == buff) {
      config->items[i].zoom = zoom;
      config->items[i].tile_x = tile_x;
      config->items[i].tile_y = tile_y;

      switch (config->items[i].status) {
      case TODO:
        config->items[i].status = TODO;
        break;
      case CLEARED:
        config->items[i].status = CLEARED;
        break;
      case RENDERING:
        config->items[i].status = TODO;
        break;
      case DONE:
        config->items[i].status = TODO;
        break;
      }
      xSemaphoreGive(config->queue_lock);
      return;
    }
  }

  // error
  printf("Error enqueuing tile\n");
  xSemaphoreGive(config->queue_lock);
}

int render_queue_render_item(renderer_queue_config *config) {
  uint8_t idx = 0;
  uint8_t zoom = 0;
  uint32_t tile_x = 0;
  uint32_t tile_y = 0;
  uint16_t *buff = NULL;

  xSemaphoreTake(config->queue_lock, portMAX_DELAY);

  for (int i = 0; i < sizeof(config->items) / sizeof(config->items[0]); ++i) {
    switch (config->items[i].status) {
    case TODO:
      idx = i;
      buff = config->items[i].buff;
      break;
    case CLEARED:
      break;
    case RENDERING:
      break;
    case DONE:
      break;
    }
    if (buff) {
      break;
    }
  }

  if (buff) {
    xSemaphoreGive(config->queue_lock);

    renderer_clear_tile(buff);
    config->render_done_cb(buff);

    xSemaphoreTake(config->queue_lock, portMAX_DELAY);
    config->items[idx].status = CLEARED;
    xSemaphoreGive(config->queue_lock);

    return 1;
  }

  // everything cleared, lets start rendering
  buff = NULL;
  for (int i = 0; i < sizeof(config->items) / sizeof(config->items[0]); ++i) {
    switch (config->items[i].status) {
    case TODO:
      printf("This should not be possible\n");
      break;
    case CLEARED:
      idx = i;
      zoom = config->items[i].zoom;
      tile_x = config->items[i].tile_x;
      tile_y = config->items[i].tile_y;
      buff = config->items[i].buff;
      break;
    case RENDERING:
      printf("This should not be possible\n");
      break;
    case DONE:
      break;
    }
    if (buff) {
      break;
    }
  }

  if (buff) {
    config->items[idx].status = RENDERING;
  }
  xSemaphoreGive(config->queue_lock);

  if (!buff) {
    return 0;
  }

  renderer_render_tile(config->header, zoom, tile_x, tile_y, buff);
  config->render_done_cb(buff);

  xSemaphoreTake(config->queue_lock, portMAX_DELAY);
  if (config->items[idx].status == RENDERING) {
    config->items[idx].status = DONE;
  }
  xSemaphoreGive(config->queue_lock);
  return 1;
}
