#include "mapsforge-renderer.h"

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mapsforge/mapsforge_theme.h"
#include "mapsforge/utils.h"
#include "mem.h"
#include "FreeRTOS.h"
#include "portable.h"
#include "semphr.h"
#include "hardware/platform_defs.h"
#include "src/draw/lv_draw_vector.h"
#include "src/widgets/canvas/lv_canvas.h"


extern SemaphoreHandle_t lvgl_lock;

int renderer_init_map(mapsforge_header *header, const char *path) {
    uint8_t *buffer = (uint8_t *) FILE_BUFFER;

    f_open(&header->map_fp, path, FA_READ);

    unsigned int read;
    f_read(&header->map_fp, buffer, 24, &read);

    if (memcmp("mapsforge binary OSM", buffer, 20) != 0) {
        printf("not a mapsforge file\n");
        return 1;
    }

    uint32_t header_size = readBigEndian32U(buffer + 20);
    if (header_size > FILE_BUFFER_SIZE) {
        printf("header too big\n");
        return 1;
    }


    uint8_t *hp = (uint8_t *) FILE_BUFFER;

    f_lseek(&header->map_fp, 24);
    f_read(&header->map_fp, (uint8_t *) FILE_BUFFER, header_size + 512, &read);

    header->header_size = header_size;
    header->file_version = readBigEndian32U(hp);
    hp += 4;
    header->file_size = readBigEndian64(hp);
    hp += 8;
    header->date_of_creation = readBigEndian64(hp);
    hp += 8;

    header->bounding_box.minLat = readBigEndian32S(hp);
    hp += 4;
    header->bounding_box.minLon = readBigEndian32S(hp);
    hp += 4;
    header->bounding_box.maxLat = readBigEndian32S(hp);
    hp += 4;
    header->bounding_box.maxLon = readBigEndian32S(hp);
    hp += 4;

    header->tile_size = readBigEndian16(hp);
    hp += 2;

    //read projection
    char *dummy;
    uint32_t dummy_len;
    hp += parseVBEUStringInplace(&dummy, hp, &dummy_len);

    header->flags.value = *hp;
    hp += 1;

    if (header->flags.bits.map_start_exists) {
        for (int i = 0; i < 2; ++i) {
            header->map_start_position[i] = readBigEndian32S(hp);
            hp += 4;
        }
    }
    if (header->flags.bits.map_start_zoom_exists) {
        header->start_zoom_level = *hp;
        hp += 1;
    }
    if (header->flags.bits.language_exists) {
        hp += parseVBEUStringInplace(&dummy, hp, &dummy_len);
    }
    if (header->flags.bits.comment_exists) {
        hp += parseVBEUStringInplace(&dummy, hp, &dummy_len);
    }
    if (header->flags.bits.created_by_exists) {
        hp += parseVBEUStringInplace(&dummy, hp, &dummy_len);
    }
    if (header->flags.bits.reserved) {
        printf("Future flags in header, not handled\n");
    }; //Ignore?

    header->poi_tags_cnt = readBigEndian16(hp);
    hp += 2;
    header->poi_tags = pvPortCalloc(header->poi_tags_cnt, sizeof(header->poi_tags[0]));
    for (int i = 0; i < header->poi_tags_cnt; ++i) {
        hp += parseVBEUString(&header->poi_tags[i], hp);
    }

    header->way_tags_cnt = readBigEndian16(hp);
    hp += 2;
    header->way_tags = pvPortCalloc(header->way_tags_cnt, sizeof(header->way_tags[0]));
    for (int i = 0; i < header->way_tags_cnt; ++i) {
        hp += parseVBEUString(&header->way_tags[i], hp);
        //printf("%s\n", header->way_tags[i]);
    }

    // render helper
    header->way_tag_theme_lookup = pvPortCalloc(1,sizeof(theme *) * header->way_tags_cnt);
    for (int i = 0; i < header->way_tags_cnt; ++i) {
        header->way_tag_theme_lookup[i] = get_theme(header->way_tags[i]);
    }

    header->amount_of_zoom_intervals = *hp;
    hp += 1;
    header->zoom_configurations = pvPortCalloc(header->amount_of_zoom_intervals, sizeof(zoom_interval_configuration));
    for (int i = 0; i < header->amount_of_zoom_intervals; ++i) {
        header->zoom_configurations[i].base_zoom_level = *hp;
        hp += 1;
        header->zoom_configurations[i].min_zoom_level = *hp;
        hp += 1;
        header->zoom_configurations[i].max_zoom_level = *hp;
        hp += 1;
        header->zoom_configurations[i].absolute_start_position = readBigEndian64(hp);
        hp += 8;
        header->zoom_configurations[i].size_of_sub_file = readBigEndian64(hp);
        hp += 8;

        printf("Zoom configs base %i, min %i, max %i\n", header->zoom_configurations[i].base_zoom_level,
               header->zoom_configurations[i].min_zoom_level, header->zoom_configurations[i].max_zoom_level);
        // helper
        uint8_t base_zoom = header->zoom_configurations[i].base_zoom_level;
        uint32_t x1 = longMd2tilex(header->bounding_box.minLon, base_zoom);
        uint32_t x2 = longMd2tilex(header->bounding_box.maxLon, base_zoom);
        uint32_t y1 = latMd2tiley(header->bounding_box.minLat, base_zoom);
        uint32_t y2 = latMd2tiley(header->bounding_box.maxLat, base_zoom);
        header->zoom_configurations[i].tile_x_start = MIN(x1, x2);
        header->zoom_configurations[i].tile_x_end = MAX(x1, x2);
        header->zoom_configurations[i].tile_y_start = MIN(y1, y2);
        header->zoom_configurations[i].tile_y_end = MAX(y1, y2);
    }

    return 0;
}

