#include "FreeRTOS.h"
#include "task.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

uint8_t parseVBEU(uint64_t *value, const uint8_t *data) {
    *value = 0;
    uint8_t idx = 0;
    uint8_t shift = 0;
    uint8_t byte;
    while (1) {
        byte = data[idx++];
        *value |= ((uint64_t) (byte & 0x7F)) << shift;
        if (!(byte & 0x80))
            break;
        shift += 7;
    }
    return idx;
}

uint8_t parseVBES(int64_t *value, const uint8_t *data) {
    *value = 0;
    uint8_t idx = 0;
    while (data[idx] & 0x80) {
        *value += (data[idx] & 0x7F) << (7 * idx);
        idx += 1;
    }
    *value += (data[idx] & 0x3F) << (7 * idx);
    *value *= data[idx] & 0x40 ? -1 : 1;
    idx += 1;
    return idx;
}

// this parses and allocates the string, caller takes ownership
uint8_t parseVBEUString(char **dst, const uint8_t *data) {
    uint64_t str_len = 0;
    uint64_t vbeu_len = 0;
    vbeu_len += parseVBEU(&str_len, data);
    data += vbeu_len;
    *dst = pvPortCalloc(1, str_len + 1);
    memcpy(*dst, data, str_len);
    (*dst)[str_len] = 0;
    return str_len + vbeu_len;
}

// reports start pos in buffer and len
uint8_t parseVBEUStringInplace(char **dst, uint8_t *data, uint32_t *len) {
    uint64_t str_len = 0;
    uint64_t vbeu_len = 0;
    vbeu_len += parseVBEU(&str_len, data);
    data += vbeu_len;
    *dst = (char *) data;
    *len = str_len;
    return str_len + vbeu_len;
}

uint16_t readBigEndian16(const uint8_t *data) {
    uint16_t result = data[0];
    for (int i = 1; i < 2; ++i)
        result = (result << 8) | data[i];
    return result;
}

int32_t readBigEndian32S(const uint8_t *data) {
    int32_t result = data[0];
    for (int i = 1; i < 4; ++i)
        result = (result << 8) | data[i];
    return result;
}

uint32_t readBigEndian32U(const uint8_t *data) {
    uint32_t result = data[0];
    for (int i = 1; i < 4; ++i)
        result = (result << 8) | data[i];
    return result;
}

uint64_t readBigEndian64(const uint8_t *data) {
    uint64_t result = data[0];
    for (int i = 1; i < 8; ++i)
        result = (result << 8) | data[i];
    return result;
}

float md2d(int32_t microdegrees) {
    return ((float) microdegrees) / 1000000.0f;
}

int d2md(float degrees) {
    return (int) (degrees * 1000000);
}

// Returns longitude in pixels at a certain zoom level
uint32_t lonMdToX_px(int32_t lon, uint8_t zoom) {
    uint64_t offset = 256 << (zoom - 1);
    uint64_t nominator = offset * 180000000 + offset * lon;
    return nominator / 180000000;
}

// Returns latitude in pixels at a certain zoom level
uint32_t latToY_px(float lat, uint8_t zoom) {
    uint32_t offset = 256 << (zoom - 1);
    return (uint32_t) roundf(
        (float) offset - (float) offset / (float) M_PI *
        logf((1 + sinf(lat * (float) M_PI / 180)) / (1 - sinf(lat * (float) M_PI / 180))) / 2);
}

uint32_t latMdToY_px(int32_t lat, uint8_t zoom) {
    return latToY_px(md2d(lat), zoom);
}


uint32_t longMd2tilex(int32_t lon, int z) {
    uint64_t numerator = ((uint64_t) lon + 180000000) * (1 << z);
    uint64_t denominator = 360000000;
    uint32_t ret = (uint32_t) (numerator / denominator);
    return ret;
}

uint32_t lat2tiley(float lat, int z) {
    float latrad = lat * (float) M_PI / 180.0f;
    uint32_t ret = (uint32_t) (floor((1.0 - asinhf(tanf(latrad)) / (float) M_PI) / 2.0 * (1 << z)));
    return ret;
}


uint32_t latMd2tiley(int32_t lat, int z) {
    // is there a non floating point implementation?
    return lat2tiley(md2d(lat), z);
}


int32_t tilex2longMd(uint32_t x, uint32_t z) {
    uint32_t n = 1 << z;
    uint64_t nominator = (uint64_t) x * 360000000 - (uint64_t) n * 180000000;
    return (int32_t) (nominator / n);
}

float tiley2lat(uint32_t y, uint32_t z) {
    float n = (float) M_PI - 2.0f * (float) M_PI * y / (float) (1 << z);
    return 180.0f / (float) M_PI * atanf(0.5f * (expf(n) - expf(-n)));
}

int32_t tiley2latMd(uint32_t y, uint32_t z) {
    // todo implement without conversion
    return d2md(tiley2lat(y, z));
}
