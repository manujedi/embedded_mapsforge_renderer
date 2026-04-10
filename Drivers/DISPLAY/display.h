#ifndef CYCLINGPC_DISPLAY_H
#define CYCLINGPC_DISPLAY_H

#include "hardware/gpio.h"
#include <src/misc/lv_types.h>

void display_init(void);
void display_prepeare_lvgl(lv_display_t *display);

void dma_handler(void);

#endif //CYCLINGPC_DISPLAY_H