int renderer_deinit_map(const mapsforge_header *header) {
    for (int i = 0; i < header->poi_tags_cnt; ++i) {
        vPortFree(header->poi_tags[i]);
    }
    vPortFree(header->poi_tags);

    for (int i = 0; i < header->way_tags_cnt; ++i) {
        vPortFree(header->way_tags[i]);
    }
    vPortFree(header->way_tags);
    vPortFree(header->zoom_configurations);

    return 0;
}


int renderer_get_zoom_configuration_id(const mapsforge_header *header, uint8_t zoom, uint8_t *zoom_idx) {
    uint8_t zoom_interval_configuration = UINT8_MAX;

    if (header == NULL)
        return 1;
    for (int i = 0; i < header->amount_of_zoom_intervals; ++i) {
        if (header->zoom_configurations[i].min_zoom_level <= zoom &&
            header->zoom_configurations[i].max_zoom_level >= zoom) {
            zoom_interval_configuration = i;
            break;
        }
    }
    if (zoom_interval_configuration == UINT8_MAX)
        return 1;

    *zoom_idx = zoom_interval_configuration;
    return 0;
}


int renderer_get_tile_offset(mapsforge_header *header, uint8_t zoom, uint32_t tile_x, uint32_t tile_y,
                             uint64_t *tile_offset, uint64_t *tile_end_offset) {
    FRESULT fr;
    unsigned int read;
    uint8_t zoom_interval_configuration = UINT8_MAX;

    if (header == NULL)
        return 1;

    if (renderer_get_zoom_configuration_id(header, zoom, &zoom_interval_configuration))
        return 1;

    uint32_t x_start = header->zoom_configurations[zoom_interval_configuration].tile_x_start;
    uint32_t x_end = header->zoom_configurations[zoom_interval_configuration].tile_x_end;
    uint32_t y_start = header->zoom_configurations[zoom_interval_configuration].tile_y_start;
    uint32_t y_end = header->zoom_configurations[zoom_interval_configuration].tile_y_end;

    if (tile_x < x_start || tile_x > x_end || tile_y < y_start || tile_y > y_end)
        return 1;

    uint32_t row_len = x_end - x_start + 1;
    uint32_t idx = (tile_y - y_start) * row_len + (tile_x - x_start);

    fr = f_lseek(&header->map_fp,
                 header->zoom_configurations[zoom_interval_configuration].absolute_start_position +
                 (header->flags.bits.debug ? 16 : 0) +
                 (5 * idx));
    if (fr != FR_OK) { return 1; }

    uint8_t buffer[10];
    fr = f_read(&header->map_fp, buffer, 10, &read);
    if (fr != FR_OK) { return 1; }
    if (read != 10) { return 1; }

    *tile_offset = 0;
    if (buffer[0] & 0x8) // water
        return 0;

    for (int i = 0; i < 5; ++i) {
        *tile_offset = (*tile_offset) << 8;
        *tile_offset += buffer[i];
    }
    *tile_offset += header->zoom_configurations[zoom_interval_configuration].absolute_start_position;

    *tile_end_offset = 0;
    // is last tile?
    if (tile_x == x_end && tile_y == y_end) {
        *tile_end_offset = header->zoom_configurations[zoom_interval_configuration].absolute_start_position +
                           header->zoom_configurations[zoom_interval_configuration].size_of_sub_file;
    } else {
        for (int i = 5; i < 10; ++i) {
            *tile_end_offset = (*tile_end_offset) << 8;
            *tile_end_offset += buffer[i];
        }
        *tile_end_offset += header->zoom_configurations[zoom_interval_configuration].absolute_start_position;
    }
    return 0;
}


