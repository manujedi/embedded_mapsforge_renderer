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

#include "hardware/uart.h"

extern SemaphoreHandle_t i2c_lock;

GPS_information_t gps_information;


void LC76G_init(void) {

    gpio_init(GPS_RST_PIN);
    gpio_set_dir(GPS_RST_PIN, GPIO_OUT);
    gpio_put(GPS_RST_PIN, 0);
    sleep_ms(100);
    // internally pulled up in GPS module
    gpio_init(GPS_RST_PIN);
    sleep_ms(1000);

    // mark data is invalid
    memset(&gps_information, 0, sizeof(GPS_information_t));
    gps_information.timestamp_tick = UINT32_MAX;

    uart_init(uart1, 9600);

    // Correct mapping
    gpio_set_function(GPS_TX_PIN, UART_FUNCSEL_NUM(uart1, GPS_TX_PIN));
    gpio_set_function(GPS_RX_PIN, UART_FUNCSEL_NUM(uart1, GPS_RX_PIN));
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
    millidegrees += (minutes / 60) * 1000000;
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
    millidegrees += (minutes / 60) * 1000000;
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
    float altitude = strtof((char *) readData, NULL);

    gps_information.altitude = (int32_t) altitude;
}


void LC76G_read_NMEA(void) {

    uint8_t buff[1];
    while (1) {
        uart_read_blocking(uart1, buff, 1);
        printf("%c\n", *buff);
    }

}
