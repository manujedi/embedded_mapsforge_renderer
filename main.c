/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <hardware/dma.h>
#include <src/display/lv_display.h>
#include <src/lv_init.h>
#include <src/misc/lv_color.h>
#include <src/misc/lv_palette.h>
#include <src/misc/lv_timer.h>
#include <src/tick/lv_tick.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mem.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "projdefs.h"
#include "task.h"

#include "main.h"

#include "display.h"
#include "hardware/xip_cache.h"
#include "psram.h"

#include "pico/async_context_freertos.h"

#include "lv_demos.h"

// Priorities of our threads - higher numbers are higher priority
#define MAIN_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)
#define BLINK_TASK_PRIORITY (tskIDLE_PRIORITY + 1UL)
#define WORKER_TASK_PRIORITY (tskIDLE_PRIORITY + 4UL)

TaskHandle_t maintask;

lv_display_t *display;
lv_obj_t *map_rotation_container;
lv_obj_t *map_scroll_container;
lv_obj_t *map_tile_canvas[5][5];

lv_anim_t a;
lv_obj_t *obj;

static void anim_x_cb(void *var, int32_t v) { lv_obj_set_x(var, v); }

static void anim_size_cb(void *var, int32_t v) { lv_obj_set_size(var, v, v); }

void lv_tick(__unused void *params) {
  while (1) {
    // dma_handler();
    vTaskDelay(pdMS_TO_TICKS(1));
    lv_tick_inc(1);
  }
}

void lv_timer(__unused void *params) {
  uint32_t timeout = 0;
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(timeout));
    timeout = lv_timer_handler();
  }
}

void main_task(__unused void *params) {

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
  lv_init();
  display = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
  uint8_t *buf = (uint8_t *)LVGL_FRAME_BUFFER;
  lv_display_set_buffers(display, buf, NULL, 466*466*2,
                         LV_DISPLAY_RENDER_MODE_DIRECT);
  display_prepeare_lvgl(display);

  
  obj = lv_obj_create(lv_scr_act());
  
  lv_obj_set_style_bg_color(obj, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);

  lv_obj_align(obj, LV_ALIGN_LEFT_MID, 10, 0);

  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_values(&a, 10, 50);
  lv_anim_set_time(&a, 1000);
  lv_anim_set_playback_delay(&a, 100);
  lv_anim_set_playback_time(&a, 300);
  lv_anim_set_repeat_delay(&a, 500);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);

  lv_anim_set_exec_cb(&a, anim_size_cb);
  lv_anim_start(&a);
  lv_anim_set_exec_cb(&a, anim_x_cb);
  lv_anim_set_values(&a, 10, 400);
  lv_anim_start(&a);

  xTaskCreate(lv_tick, "lv_tick", configMINIMAL_STACK_SIZE, NULL,
              BLINK_TASK_PRIORITY, NULL);

  xTaskCreate(lv_timer, "lv_timer", 16 * 1024, NULL, BLINK_TASK_PRIORITY, NULL);

  // remove us
  vTaskDelete(NULL);
}

int main(void) {
  stdio_init_all();

  /* Configure the hardware ready to run the demo. */
  const char *rtos_name;
  rtos_name = "FreeRTOS SMP";

  printf("Starting %s on both cores:\n", rtos_name);

  xTaskCreate(main_task, "MainThread", 16 * 1024, NULL, MAIN_TASK_PRIORITY,
              &maintask);

  // we must bind the main task to one core (well at least while the init is
  // called)
  vTaskCoreAffinitySet(maintask, 1);

  /* Start the tasks and timer running. */
  vTaskStartScheduler();

  return 0;
}
