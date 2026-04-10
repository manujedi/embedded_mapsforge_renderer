/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <FreeRTOSConfig_examples_common.h>
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/timer.h>
#include <mapsforge/mapsforge-renderer-queue.h>
#include <mapsforge/mapsforge-renderer.h>
#include <mapsforge/mapsforge.h>
#include <mapsforge/utils.h>
#include <pico/time.h>
#include <src/display/lv_display.h>
#include <src/lv_init.h>
#include <src/misc/lv_color.h>
#include <src/misc/lv_palette.h>
#include <src/misc/lv_timer.h>
#include <src/tick/lv_tick.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "CST9217.h"
#include "ff.h"
#include "mem.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "portmacro.h"
#include "projdefs.h"
#include "sd_spi.h"
#include "task.h"

#include "main.h"

#include "display.h"
#include "hardware/xip_cache.h"
#include "psram.h"

#include "pico/async_context_freertos.h"

#include "lv_demos.h"


TaskHandle_t maintask;

// stuff for tracing performance
uint64_t getRuntime(void) { return time_us_64(); }

SemaphoreHandle_t lvgl_lock;
lv_display_t *display;
lv_obj_t *map_rotation_container;
lv_obj_t *map_scroll_container;
lv_obj_t *map_tile_canvas[5][5];

renderer_queue_config *queue_config;
mapsforge_header *map_header;
uint32_t tile_x;
uint32_t tile_y;
uint8_t zoom;

extern CST9217_Struct CST9217;

