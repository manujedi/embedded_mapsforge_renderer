#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "hardware/spi.h"

typedef struct {
    spi_inst_t *spi;
    uint cs_pin;
    uint sck_pin;
    uint mosi_pin;
    uint miso_pin;
    uint32_t init_baud_hz;   // e.g. 400 kHz (<= 400 kHz for init)
    uint32_t high_baud_hz;   // e.g. 12 MHz (after init)
} sd_spi_config_t;

// Initialize SPI pins and the SD card over SPI. Returns true on success.
bool sd_spi_init(const sd_spi_config_t *cfg);

// Read a 512-byte block (LBA) into buf. Returns true on success.
bool sd_read_block(uint32_t lba, uint8_t *buf);
bool sd_read_blocks(uint32_t address, uint8_t *buf, uint16_t cnt);

// Write a 512-byte block (LBA) from buf. Returns true on success.
bool sd_write_block(uint32_t lba, const uint8_t *buf);
bool sd_write_blocks(uint32_t address, const uint8_t *buf, uint16_t cnt);

// Returns true if the card is SDHC/SDXC (block addressing), false for SDSC (byte addressing).
bool sd_is_sdhc(void);

bool sd_is_initialized(void);
