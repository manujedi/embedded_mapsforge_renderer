#ifndef CYCLINGPC_DISPLAY_H
#define CYCLINGPC_DISPLAY_H

#include "hardware/gpio.h"
#include <src/misc/lv_types.h>

#define DISPLAY_CS_PIN 10
#define DISPLAY_SCK_PIN 11
#define DISPLAY_SIO0_PIN 12
#define DISPLAY_SI1_PIN 13
#define DISPLAY_SI2_PIN 14
#define DISPLAY_SI3_PIN 15
#define DISPLAY_RESET_PIN 16
#define DISPLAY_TE_PIN 17

#define BOARD_I2C_PORT i2c1
#define BOARD_SDA_PIN 6
#define BOARD_SCL_PIN 7

void display_init(void);
void display_prepeare_lvgl(lv_display_t *display);

void dma_handler(void);

#endif //CYCLINGPC_DISPLAY_H