static void scroll_event_cb(lv_event_t *e) {
  lv_obj_t *map_obj = lv_event_get_user_data(e);

  int r = lv_obj_get_scroll_right(map_obj);
  int l = lv_obj_get_scroll_left(map_obj);
  int t = lv_obj_get_scroll_top(map_obj);
  int b = lv_obj_get_scroll_bottom(map_obj);

  printf("r %i l %i t %i b %i\n", r, l, t, b);

  uint16_t scroll_threshold = 0;

  if (r < scroll_threshold) {
    // save left colum buffers
    void *colum[5];

    for (int y = 0; y < 5; y++) {
      colum[y] = lv_obj_get_user_data(map_tile_canvas[0][y]);
    }

    // move everything one to the left
    for (int x = 0; x < 4; x++) {
      for (int y = 0; y < 5; y++) {
        uint16_t *buff = lv_obj_get_user_data(map_tile_canvas[x + 1][y]);
        lv_canvas_set_buffer(map_tile_canvas[x][y], buff, 256, 256,
                             LV_COLOR_FORMAT_RGB565);
        lv_obj_set_user_data(map_tile_canvas[x][y], buff);
      }
    }

    // set left buffer on the right
    for (int y = 0; y < 5; y++) {
      lv_canvas_set_buffer(map_tile_canvas[4][y], colum[y], 256, 256,
                           LV_COLOR_FORMAT_RGB565);
      lv_obj_set_user_data(map_tile_canvas[4][y], colum[y]);
    }

    // render new tiles
    tile_x += 1;
    // set left buffer on the right
    for (int y = 0; y < 5; y++) {
      // renderer_render_tile(map_header, zoom, tile_x + 4, tile_y + y,
      // colum[y]);
      renderer_queue_enqueue(queue_config, zoom, tile_x + 4, tile_y + y,
                             colum[y]);
    }

    // revert scroll by one tile
    // this needs to be done last because we may call this function recursively
    // if we scrolled more than 256px
    lv_obj_scroll_by(map_obj, 256, 0, LV_ANIM_OFF);
  }
  if (l < scroll_threshold) {
    // save right colum buffers
    void *colum[5];

    for (int y = 0; y < 5; y++) {
      colum[y] = lv_obj_get_user_data(map_tile_canvas[4][y]);
    }

    // move everything one to the right
    for (int x = 4; x > 0; x--) {
      for (int y = 0; y < 5; y++) {
        uint16_t *buff = lv_obj_get_user_data(map_tile_canvas[x - 1][y]);
        lv_canvas_set_buffer(map_tile_canvas[x][y], buff, 256, 256,
                             LV_COLOR_FORMAT_RGB565);
        lv_obj_set_user_data(map_tile_canvas[x][y], buff);
      }
    }

    // set right column on the left
    for (int y = 0; y < 5; y++) {
      lv_canvas_set_buffer(map_tile_canvas[0][y], colum[y], 256, 256,
                           LV_COLOR_FORMAT_RGB565);
      lv_obj_set_user_data(map_tile_canvas[0][y], colum[y]);
    }

    // render new tiles
    tile_x -= 1;
    // set left buffer on the right
    for (int y = 0; y < 5; y++) {
      // renderer_render_tile(map_header, zoom, tile_x, tile_y + y, colum[y]);
      renderer_queue_enqueue(queue_config, zoom, tile_x, tile_y + y, colum[y]);
    }

    // revert scroll by one tile
    // this needs to be done last because we may call this function recursively
    // if we scrolled more than 256px
    lv_obj_scroll_by(map_obj, -256, 0, LV_ANIM_OFF);
  }
  if (t < scroll_threshold) {
    // save bottom row buffers
    void *row[5];

    for (int x = 0; x < 5; x++) {
      row[x] = lv_obj_get_user_data(map_tile_canvas[x][4]);
    }

    // move everything one row down
    for (int x = 0; x < 5; x++) {
      for (int y = 4; y > 0; y--) {
        uint16_t *buff = lv_obj_get_user_data(map_tile_canvas[x][y - 1]);
        lv_canvas_set_buffer(map_tile_canvas[x][y], buff, 256, 256,
                             LV_COLOR_FORMAT_RGB565);
        lv_obj_set_user_data(map_tile_canvas[x][y], buff);
      }
    }

    // add row at top
    for (int x = 0; x < 5; x++) {
      lv_canvas_set_buffer(map_tile_canvas[x][0], row[x], 256, 256,
                           LV_COLOR_FORMAT_RGB565);
      lv_obj_set_user_data(map_tile_canvas[x][0], row[x]);
    }

    // render new tiles
    tile_y -= 1;
    // set left buffer on the right
    for (int x = 0; x < 5; x++) {
      // renderer_render_tile(map_header, zoom, tile_x + x, tile_y, row[x]);
      renderer_queue_enqueue(queue_config, zoom, tile_x + x, tile_y, row[x]);
    }

    // revert scroll by one tile
    // this needs to be done last because we may call this function recursively
    // if we scrolled more than 256px
    lv_obj_scroll_by(map_obj, 0, -256, LV_ANIM_OFF);
  }
  if (b < scroll_threshold) {
    // save top row buffers
    void *row[5];

    for (int x = 0; x < 5; x++) {
      row[x] = lv_obj_get_user_data(map_tile_canvas[x][0]);
    }

    // move everything one row up
    for (int x = 0; x < 5; x++) {
      for (int y = 0; y < 4; y++) {
        uint16_t *buff = lv_obj_get_user_data(map_tile_canvas[x][y + 1]);
        lv_canvas_set_buffer(map_tile_canvas[x][y], buff, 256, 256,
                             LV_COLOR_FORMAT_RGB565);
        lv_obj_set_user_data(map_tile_canvas[x][y], buff);
      }
    }

    // add row at bottom
    for (int x = 0; x < 5; x++) {
      lv_canvas_set_buffer(map_tile_canvas[x][4], row[x], 256, 256,
                           LV_COLOR_FORMAT_RGB565);
      lv_obj_set_user_data(map_tile_canvas[x][4], row[x]);
    }

    // render new tiles
    tile_y += 1;
    // set left buffer on the right
    for (int x = 0; x < 5; x++) {
      // renderer_render_tile(map_header, zoom, tile_x + x, tile_y + 4, row[x]);
      renderer_queue_enqueue(queue_config, zoom, tile_x + x, tile_y + 4,
                             row[x]);
    }

    // revert scroll by one tile
    // this needs to be done last because we may call this function recursively
    // if we scrolled more than 256px
    lv_obj_scroll_by(map_obj, 0, +256, LV_ANIM_OFF);
  }
}