int renderer_read_ways_to_mem(mapsforge_header *header, uint8_t zoom, uint32_t tile_x, uint32_t tile_y) {
    //helper
    uint8_t *ptr;
    uint64_t uint_parse = 0;
    int64_t int_parse = 0;
    uint32_t strlen;
    uint32_t buffer_offset;


    // file handling
    FRESULT fr;
    unsigned int read;
    uint8_t *buffer = (uint8_t *) FILE_BUFFER;
    uint64_t buffer_offset_from_file_start = 0;

    uint64_t tile_offset_in_file;
    uint64_t tile_offset_in_file_end;

    uint64_t first_way_offset_in_tile;
    uint64_t way_offset_in_file;

    // coord calculation
    int32_t tile_mlat_start = tiley2latMd(tile_y, zoom);
    int32_t tile_mlon_start = tilex2longMd(tile_x, zoom);

    uint8_t zoom_configuration_idx;
    uint8_t zoom_configs_cnt;

    // ways
    uint32_t way_data_size;
    uint32_t way_signature_data_size_size;

    uint8_t osm_layer;
    uint8_t highest_theme_z_layer;
    way_special_byte special_byte;
    way_flags flags;

    uint8_t *layer_way_list_mem_ptr = (uint8_t *) WAYS_BUFFER;
    way_list_entry_way **layer_way_list_ptr_to_next_ptr[256] = {0};
    way_list_entry_way *layer_way_list_curr_node;

    uint8_t *layer_way_name_list_mem_ptr = (uint8_t *) WAY_NAMES_BUFFER;
    way_list_entry_way_name **layer_way_name_list_ptr_to_next_ptr;
    way_list_entry_way_name *layer_way_name_list_curr_node;

    bool layer_way_list_save_current_way_to_render;
    bool layer_way_name_list_save_current_way_to_render;


    // preparation
    memset(layer_way_list_mem_ptr, 0, WAYS_BUFFER_SIZE);
    for (int i = 0; i < 256; ++i) {
        layer_way_list_ptr_to_next_ptr[i] = (way_list_entry_way **) layer_way_list_mem_ptr;
        layer_way_list_mem_ptr += sizeof(way_list_entry_way **);
    }
    layer_way_name_list_ptr_to_next_ptr = (way_list_entry_way_name **) layer_way_name_list_mem_ptr;
    layer_way_name_list_mem_ptr += sizeof(way_list_entry_way_name **);


    // get tile offset in file
    if (renderer_get_tile_offset(header, zoom, tile_x, tile_y, &tile_offset_in_file, &tile_offset_in_file_end))
        return 1;

    // get zoom config
    if (renderer_get_zoom_configuration_id(header, zoom, &zoom_configuration_idx))
        return 1;


    uint64_t tile_size_on_disk = tile_offset_in_file_end - tile_offset_in_file;
    //printf("Tile size on disk: %luKB\n", (tile_size_on_disk) / 1024);


    //read tile header
    fr = f_lseek(&header->map_fp, tile_offset_in_file);
    buffer_offset_from_file_start = tile_offset_in_file;
    if (fr != FR_OK) { return 1; }
    fr = f_read(&header->map_fp, buffer, MIN(FILE_BUFFER_SIZE, tile_size_on_disk), &read);
    if (fr != FR_OK) { return 1; }
    if (read != MIN(FILE_BUFFER_SIZE, tile_size_on_disk)) { return 1; }

    // parse tile header
    ptr = buffer;
    if (header->flags.bits.debug) {
        //printf("%.*s\n", 32, (char *) buffer);
        ptr += 32;
    }

    zoom_configs_cnt = header->zoom_configurations[zoom_configuration_idx].max_zoom_level -
                       header->zoom_configurations[zoom_configuration_idx].min_zoom_level + 1;
    // ignore zoom table?
    //uint32_t zoom_table[zoom_configs][2];
    for (int i = 0; i < zoom_configs_cnt; ++i) {
        ptr += parseVBEU(&uint_parse, ptr);
        //zoom_table[i][0] = uint_parse;
        ptr += parseVBEU(&uint_parse, ptr);
        //zoom_table[i][1] = uint_parse;
    }

    // ignore POI for now
    ptr += parseVBEU(&first_way_offset_in_tile, ptr);


    // tile offset + tile header which is (ptr - buffer) + first_way_offset_in_tile
    way_offset_in_file = tile_offset_in_file + (ptr - buffer) + first_way_offset_in_tile;


    while (way_offset_in_file < tile_offset_in_file_end) {
        layer_way_list_save_current_way_to_render = false;
        layer_way_name_list_save_current_way_to_render = false;

        /*
        // read everything, no buffer implementation
        fr = f_lseek(&header->map_fp, way_offset_in_file);
        if (fr != FR_OK) { return 1; }
        fr = f_read(&header->map_fp, buffer, 512, &read);
        if (fr != FR_OK) { return 1; }
        */

        buffer_offset = (way_offset_in_file - buffer_offset_from_file_start);

        // refresh buffer at 3/4 of usage
        if (FILE_BUFFER_SIZE / 4 > (FILE_BUFFER_SIZE - buffer_offset)) {
            fr = f_lseek(&header->map_fp, way_offset_in_file);
            if (fr != FR_OK) { return 1; }
            fr = f_read(&header->map_fp, buffer, FILE_BUFFER_SIZE, &read);
            if (fr != FR_OK) { return 1; }
            if (read != FILE_BUFFER_SIZE) { return 1; }

            buffer_offset_from_file_start += buffer_offset;
            buffer_offset = 0;
        }

        ptr = buffer + buffer_offset;

        if (header->flags.bits.debug) {
            //printf("%.*s\n", 32, (char *) ptr);
            // for debugging a specific way
            /*
            if (memcmp(ptr, "---WayStart24437215---", 21) == 0)
                printf(" ");*/
            ptr += 32;
        }

        ptr += parseVBEU(&uint_parse, ptr);
        way_data_size = uint_parse;
        way_signature_data_size_size = ptr - buffer - buffer_offset;

        // read full way data
        /*
        fr = f_lseek(&header->map_fp, way_offset_in_file + way_signature_data_size_size);
        if (fr != FR_OK) { return 1; }
        fr = f_read(&header->map_fp, buffer, way_data_size, &read);
        if (fr != FR_OK || read != way_data_size) { return 1; }
        */

        buffer_offset = ((way_offset_in_file + way_signature_data_size_size) - buffer_offset_from_file_start);
        ptr = buffer + buffer_offset;

        // ignore, i guess this is only a hint if the way needs to be rendered if rendering a sub tile?
        //uint16_t sub_tile_bitmap = readBigEndian16(ptr);
        ptr += 2;

        special_byte.value = *ptr;
        ptr += 1;
        osm_layer = special_byte.bits.osm_tag_layer;

        // decide if we will save the way to render it later and
        // save the tag with the highest z-layer and calculate z-layer
        highest_theme_z_layer = 0;
        uint16_t tag_id_highest_z = 0;

        for (int tag = 0; tag < special_byte.bits.amount_of_tags; ++tag) {
            uint64_t tag_id;
            ptr += parseVBEU(&tag_id, ptr);
            if (header->way_tag_theme_lookup[tag_id]) {
                //if (header->way_tag_theme_lookup[tag_id]->line_width[zoom]) {
                    // initial tag that is rendered
                    if (layer_way_list_save_current_way_to_render == false) {
                        layer_way_list_save_current_way_to_render = true;
                        tag_id_highest_z = tag_id;
                        highest_theme_z_layer = header->way_tag_theme_lookup[tag_id]->z_layer;
                    }
                    // higher z-layer?
                    if (header->way_tag_theme_lookup[tag_id]->z_layer > highest_theme_z_layer) {
                        tag_id_highest_z = tag_id;
                        highest_theme_z_layer = header->way_tag_theme_lookup[tag_id]->z_layer;
                    }
               // }
            }
        }


        flags.value = *ptr;
        ptr += 1;

        char *way_name;
        uint32_t way_name_length;

        int32_t label_pos_lat_diff = 0;
        int32_t label_pos_lon_diff = 0;

        uint32_t num_data_blocks = 1;

        if (flags.bits.way_name) {
            ptr += parseVBEUStringInplace(&way_name, ptr, &way_name_length);
            //printf("%.*s\n", strlen, way_name);
        }
        if (flags.bits.house_number) {
            char *house_number;
            ptr += parseVBEUStringInplace(&house_number, ptr, &strlen);
        }
        if (flags.bits.reference) {
            char *reference;
            ptr += parseVBEUStringInplace(&reference, ptr, &strlen);
        }
        if (flags.bits.label_pos) {
            ptr += parseVBES(&int_parse, ptr);
            label_pos_lat_diff = (int32_t) int_parse;
            ptr += parseVBES(&int_parse, ptr);
            label_pos_lon_diff = (int32_t) int_parse;
        }
        if (flags.bits.number_of_way_data_blocks) {
            ptr += parseVBEU(&uint_parse, ptr);
            num_data_blocks = uint_parse;
        }

        if (flags.bits.future_use) {
            printf("Future flags in way properties set, not handled\n");
        }

        if (flags.bits.way_name) {
            layer_way_name_list_save_current_way_to_render = true;
        }
        if (layer_way_name_list_save_current_way_to_render) {
            layer_way_name_list_curr_node = (way_list_entry_way_name *) layer_way_name_list_mem_ptr;
            // register current node (on this layer)
            *layer_way_name_list_ptr_to_next_ptr = layer_way_name_list_curr_node;
            // remember where to register next node (on this layer)
            layer_way_name_list_ptr_to_next_ptr = (way_list_entry_way_name **) &
                    layer_way_name_list_curr_node->next;
            // set data
            layer_way_name_list_curr_node->next = NULL;
            memcpy(layer_way_name_list_curr_node->name, way_name, way_name_length);
            layer_way_name_list_curr_node->name_len = way_name_length;
            layer_way_name_list_mem_ptr += sizeof(way_list_entry_way_name) + sizeof(char) * way_name_length;
        }


        if (layer_way_list_save_current_way_to_render) {
            layer_way_list_curr_node = (way_list_entry_way *) layer_way_list_mem_ptr;
            // register current node (on this layer)
            *layer_way_list_ptr_to_next_ptr[highest_theme_z_layer * 16 + osm_layer] = layer_way_list_curr_node;
            // remember where to register next node (on this layer)
            layer_way_list_ptr_to_next_ptr[highest_theme_z_layer * 16 + osm_layer] = (way_list_entry_way **) &
                    layer_way_list_curr_node->next;
            // set data
            layer_way_list_curr_node->next = NULL;
            layer_way_list_curr_node->tag_id = tag_id_highest_z;
        }

        uint32_t way_coord_idx = 0;
        // WAY DATA START
        for (int way_data_block = 0; way_data_block < num_data_blocks; ++way_data_block) {
            uint32_t way_coord_blocks;
            ptr += parseVBEU(&uint_parse, ptr);
            way_coord_blocks = uint_parse;
            //way_coord_blocks > 1 means that it is a multipolygon which is ignored for now

            for (int way_coord_block = 0; way_coord_block < way_coord_blocks; ++way_coord_block) {
                ptr += parseVBEU(&uint_parse, ptr);
                uint32_t way_nodes = uint_parse;

                ptr += parseVBES(&int_parse, ptr);
                int32_t mlat_curr = tile_mlat_start + (int32_t) int_parse;

                ptr += parseVBES(&int_parse, ptr);
                int32_t mlon_curr = tile_mlon_start + (int32_t) int_parse;


                if (layer_way_name_list_save_current_way_to_render) {
                    layer_way_name_list_curr_node->mlat = mlat_curr + label_pos_lat_diff;
                    layer_way_name_list_curr_node->mlon = mlon_curr + label_pos_lon_diff;
                }

                // only save way for non-multipolygon
                if (layer_way_list_save_current_way_to_render && way_coord_block == 0) {
                    layer_way_list_curr_node->coords[way_coord_idx * 2] = mlat_curr;
                    layer_way_list_curr_node->coords[way_coord_idx * 2 + 1] = mlon_curr;
                    way_coord_idx++;
                }


                if (flags.bits.delta_encoding_type == 0) {
                    // single delta encoding
                    for (int way_node = 0; way_node < (way_nodes >= 1 ? way_nodes - 1 : 0); ++way_node) {
                        ptr += parseVBES(&int_parse, ptr);
                        mlat_curr += (int32_t) int_parse;
                        ptr += parseVBES(&int_parse, ptr);
                        mlon_curr += (int32_t) int_parse;

                        if (layer_way_list_save_current_way_to_render && way_coord_block == 0) {
                            layer_way_list_curr_node->coords[way_coord_idx * 2] = mlat_curr;
                            layer_way_list_curr_node->coords[way_coord_idx * 2 + 1] = mlon_curr;
                            way_coord_idx++;
                        }
                    }
                } else {
                    //double delta
                    int32_t delta_mlat = 0;
                    int32_t delta_mlon = 0;

                    for (int way_node = 0; way_node < (way_nodes >= 1 ? way_nodes - 1 : 0); ++way_node) {
                        ptr += parseVBES(&int_parse, ptr);
                        delta_mlat += (int32_t) int_parse;
                        ptr += parseVBES(&int_parse, ptr);
                        delta_mlon += (int32_t) int_parse;

                        mlat_curr += delta_mlat;
                        mlon_curr += delta_mlon;

                        if (layer_way_list_save_current_way_to_render && way_coord_block == 0) {
                            layer_way_list_curr_node->coords[way_coord_idx * 2] = mlat_curr;
                            layer_way_list_curr_node->coords[way_coord_idx * 2 + 1] = mlon_curr;
                            way_coord_idx++;
                        }
                    }
                }
            }
        }

        if (layer_way_list_save_current_way_to_render) {
            layer_way_list_curr_node->coords_cnt += way_coord_idx;
            layer_way_list_mem_ptr += sizeof(way_list_entry_way) + way_coord_idx * sizeof(uint32_t) * 2;
        }

        if (layer_way_list_mem_ptr - ((uint8_t *) WAYS_BUFFER) > WAYS_BUFFER_SIZE - (WAYS_BUFFER_SIZE >> 5)) {
            printf("Stopping loading ways, buffer almost full %ldKB\n",
                   (layer_way_list_mem_ptr - ((uint8_t *) WAYS_BUFFER)) / 1024);
            return 1;
        }

        way_offset_in_file = way_offset_in_file + way_signature_data_size_size + way_data_size;
    }

    printf("Used way mem: %ld (%ld%%)\n", (layer_way_list_mem_ptr - ((uint8_t *) WAYS_BUFFER)),
           (layer_way_list_mem_ptr - ((uint8_t *) WAYS_BUFFER)) * 100 / WAYS_BUFFER_SIZE);
    printf("Used way names mem: %ld (%ld%%)\n", (layer_way_name_list_mem_ptr - ((uint8_t *) WAY_NAMES_BUFFER)),
           (layer_way_name_list_mem_ptr - ((uint8_t *) WAY_NAMES_BUFFER)) * 100 / WAYS_BUFFER_SIZE);

    return 0;
}

