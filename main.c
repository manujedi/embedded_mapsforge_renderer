/**
* Copyright (c) Manuel Jedinger
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <FreeRTOSConfig_examples_common.h>
#include <GUI/map_screen.h>
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/timer.h>
#include <mapsforge/mapsforge-renderer-queue.h>
#include <mapsforge/mapsforge-renderer.h>
#include <mapsforge/mapsforge.h>
#include <mapsforge/utils.h>
#include <pico/time.h>
#include <include/lvgl/lvgl.h>
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

#include "map_screen.h"
#include "GPS/LC76G.h"

TaskHandle_t maintask;

// stuff for tracing performance
uint64_t getRuntime(void) { return time_us_64(); }

// global needed locks
SemaphoreHandle_t lvgl_lock;
SemaphoreHandle_t i2c_lock;
SemaphoreHandle_t fatfs_lock;

lv_display_t *display;

mapsforge_header *map_header;

extern CST9217_Struct CST9217;

#define gesture_touch_points 10
lv_indev_touch_data_t touches[gesture_touch_points * 2];
uint8_t touch_cnt = 0;

void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  // gestures
  // if (touch_cnt) {
  //   lv_indev_gesture_recognizers_update(indev, touches, touch_cnt);
  //   lv_indev_gesture_recognizers_set_data(indev, data);
  // }
  // normal input
  data->point.x = CST9217.data[0].x;
  data->point.y = CST9217.data[0].y;
  data->state =
      CST9217.data[0].valid ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void input_gesture_detection_task(void *pvParameters) {

  while (1) {

    vTaskDelay(pdMS_TO_TICKS(50));
    CST9217_Read_Data();

    // gestures
    if (CST9217.points == 2) {

      // wait for release touch, too much data
      if (touch_cnt == gesture_touch_points * 2) {
        continue;
      }

      uint32_t time_ms = time_us_32() / 1000;

      for (uint8_t finger = 0; finger < 2; finger++) {
        /* Get the last event for the contact point */
        lv_indev_touch_data_t *cur = NULL;

        cur = &touches[touch_cnt];
        cur->id = finger;
        touch_cnt++;

        cur->point.x = CST9217.data[finger].x;
        cur->point.y = CST9217.data[finger].y;
        cur->state = CST9217.data[finger].valid ? LV_INDEV_STATE_PRESSED
                                                : LV_INDEV_STATE_RELEASED;

        cur->timestamp = time_ms;
      }

    } else {
      touch_cnt = 0;
    }
  }
}

void render_tile_task(void *pvParameters) {
  //uint64_t time_start = time_us_64();
  while (1) {
    if (render_tile())
      continue;
    // uint64_t took_time = time_us_64() - time_start;
    // printf("%" PRIu64 "s", took_time/1000000);
    // printf(" %" PRIu64 "ms\n", (took_time/1000)%1000);
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

extern GPS_information_t gps_information;

void gps_read_task(__unused void *params) {
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    LC76G_read_NMEA();
    if (xTaskGetTickCount() - 1000 < gps_information.timestamp_tick) {
      printf("lat %i, long %i, satellites: %i\n", gps_information.latm, gps_information.longm,gps_information.satellites);
    }
    else {
      printf("no gps fix\n");
    }
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

  // mapsforge display
  xSemaphoreTake(lvgl_lock, portMAX_DELAY);
  map_screen_init(map_header);
  xSemaphoreGive(lvgl_lock);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(MIN(LV_DEF_REFR_PERIOD, timeout)));
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
  //memset((uint8_t *)PSRAM_LOCATION, 0, PSRAM_SIZE);
  xip_cache_invalidate_all();

  // locks
  lvgl_lock = xSemaphoreCreateMutex();
  i2c_lock = xSemaphoreCreateMutex();
  fatfs_lock = xSemaphoreCreateMutex();

  // lvgl
  lv_init();
  display = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);

  //psram
  /*uint8_t *buf = (uint8_t *)LVGL_FRAME_BUFFER;
  lv_display_set_buffers(display, buf, NULL, 466 * 466 * 2,
                         LV_DISPLAY_RENDER_MODE_DIRECT);*/

  // internal ram
   static uint8_t buf[466*466*2];
   lv_display_set_buffers(display, buf, NULL, 466 * 466 * 2,
                          LV_DISPLAY_RENDER_MODE_DIRECT);

  display_prepeare_lvgl(display);

  // touch
  CST9217_Init();
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_read_cb);

  //gps
  LC76G_init();


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

  if (pdPASS != xTaskCreate(lv_tick, "lv_tick", configMINIMAL_STACK_SIZE, NULL, 15, NULL)) {
    asm("bkpt");
  }

  if (pdPASS != xTaskCreate(lv_timer, "lv_timer",  2*1024, NULL, 8, NULL)) {
    asm("bkpt");
  }

  if (pdPASS != xTaskCreate(render_tile_task, "mapsforge_render", 5*1024, NULL, 1, NULL)) {
    asm("bkpt");
  }

  if (pdPASS != xTaskCreate(input_gesture_detection_task, "input_gesture",
              configMINIMAL_STACK_SIZE, NULL, 12, NULL)) {
    asm("bkpt");
  }

  // xTaskCreate(gps_read_task, "gps_read_task", 1 * 1024, NULL, 10, NULL);

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

  xTaskCreate(main_task, "MainThread", configMINIMAL_STACK_SIZE, NULL, 15, &maintask);

  // we must bind the main task to one core (well at least while the init is
  // called)
  vTaskCoreAffinitySet(maintask, 1);

  /* Start the tasks and timer running. */
  vTaskStartScheduler();

  return 0;
}