void pointer_read(lv_indev_t *indev, lv_indev_data_t *data) {
  data->point.x = CST9217.data[0].x;
  data->point.y = CST9217.data[0].y;
  data->state =
      CST9217.data[0].valid ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void render_done_cb(void *buff) {
  xSemaphoreTake(lvgl_lock, portMAX_DELAY);
  for (int x = 0; x < 5; x++) {
    for (int y = 0; y < 5; y++) {
      if (buff == lv_obj_get_user_data(map_tile_canvas[x][y])) {
        lv_obj_invalidate(map_tile_canvas[x][y]);
        xSemaphoreGive(lvgl_lock);
        return;
      }
    }
  }
  xSemaphoreGive(lvgl_lock);
}

void render_tile_task(void *pvParameters) {
  while (1) {
    if (render_queue_render_item(queue_config))
      continue;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void lv_tick(__unused void *params) {
  while (1) {
    // dma_handler();
    vTaskDelay(pdMS_TO_TICKS(5));
    lv_tick_inc(5);
  }
}

void lv_timer(__unused void *params) {
  uint32_t timeout = 0;
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(MIN(LV_DEF_REFR_PERIOD, timeout)));
    CST9217_Read_Data(); // this is only needed for gui, so do it right before
                         // render
    xSemaphoreTake(lvgl_lock, portMAX_DELAY);
    timeout = lv_timer_handler();
    xSemaphoreGive(lvgl_lock);
  }
}

void main_task(__unused void *params) {
  stdio_init_all();


  // display
  display_init();

  // psram
  uint32_t psram_size = 0;
  xip_cache_clean_all();
  psram_size = psram_init(8);
  assert(psram_size == PSRAM_SIZE);
  memset((uint8_t *)PSRAM_LOCATION, 0, PSRAM_SIZE);
  xip_cache_invalidate_all();

  // lvgl
  lvgl_lock = xSemaphoreCreateMutex();
  lv_init();
  display = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
  uint8_t *buf = (uint8_t *)LVGL_FRAME_BUFFER;
  lv_display_set_buffers(display, buf, NULL, 466 * 466 * 2,
                         LV_DISPLAY_RENDER_MODE_DIRECT);

  // uint8_t *buf = (uint8_t *)pvPortCalloc(1,466*466*2);
  // lv_display_set_buffers(display, buf, NULL, 466 * 466 * 2,
  //                        LV_DISPLAY_RENDER_MODE_DIRECT);

  display_prepeare_lvgl(display);

  // touch
  CST9217_Init();
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, pointer_read);

  // sdcard
  sd_spi_config_t sd_spi = {spi0, 21, 18, 19, 20, 200000, 12000000};
  sd_spi_init(&sd_spi);

  // mount
  FATFS fs;
  FRESULT res;
  res = f_mount(&fs, "/", 1);
  if (res != FR_OK)
    printf("couldn't mount sd card");

  // mapsforge
  // map_header = (mapsforge_header *)calloc(1, sizeof(mapsforge_header));
  map_header = (mapsforge_header *)pvPortCalloc(1, sizeof(mapsforge_header));
  renderer_init_map(map_header,
                    "/austria-orig-settings-nodebug-v27-03simple.map");

  queue_config =
      (renderer_queue_config *)calloc(1, sizeof(renderer_queue_config));
  renderer_queue_init(map_header, queue_config, render_done_cb);

  // mapsforge display

  lv_style_t style;
  lv_style_init(&style);
  lv_style_set_border_width(&style, 0);
  lv_style_set_bg_color(&style, lv_color_black());
  lv_style_set_pad_all(&style, 0);

  lv_obj_t *screen = lv_obj_create(NULL);
  lv_obj_set_style_border_width(screen, 0, 0);

  lv_obj_t *map_obj = lv_obj_create(screen);
  lv_obj_add_style(map_obj, &style, 0);
  lv_obj_set_size(map_obj, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  lv_obj_set_scroll_dir(map_obj, LV_DIR_ALL);
  lv_obj_add_flag(map_obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(map_obj, LV_SCROLLBAR_MODE_OFF);
  lv_obj_remove_flag(map_obj, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_add_event_cb(map_obj, scroll_event_cb, LV_EVENT_SCROLL, map_obj);

  lv_obj_t *map_grid = lv_obj_create(map_obj);
  lv_obj_add_style(map_grid, &style, 0);
  lv_obj_set_size(map_grid, 5 * 256, 5 * 256); // total size of grid
  lv_obj_set_scroll_dir(map_grid, LV_DIR_NONE);
  lv_obj_remove_flag(map_grid, LV_OBJ_FLAG_SCROLLABLE);

  zoom = 19;
  tile_x = longMd2tilex(15458252, zoom);
  tile_y = latMd2tiley(47059026, zoom);

  // center map
  tile_x -= 2;
  tile_y -= 2;

  for (int x = 0; x < 5; x++) {
    for (int y = 0; y < 5; y++) {
      uint16_t *framebuffer =
          ((uint16_t *)TILE_BUFFER) + 256 * 256 * (y * 5 + x);

      map_tile_canvas[x][y] = lv_canvas_create(map_grid);
      lv_canvas_set_buffer(map_tile_canvas[x][y], framebuffer, 256, 256,
                           LV_COLOR_FORMAT_RGB565);
      lv_obj_set_pos(map_tile_canvas[x][y], x * 256, y * 256);
      lv_obj_set_user_data(map_tile_canvas[x][y], framebuffer);

      renderer_queue_register_buffer(queue_config, framebuffer);
      renderer_queue_enqueue(queue_config, zoom, tile_x + x, tile_y + y,
                             framebuffer);
    }
  }

  lv_screen_load(screen);

  // call this once or scroll doesn't work
  lv_timer_handler();

  // scroll by 2 tiles to center map
  lv_obj_scroll_by(map_obj, -2 * 256, -2 * 256, LV_ANIM_OFF);

  // center loaded tile
  lv_obj_scroll_by(map_obj, (DISPLAY_WIDTH - 256) / 2,
                   (DISPLAY_HEIGHT - 256) / 2, LV_ANIM_OFF);

  xTaskCreate(lv_tick, "lv_tick", configMINIMAL_STACK_SIZE, NULL,
              15, NULL);

  xTaskCreate(lv_timer, "lv_timer", 16 * 1024, NULL, 8,
              NULL);

  xTaskCreate(render_tile_task, "mapsforge_render", 16 * 1024, NULL,
              0, NULL);


  for (uint16_t interrupt_number = 0; interrupt_number < 52; interrupt_number++) {
    printf("i %02d en: %i prio: %i\n", interrupt_number, irq_is_enabled(interrupt_number), irq_get_priority(interrupt_number));
  }

  // remove us
  vTaskDelete(NULL);
}

int main(void) {
  static uint32_t cpu_f_khz = 200 * 1000;
  set_sys_clock_khz(cpu_f_khz, true);
  clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                  cpu_f_khz * 1000, cpu_f_khz * 1000);

  /* Configure the hardware ready to run the demo. */
  const char *rtos_name;
  rtos_name = "FreeRTOS SMP";

  printf("Starting %s on both cores:\n", rtos_name);

  xTaskCreate(main_task, "MainThread", 16 * 1024, NULL, 15,
              &maintask);

  // we must bind the main task to one core (well at least while the init is
  // called)
  vTaskCoreAffinitySet(maintask, 1);

  /* Start the tasks and timer running. */
  vTaskStartScheduler();

  return 0;
}