int renderer_render_tile_from_mem(mapsforge_header *header, uint8_t zoom, uint32_t tile_x, uint32_t tile_y,
                                  uint16_t *img_buff) {
    // border
    /*for (int i = 0; i < 256; ++i) {
        // top line
        img_buff[i] = 0x0000;
        // bottom line
        img_buff[256 * 255 + i] = 0x0000;
        //left line
        img_buff[i * 256] = 0x0000;
        //right line
        img_buff[i * 256 + 255] = 0x0000;
    }*/

    way_list_entry_way **way_starts = (way_list_entry_way **) ((uint8_t *) WAYS_BUFFER);

    const uint32_t tile_x_px = tile_x * 256;
    const uint32_t tile_y_px = tile_y * 256;

    int32_t mlat1 = tiley2latMd(tile_y, zoom);
    int32_t mlat2 = tiley2latMd(tile_y + 1, zoom);

    int32_t mlon1 = tilex2longMd(tile_x, zoom);
    int32_t mlon2 = tilex2longMd(tile_x + 1, zoom);

    int32_t mlat_min = MIN(mlat1, mlat2);
    int32_t mlat_max = MAX(mlat1, mlat2);
    int32_t mlon_min = MIN(mlon1, mlon2);
    int32_t mlon_max = MAX(mlon1, mlon2);

    bool polygon = false;
    int32_t mlat;
    int32_t mlon;

    __unused char txt[256];

    xSemaphoreTake(lvgl_lock, portMAX_DELAY);

    lv_obj_t *dummy_screen = lv_obj_create(NULL);
    lv_obj_t *canvas = lv_canvas_create(dummy_screen);
    lv_canvas_set_buffer(canvas, img_buff, 256, 256, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(canvas, 256, 256);

    lv_layer_t lv_layer;
    lv_canvas_init_layer(canvas, &lv_layer);

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);

    line_dsc.round_end = 1;
    line_dsc.round_start = 1;


    xSemaphoreGive(lvgl_lock);

    // streets and polygons
    for (int layer = 0; layer < 256; ++layer) {

        way_list_entry_way *node = way_starts[layer];
        while (node != NULL) {

            /*
            static int nodenumber = 0;
            nodenumber++;

            if (tile_x == 284655 && tile_y == 184280 && layer == 133 && nodenumber >= 61584) {
                printf("rendering node %i\n", nodenumber);
            }
            */


            lv_color_t color;
            uint8_t thickness = 0;
            bool allow_polygon = false;

            const theme *theme = header->way_tag_theme_lookup[node->tag_id];
            if (theme != NULL) {
                color = theme->color;
                thickness = theme->line_width[zoom];
                allow_polygon = theme->allow_polygon;
            }

            if (!thickness) {
                node = (way_list_entry_way *) node->next;
                continue;
            }

            mlat = node->coords[0];
            mlon = node->coords[1];
            uint32_t x0 = lonMdToX_px(mlon, zoom);
            uint32_t y0 = latMdToY_px(mlat, zoom);

            polygon = false;
            if (allow_polygon) {
                if (y0 == latMdToY_px(node->coords[(node->coords_cnt - 1) * 2], zoom) &&
                    x0 == lonMdToX_px(node->coords[(node->coords_cnt - 1) * 2 + 1], zoom)) {
                    polygon = true;
                }
            }

            bool render = false;
            bool lat_min_cond = false;
            bool lat_max_cond = false;
            bool long_min_cond = false;
            bool long_max_cond = false;

            xSemaphoreTake(lvgl_lock, portMAX_DELAY);

            if (polygon) {
                lv_fpoint_t pts[node->coords_cnt];
                lv_fpoint_t pt;


                for (int coord_idx = 0; coord_idx < node->coords_cnt; ++coord_idx) {
                    mlat = node->coords[coord_idx * 2 + 0];
                    mlon = node->coords[coord_idx * 2 + 1];

                    // lat is y, lon is x
                    if (mlat >= mlat_min) lat_min_cond = true; // there exists a point below the
                    if (mlat <= mlat_max) lat_max_cond = true;

                    if (mlon >= mlon_min) long_min_cond = true;
                    if (mlon <= mlon_max) long_max_cond = true;

                    // calculate px
                    uint32_t x1 = lonMdToX_px(mlon, zoom);
                    uint32_t y1 = latMdToY_px(mlat, zoom);

                    pt.x = (float) ((int) x1 - (int) tile_x_px);
                    pt.y = (float) ((int) y1 - (int) tile_y_px);

                    pts[coord_idx] = pt;
                }

                if (lat_min_cond && lat_max_cond && long_min_cond && long_max_cond) { render = true; }

                if (render) {
                    lv_draw_vector_dsc_t *draw_vec_dsc = lv_draw_vector_dsc_create(&lv_layer);
                    lv_vector_path_t *path = lv_vector_path_create(LV_VECTOR_PATH_QUALITY_MEDIUM);

                    lv_vector_path_move_to(path, &pts[0]);
                    for (int coord_idx = 1; coord_idx < node->coords_cnt; ++coord_idx) {
                        lv_vector_path_line_to(path, &pts[coord_idx]);
                    }

                    lv_vector_path_close(path);

                    lv_draw_vector_dsc_set_fill_color(draw_vec_dsc, color);
                    lv_draw_vector_dsc_add_path(draw_vec_dsc, path);

                    lv_draw_vector(draw_vec_dsc);

                    lv_draw_vector_dsc_delete(draw_vec_dsc);
                    lv_vector_path_delete(path);
                }
            } else {
                lv_point_precise_t pts[node->coords_cnt];
                lv_point_precise_t pt;

                for (int coord_idx = 0; coord_idx < node->coords_cnt; ++coord_idx) {
                    mlat = node->coords[coord_idx * 2 + 0];
                    mlon = node->coords[coord_idx * 2 + 1];

                    // lat is y, lon is x
                    if (mlat >= mlat_min) lat_min_cond = true; // there exists a point below the
                    if (mlat <= mlat_max) lat_max_cond = true;

                    if (mlon >= mlon_min) long_min_cond = true;
                    if (mlon <= mlon_max) long_max_cond = true;

                    // calculate px
                    uint32_t x1 = lonMdToX_px(mlon, zoom);
                    uint32_t y1 = latMdToY_px(mlat, zoom);

                    pt.x = (float) ((int) x1 - (int) tile_x_px);
                    pt.y = (float) ((int) y1 - (int) tile_y_px);

                    pts[coord_idx] = pt;
                }

                if (lat_min_cond && lat_max_cond && long_min_cond && long_max_cond) { render = true; }

                if (render) {
                    line_dsc.color = color;
                    line_dsc.width = thickness;
                    line_dsc.points = pts;
                    line_dsc.point_cnt = (int) node->coords_cnt;

                    lv_draw_line(&lv_layer, &line_dsc);
                }
            }

            lv_canvas_finish_layer(canvas, &lv_layer);
            // lv_canvas_init_layer(canvas, &lv_layer);

            xSemaphoreGive(lvgl_lock);

            node = (way_list_entry_way *) node->next;
        }

    }

    // labels
    /*
    way_list_entry_way_name *node = *((way_list_entry_way_name **) ((uint8_t*) WAY_NAMES_BUFFER));
    while (node != NULL) {
        int32_t x0 = (int32_t) (lonMdToX_px(node->mlon, zoom) - tile_x_px);
        int32_t y0 = (int32_t) (latMdToY_px(node->mlat, zoom) - tile_y_px);

        uint8_t len = node->name_len > 254 ? 254 : node->name_len;
        memcpy(txt, node->name, len);
        txt[len] = 0;

        xSemaphoreTake(lvgl_lock, portMAX_DELAY);

        lv_draw_label_dsc_t label_dsc;
        lv_draw_label_dsc_init(&label_dsc);
        label_dsc.color.blue = 0;
        label_dsc.color.red = 0;
        label_dsc.color.green = 0;
        label_dsc.font = &JetBrainsMonoRegular_12;
        //label_dsc.font = &lv_font_montserrat_12;
        label_dsc.text = txt;


        lv_area_t coords = {x0, y0, 2000, 2000};

        lv_draw_label(&lv_layer, &label_dsc, &coords);


        lv_canvas_finish_layer(canvas, &lv_layer);

        xSemaphoreGive(lvgl_lock);


        node = node->next;
    }
    */


    
    sprintf(txt, "%i/%i/%i", zoom, tile_x, tile_y);

    xSemaphoreTake(lvgl_lock, portMAX_DELAY);
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color.blue = 0;
    label_dsc.color.red = 0;
    label_dsc.color.green = 0;
    label_dsc.font = &lv_font_montserrat_12;
    label_dsc.text = txt;
    lv_area_t coords = {5, 5, 256, 256};
    lv_draw_label(&lv_layer, &label_dsc, &coords);
    lv_canvas_finish_layer(canvas, &lv_layer);
    xSemaphoreGive(lvgl_lock);
    

    xSemaphoreTake(lvgl_lock, portMAX_DELAY);

    lv_obj_delete(canvas);
    lv_obj_delete(dummy_screen);

    xSemaphoreGive(lvgl_lock);

    return 0;
}

