#ifndef CYCLINGPC_LC76G_H
#define CYCLINGPC_LC76G_H

#include "stdint.h"

typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
} GPS_time_t;

typedef struct {
    int32_t latm;
    int32_t longm;
    int32_t altitude;
    uint8_t latest_timestamp;
    uint16_t satellites;
    uint32_t timestamp_tick;
    GPS_time_t time;
} GPS_information_t;

void LC76G_init(void);

void LC76G_read_NMEA(void);
#endif //CYCLINGPC_LC76G_H
