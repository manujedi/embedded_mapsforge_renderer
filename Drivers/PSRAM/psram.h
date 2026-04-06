#ifndef INCLUDE_PSRAM_H
#define INCLUDE_PSRAM_H

#include "stddef.h"
#include "stdint.h"

//#define PSRAM_LOCATION _u(0x15000000)
#define PSRAM_LOCATION 0x11000000U
//#define PSRAM_SIZE      0x800000U
#define PSRAM_SIZE        0x800000U

size_t psram_init(uint32_t cs_pin);

#endif
