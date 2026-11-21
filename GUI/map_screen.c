#include "map_screen.h"
#include "PSRAM/mem.h"
#include "lvgl.h"
#include "mapsforge/utils.h"
#include <main.h>
#include <mapsforge/mapsforge-renderer-queue.h>
#include <mapsforge/mapsforge.h>
#include <src/core/lv_obj.h>
#include <src/core/lv_obj_pos.h>
#include <src/indev/lv_indev.h>
#include <src/misc/lv_area.h>
#include <stdint.h>
#include <stdlib.h>

static lv_style_t style;
static lv_obj_t *screen;
static lv_obj_t *map_obj;
static lv_obj_t *map_grid;
static lv_obj_t *map_tile_canvas[5][5];

// static lv_obj_t *zoom_label;
static lv_obj_t *zoom_plus_button;
static lv_obj_t *plus_label;
static lv_obj_t *zoom_minus_button;
static lv_obj_t *minus_label;

static uint32_t longitude = 15458252;
static uint32_t latitude = 47059026;
static uint8_t zoom;
// tile x any y at the top left from the current rendered area
static uint32_t tile_x;
static uint32_t tile_y;
// center of the screen in offset from top left corner
static uint16_t x_offset_px;
static uint16_t y_offset_px;

renderer_queue_config *queue_config;

extern SemaphoreHandle_t lvgl_lock;

static void scroll_event_cb(lv_event_t *e);
static void gesture_event_cb(lv_event_t *e);
static void zoom_plus_event_cb(lv_event_t *e);
static void zoom_minus_event_cb(lv_event_t *e);
void render_done_cb(void *buff);

