#pragma once
#include <stdint.h>
#include <stdio.h>
#include "ff.h"

typedef union way_special_byte way_special_byte;
typedef union way_flags way_flags;
typedef struct bounding_box bounding_box;
typedef union header_flags header_flags;
typedef struct zoom_interval_configuration zoom_interval_configuration;
typedef struct way_list_entry_way way_list_entry_way;
typedef struct way_list_entry_way_name way_list_entry_way_name;
typedef struct mapsforge_header mapsforge_header;

#include "mapsforge/mapsforge_theme.h"


union way_special_byte {
    struct __attribute__((packed)) {
        uint8_t amount_of_tags: 4;
        uint8_t osm_tag_layer: 4;
    } bits;

    uint8_t value;
};

union way_flags {
    struct __attribute__((packed)) {
        uint8_t future_use: 2;
        uint8_t delta_encoding_type: 1;
        uint8_t number_of_way_data_blocks: 1;
        uint8_t label_pos: 1;
        uint8_t reference: 1;
        uint8_t house_number: 1;
        uint8_t way_name: 1;
    } bits;

    uint8_t value;
};

struct __attribute__((packed)) bounding_box {
    int32_t minLat;
    int32_t minLon;
    int32_t maxLat;
    int32_t maxLon;
};

union header_flags {
    struct __attribute__((packed)) {
        uint8_t reserved: 2;
        uint8_t created_by_exists: 1;
        uint8_t comment_exists: 1;
        uint8_t language_exists: 1;
        uint8_t map_start_zoom_exists: 1;
        uint8_t map_start_exists: 1;
        uint8_t debug: 1;
    } bits;

    uint8_t value;
};

struct __attribute__((packed)) zoom_interval_configuration {
    uint8_t base_zoom_level;
    uint8_t min_zoom_level;
    uint8_t max_zoom_level;
    uint64_t absolute_start_position;
    uint64_t size_of_sub_file;
    // non mapsforge helper
    uint32_t tile_x_start;
    uint32_t tile_x_end;
    uint32_t tile_y_start;
    uint32_t tile_y_end;
};

//this is a single linked list of ways
// used to create a per layer ways list (to render them in order, draw layer 5 over layer 4...)
typedef enum way_list_entry_type:uint8_t {
    WAY,
    WAY_NAME,
} way_list_entry_type;

struct way_list_entry_way {
    way_list_entry_way *next;
    uint16_t tag_id;
    uint32_t coords_cnt;
    int32_t coords[];
};

struct way_list_entry_way_name {
    way_list_entry_way_name *next;
    int32_t mlat;
    int32_t mlon;
    uint16_t name_len;
    char name[];
};

// this is mostly the mapsforge header + the tile index in the zoom interval configs
struct mapsforge_header {
    FIL map_fp;

    // char magic_byte[20];
    uint32_t header_size;
    uint32_t file_version;
    uint64_t file_size;
    uint64_t date_of_creation;
    bounding_box bounding_box;
    uint16_t tile_size;
    // char *projection;
    header_flags flags;
    int32_t map_start_position[2];
    uint8_t start_zoom_level;
    /*char *language;
    char *comment;
    char *created_by;*/
    uint16_t poi_tags_cnt;
    char **poi_tags;
    uint16_t way_tags_cnt;
    char **way_tags;
    const theme **way_tag_theme_lookup;
    uint8_t amount_of_zoom_intervals;
    zoom_interval_configuration *zoom_configurations;
};
