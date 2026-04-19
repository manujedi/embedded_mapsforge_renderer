#include "LC76G.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Board.h"
#include "FreeRTOS.h"
#include "portmacro.h"
#include "pico/async_context_freertos.h"
#include "semphr.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

extern SemaphoreHandle_t i2c_lock;

GPS_information_t gps_information;


#define DEVICE_ADDRESS     0x50
#define DEVICE_ADDRESS_R   0x54

void LC76G_init(void) {
    xSemaphoreTake(i2c_lock, portMAX_DELAY);
    i2c_init(BOARD_I2C_PORT, 100 * 1000);
    gpio_set_function(BOARD_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(BOARD_SCL_PIN, GPIO_FUNC_I2C);
    xSemaphoreGive(i2c_lock);
    gpio_pull_up(BOARD_SDA_PIN);
    gpio_pull_up(BOARD_SCL_PIN);

    gpio_init(GPS_RST_PIN);
    gpio_set_dir(GPS_RST_PIN, GPIO_OUT);
    gpio_put(GPS_RST_PIN, 0);
    sleep_ms(10);
    gpio_put(GPS_RST_PIN, 1);
    sleep_ms(50);

    // mark data is invalid
    memset(&gps_information, 0, sizeof(GPS_information_t));
    gps_information.timestamp_tick = UINT32_MAX;

}

// -------------------- I2C WRITE --------------------
bool i2c_write(uint8_t addr, const uint8_t *data, size_t len) {
    xSemaphoreTake(i2c_lock, portMAX_DELAY);
    int ret = i2c_write_blocking(BOARD_I2C_PORT, addr, data, len, false);
    xSemaphoreGive(i2c_lock);
    return (ret >= 0);
}

// -------------------- I2C READ --------------------
bool i2c_read(uint8_t addr, uint8_t *data, size_t len) {
    xSemaphoreTake(i2c_lock, portMAX_DELAY);
    int ret = i2c_read_blocking(BOARD_I2C_PORT, addr, data, len, false);
    xSemaphoreGive(i2c_lock);
    return (ret >= 0);
}

uint32_t find_next_comma(uint8_t *readData, uint32_t maxLength) {
    uint32_t i = 0;
    do {
        if (readData[i] == ',')
            return i;

        i++;
    } while (i < maxLength);
    return 0;
}

int32_t parseLatitude(uint8_t *readData, uint32_t maxLength) {
    float millidegrees = 0.0f;
    float minutes = 0.0f;
    uint32_t tmp = 0;

    int32_t comma_pos = 0;
    while (comma_pos < maxLength) {
        comma_pos++;
        if (readData[comma_pos] == '.')
            break;
    }

    tmp = (readData[comma_pos - 4] - '0') * 10;
    tmp += readData[comma_pos - 3] - '0';
    millidegrees = (float) tmp * 1000000;
    //minutes
    minutes = (float) (readData[comma_pos - 2] - '0') * 10;
    minutes += (float) readData[comma_pos - 1] - '0';
    // minutes digits
    float factor = 0.1f;
    for (uint32_t i = comma_pos + 1; i < maxLength; i++) {
        minutes += (float) (readData[i] - '0') * factor;
        factor *= 0.1f;
    }
    millidegrees += (minutes/60) * 1000000;
    return (int32_t) millidegrees;
}

int32_t parseLongitude(uint8_t *readData, uint32_t maxLength) {
    float millidegrees = 0.0f;
    float minutes = 0.0f;
    uint32_t tmp = 0;

    int32_t comma_pos = 0;
    while (comma_pos < maxLength) {
        comma_pos++;
        if (readData[comma_pos] == '.')
            break;
    }

    tmp = (readData[comma_pos - 5] - '0') * 100;
    tmp += (readData[comma_pos - 4] - '0') * 10;
    tmp += readData[comma_pos - 3] - '0';
    millidegrees = (float) tmp * 1000000;
    //minutes
    minutes = (float) (readData[comma_pos - 2] - '0') * 10;
    minutes += (float) readData[comma_pos - 1] - '0';
    // minutes digits
    float factor = 0.1f;
    for (uint32_t i = comma_pos + 1; i < maxLength; i++) {
        minutes += (float) (readData[i] - '0') * factor;
        factor *= 0.1f;
    }
    millidegrees += (minutes/60) * 1000000;
    return (int32_t) millidegrees;
}

void LC76G_parse_GNGGA(uint8_t *readData) {
    // $GNGGA,080533.000,4703.485652,N,01527.370212,E,1,10,1.16,371.908,M,43.760,M,,*76

    uint32_t lineLenght = 0;
    do {
        if (readData[lineLenght] == '\r' || readData[lineLenght] == '\n') {
            break;
        }
        lineLenght += 1;
    } while (1);

    // nah, skip it
    if (lineLenght < 70) {
        return;
    }

    //skip "$GNGGA,"
    readData += find_next_comma(readData, lineLenght) + 1;

    // time
    gps_information.time.hours = (readData[0] - '0') * 10;
    gps_information.time.hours += (readData[1] - '0');
    gps_information.time.minutes = (readData[2] - '0') * 10;
    gps_information.time.minutes += (readData[3] - '0');
    gps_information.time.seconds = (readData[4] - '0') * 10;
    gps_information.time.seconds += (readData[5] - '0');

    readData += find_next_comma(readData, lineLenght) + 1;

    // latitude
    uint32_t len = find_next_comma(readData, lineLenght);
    gps_information.latm = parseLatitude(readData, len);
    readData += len + 1;

    gps_information.latm += readData[0] == 'N' ? 1 : -1;
    readData += find_next_comma(readData, lineLenght) + 1;

    // longitude
    len = find_next_comma(readData, lineLenght);
    gps_information.longm = parseLongitude(readData, len);
    readData += len + 1;

    gps_information.longm += readData[0] == 'S' ? 1 : -1;
    readData += find_next_comma(readData, lineLenght) + 1;

    // $GNGGA,080533.000,4703.485652,N,01527.370212,E,1,10,1.16,371.908,M,43.760,M,,*76
    // 1,10,1.16,371.908,M,43.760,M,,*76

    bool fix = readData[0] == '0' ? false : true;
    if (fix) {
        gps_information.timestamp_tick = xTaskGetTickCount();
    }

    readData += 2;

    len = find_next_comma(readData, lineLenght);
    gps_information.satellites = 0;

    for (uint32_t i = 0; i < len; i++) {
        gps_information.satellites *= 10;
        gps_information.satellites += readData[i] - '0';
    }

    readData += len + 1;

    // HDOP, skip
    readData += find_next_comma(readData, lineLenght) + 1;

    len = find_next_comma(readData, lineLenght);
    float altitude = strtof((char*)readData, NULL);

    gps_information.altitude = (int32_t) altitude;






}


void LC76G_read_NMEA(void) {
    // init write
    uint8_t data[] = {0x08, 0x00, 0x51, 0xAA, 0x04, 0x00, 0x00, 0x00};
    uint8_t readData[4] = {0};

    if (!i2c_write(DEVICE_ADDRESS, data, sizeof(data))) {
        printf("Write failed\n");
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    if (!i2c_read(DEVICE_ADDRESS_R, readData, sizeof(readData))) {
        printf("Read failed\n");
        return;
    }

    uint32_t dataLength =
            (readData[0]) |
            (readData[1] << 8) |
            (readData[2] << 16) |
            (readData[3] << 24);

    if (dataLength == 0) return;

    uint8_t data2[] = {0x00, 0x20, 0x51, 0xAA};
    uint8_t totalData[sizeof(data2) + sizeof(readData)];

    memcpy(totalData, data2, sizeof(data2));
    memcpy(totalData + sizeof(data2), readData, sizeof(readData));

    vTaskDelay(pdMS_TO_TICKS(100));

    if (!i2c_write(DEVICE_ADDRESS, totalData, sizeof(totalData))) {
        printf("Write2 failed\n");
        return;
    }

    uint8_t *dynamicReadData = (uint8_t *) malloc(dataLength);
    if (!dynamicReadData) {
        printf("Malloc failed\n");
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    if (!i2c_read(DEVICE_ADDRESS_R, dynamicReadData, dataLength)) {
        printf("Dynamic read failed\n");
        free(dynamicReadData);
        return;
    }

    // OUTPUT NMEA
    /*printf("%.*s", dataLength, (char *) dynamicReadData);
    printf("\n");*/

    // parse NMEA
    for (uint32_t i = 0; i < dataLength; i++) {
        if (memcmp(dynamicReadData + i, "$GNGGA", 6) == 0) {
            LC76G_parse_GNGGA(dynamicReadData);
        }

        // next line
        while (dynamicReadData[i] != '\n' && i < dataLength) {
            i++;
        }
    }


    free(dynamicReadData);
}
