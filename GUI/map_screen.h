#ifndef INCLUDE_MAP_SCREEN_H
#define INCLUDE_MAP_SCREEN_H

#include "stddef.h"
#include "stdint.h"
#include "lvgl.h"
#include <mapsforge/mapsforge-renderer-queue.h>

void map_screen_init(mapsforge_header* map_header);

lv_obj_t* get_map_screen(void);

int render_tile(void);

#endif
