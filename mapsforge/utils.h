#pragma once
#include <stdint.h>


uint8_t parseVBEU(uint64_t *value, uint8_t *data);

uint8_t parseVBES(int64_t *value, uint8_t *data);

uint8_t parseVBEUString(char **dst, const uint8_t *data);

uint8_t parseVBEUStringInplace(char **dst, uint8_t *data, uint32_t *len);

uint16_t readBigEndian16(uint8_t *data);

int32_t readBigEndian32S(uint8_t *data);

uint32_t readBigEndian32U(uint8_t *data);

uint64_t readBigEndian64(uint8_t *data);

uint32_t lonMdToX_px(int32_t lon, uint8_t zoom);

uint32_t latMdToY_px(int32_t lat, uint8_t zoom);

uint32_t longMd2tilex(int32_t lon, int z);

uint32_t latMd2tiley(int32_t lat, int z);

int32_t tilex2longMd(uint32_t x, uint32_t z);

int32_t tiley2latMd(uint32_t y, uint32_t z);
