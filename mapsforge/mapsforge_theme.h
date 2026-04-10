#pragma once
#include <stdint.h>

#include "src/misc/lv_color.h"

typedef struct theme theme;

#include "mapsforge/mapsforge.h"


struct theme{
    char name[256];
    uint8_t z_layer; // 0-15, within each layer the osm layer is used
    bool allow_polygon;
    lv_color_t color;
    uint8_t line_width[22]; // 0-21
};

const theme* get_theme(char* tag);