int renderer_clear_tile(uint16_t *img_buff) {
    xSemaphoreTake(lvgl_lock, portMAX_DELAY); // needed?
    uint16_t *bg_ptr = img_buff;
    for (int i = 0; i < 256 * 256; ++i) {
        *bg_ptr++ = 0xdefb;
    }
    xSemaphoreGive(lvgl_lock);
    return 0;
}

int renderer_render_tile(mapsforge_header *header, uint8_t zoom, uint32_t tile_x, uint32_t tile_y,
                         uint16_t *img_buff) {
    int ret;
    uint8_t zoom_config_idx = UINT8_MAX;

    static uint32_t tile_x_cache = UINT32_MAX;
    static uint32_t tile_y_cache = UINT32_MAX;
    static uint8_t zoom_cache = UINT8_MAX;

    // search for tile which includes our tile info
    for (int i = 0; i < header->amount_of_zoom_intervals; ++i) {
        if (header->zoom_configurations[i].min_zoom_level <= zoom &&
            header->zoom_configurations[i].max_zoom_level >= zoom) {
            zoom_config_idx = i;
            break;
        }
    }
    if (zoom_config_idx == UINT8_MAX) { return 1; }


    printf("Rendering https://tile.openstreetmap.org/%i/%i/%i.png\n", zoom, tile_x, tile_y);

    int8_t zoom_diff = (int8_t) (zoom - header->zoom_configurations[zoom_config_idx].base_zoom_level);
    uint8_t base_zoom = header->zoom_configurations[zoom_config_idx].base_zoom_level;


    if (zoom_diff >= 0) {
        uint32_t new_tile_x = (tile_x) >> zoom_diff;
        uint32_t new_tile_y = (tile_y) >> zoom_diff;
        //printf("data from -> https://tile.openstreetmap.org/%i/%i/%i.png\n", base_zoom, new_tile_x, new_tile_y);

        if (!(base_zoom == zoom_cache && new_tile_x == tile_x_cache && new_tile_y == tile_y_cache)) {
            ret = renderer_read_ways_to_mem(header, base_zoom, new_tile_x, new_tile_y);
            // if (ret) return ret;
            tile_x_cache = new_tile_x;
            tile_y_cache = new_tile_y;
            zoom_cache = base_zoom;
        }

        renderer_render_tile_from_mem(header, zoom, tile_x, tile_y, img_buff);
    } else {
        uint8_t neg_zom_diff = -zoom_diff;
        for (int x = 0; x < (1 << neg_zom_diff); ++x) {
            for (int y = 0; y < (1 << neg_zom_diff); ++y) {
                ret = renderer_read_ways_to_mem(header, base_zoom, (tile_x << neg_zom_diff) + x,
                                                (tile_y << neg_zom_diff) + y);
                if (ret) continue; // skip rendering this tile
                renderer_render_tile_from_mem(header, zoom, tile_x, tile_y, img_buff);
            }
        }
    }


    return 0;
}