void map_screen_init(mapsforge_header *map_header) {

  queue_config =
      (renderer_queue_config *)calloc(1, sizeof(renderer_queue_config));
  renderer_queue_init(map_header, queue_config, render_done_cb);

  lv_style_init(&style);
  lv_style_set_border_width(&style, 0);
  lv_style_set_bg_color(&style, lv_color_black());
  lv_style_set_pad_all(&style, 0);

  screen = lv_obj_create(NULL);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_add_style(screen, &style, 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  map_obj = lv_obj_create(screen);
  lv_obj_add_style(map_obj, &style, 0);
  lv_obj_set_size(map_obj, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  lv_obj_set_scroll_dir(map_obj, LV_DIR_ALL);
  lv_obj_add_flag(map_obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(map_obj, LV_SCROLLBAR_MODE_OFF);
  lv_obj_remove_flag(map_obj, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_add_event_cb(map_obj, scroll_event_cb, LV_EVENT_SCROLL, map_obj);

  map_grid = lv_obj_create(map_obj);
  lv_obj_add_style(map_grid, &style, 0);
  lv_obj_set_size(map_grid, 5 * 256, 5 * 256); // total size of grid
  lv_obj_set_scroll_dir(map_grid, LV_DIR_NONE);
  lv_obj_remove_flag(map_grid, LV_OBJ_FLAG_SCROLLABLE);

  // Create plus button (moved before minus)
  zoom_plus_button = lv_btn_create(screen);
  lv_obj_add_style(zoom_plus_button, &style, 0);
  lv_obj_set_pos(zoom_plus_button, DISPLAY_WIDTH - 40, DISPLAY_HEIGHT / 2);
  lv_obj_set_size(zoom_plus_button, 35, 30);
  lv_obj_set_style_bg_color(zoom_plus_button, lv_palette_main(LV_PALETTE_GREY),
                            0);
  lv_obj_add_event_cb(zoom_plus_button, zoom_plus_event_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_remove_flag(zoom_plus_button, LV_OBJ_FLAG_SCROLLABLE);
  plus_label = lv_label_create(zoom_plus_button);
  lv_obj_add_style(plus_label, &style, 0);
  lv_label_set_text(plus_label, LV_SYMBOL_PLUS);
  lv_obj_set_style_text_font(plus_label, &lv_font_montserrat_24, 0);
  lv_obj_center(plus_label);
  lv_obj_remove_flag(plus_label, LV_OBJ_FLAG_SCROLLABLE);

  // Create minus button (moved after plus)
  zoom_minus_button = lv_btn_create(screen);
  lv_obj_add_style(zoom_minus_button, &style, 0);
  lv_obj_set_pos(zoom_minus_button, 40 - 35, DISPLAY_HEIGHT / 2);
  lv_obj_set_size(zoom_minus_button, 35, 30);
  lv_obj_set_style_bg_color(zoom_minus_button, lv_palette_main(LV_PALETTE_GREY),
                            0);
  lv_obj_add_event_cb(zoom_minus_button, zoom_minus_event_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_remove_flag(zoom_minus_button, LV_OBJ_FLAG_SCROLLABLE);
  minus_label = lv_label_create(zoom_minus_button);
  lv_obj_add_style(minus_label, &style, 0);
  lv_label_set_text(minus_label, LV_SYMBOL_MINUS);
  lv_obj_set_style_text_font(minus_label, &lv_font_montserrat_24, 0);
  lv_obj_center(minus_label);
  lv_obj_remove_flag(minus_label, LV_OBJ_FLAG_SCROLLABLE);

  zoom = 19;
  tile_x = longMd2tilex(longitude, zoom);
  tile_y = latMd2tiley(latitude, zoom);

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

  // make sure it does something
  lv_tick_inc(1000);
  lv_timer_handler();

  // center loaded tile
  lv_obj_scroll_by(map_obj, (DISPLAY_WIDTH - 256) / 2,
                   (DISPLAY_HEIGHT - 256) / 2, LV_ANIM_OFF);

  lv_tick_inc(1000);
  lv_timer_handler();
}

lv_obj_t *get_map_screen(void) { return screen; }

static void zoom_plus_event_cb(lv_event_t *e) {

  zoom += 1;
  tile_x = longMd2tilex(longitude, zoom);
  tile_y = latMd2tiley(latitude, zoom);

  // center map
  tile_x -= 2;
  tile_y -= 2;

  for (int x = 0; x < 5; x++) {
    for (int y = 0; y < 5; y++) {
      uint16_t *framebuffer =
          ((uint16_t *)TILE_BUFFER) + 256 * 256 * (y * 5 + x);

      lv_canvas_set_buffer(map_tile_canvas[x][y], framebuffer, 256, 256,
                           LV_COLOR_FORMAT_RGB565);
      lv_obj_set_user_data(map_tile_canvas[x][y], framebuffer);
      renderer_queue_enqueue(queue_config, zoom, tile_x + x, tile_y + y,
                             framebuffer);
    }
  }
}

static void zoom_minus_event_cb(lv_event_t *e) {

  zoom -= 1;

  tile_x = longMd2tilex(longitude, zoom);
  tile_y = latMd2tiley(latitude, zoom);

  // center map
  tile_x -= 2;
  tile_y -= 2;

  for (int x = 0; x < 5; x++) {
    for (int y = 0; y < 5; y++) {
      uint16_t *framebuffer =
          ((uint16_t *)TILE_BUFFER) + 256 * 256 * (y * 5 + x);

      lv_canvas_set_buffer(map_tile_canvas[x][y], framebuffer, 256, 256,
                           LV_COLOR_FORMAT_RGB565);
      lv_obj_set_user_data(map_tile_canvas[x][y], framebuffer);
      renderer_queue_enqueue(queue_config, zoom, tile_x + x, tile_y + y,
                             framebuffer);
    }
  }
}

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

  x_offset_px = l + (DISPLAY_WIDTH/2);
  y_offset_px = t + (DISPLAY_HEIGHT/2);

}

int render_tile(void) { return render_queue_render_item(queue_config); }

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

int helper(void) {



  return 0;
}