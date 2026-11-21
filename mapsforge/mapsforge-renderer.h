#pragma once
#include "ff.h"
#include "mapsforge/mapsforge.h"
#include "src/misc/lv_types.h"


int renderer_init_map(mapsforge_header *header, const char *path);

int renderer_deinit_map(const mapsforge_header *header);

int renderer_clear_tile(uint16_t *img_buff);

int renderer_get_tile_offset(mapsforge_header *header, uint8_t zoom, uint32_t tile_x, uint32_t tile_y,
                             uint64_t *tile_offset, uint64_t* tile_end_offset);

int renderer_draw_line_px(uint32_t tile_x,
                          uint32_t tile_y,
                          uint32_t line_x0,
                          uint32_t line_x1,
                          uint32_t line_y0,
                          uint32_t line_y1,
                          uint16_t color,
                          uint8_t thickness,
                          uint16_t *img_buff);

int renderer_render_tile(mapsforge_header *header, uint8_t zoom, uint32_t tile_x, uint32_t tile_y,
                         uint16_t *img_buff);
