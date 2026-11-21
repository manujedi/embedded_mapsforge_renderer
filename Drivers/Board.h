#ifndef CYCLINGPC_BOARD_H
#define CYCLINGPC_BOARD_H

#include "hardware/pio.h"

#define DISPLAY_CS_PIN 10
#define DISPLAY_SCK_PIN 11
#define DISPLAY_SIO0_PIN 12
#define DISPLAY_SI1_PIN 13
#define DISPLAY_SI2_PIN 14
#define DISPLAY_SI3_PIN 15
#define DISPLAY_RESET_PIN 16
#define DISPLAY_TE_PIN 17
#define DISPLAY_QSPI_PIO pio0

#define BOARD_I2C_PORT i2c1
#define BOARD_SDA_PIN 6
#define BOARD_SCL_PIN 7

#define TOUCH_RST_PIN 23
#define TOUCH_INT_PIN 22

#define GPS_RST_PIN 24
#define GPS_RX_PIN 25
#define GPS_TX_PIN 26

#endif