/**
* Copyright (c) Manuel Jedinger
 *
 * SPDX-License-Identifier: GPL-2.0
 */


#include "display.h"

#include "portmacro.h"
#include "qspi.pio.h"

#include "Board.h"

#include <FreeRTOSConfig_examples_common.h>
#include <assert.h>
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/regs/intctrl.h>
#include <hardware/regs/pio.h>
#include <hardware/structs/clocks.h>
#include <pico/platform/common.h>
#include <pico/time.h>
#include <src/display/lv_display.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "Drivers/DISPLAY/qspi.pio.h"
#include "hardware/dma.h"
#include "hardware/i2c.h"
#include "hardware/sync.h"
#include "main.h"
#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "task.h"

typedef struct qspi_config {
  PIO qspi_pio;
  uint8_t qspi_sm;
  uint32_t qspi_sm_offset;
  uint8_t n_bits;
  uint display_dma_tx;
  dma_channel_config display_dma_tx_config;
  lv_display_t *lvgl_display;
} qspi_config_t;

qspi_config_t qspi_config = {
    .qspi_pio = DISPLAY_QSPI_PIO,
    .qspi_sm = 0,
    .qspi_sm_offset = 0,
    .n_bits = 0,
    .display_dma_tx = 0,
    .display_dma_tx_config = {0},
    .lvgl_display = 0,
};

void display_clear(void);

void QSPI_select(void) {
  gpio_put(DISPLAY_CS_PIN, 0);
};

void QSPI_deselect(void) {
  // need to wait for fifo empty for deselect
  while (!pio_sm_is_tx_fifo_empty(qspi_config.qspi_pio, qspi_config.qspi_sm))
    tight_loop_contents();

  // also need to wait until the latest bits are sent, use PIO_FDEBUG_TXSTALL_BITS?
  sleep_us(1);

  gpio_put(DISPLAY_CS_PIN, 1);
};

void QSPI_write_spi_byte(uint8_t byte);

void QSPI_write_spi_set_pixel(uint32_t addr);

void QSPI_write_spi_set_register(uint32_t addr);

void display_set_window(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);

void display_init(void) {
  // gpios
  gpio_init(DISPLAY_CS_PIN);
  gpio_pull_down(DISPLAY_CS_PIN);
  gpio_set_dir(DISPLAY_CS_PIN, true);
  gpio_put(DISPLAY_CS_PIN, 1);

  gpio_init(DISPLAY_RESET_PIN);
  gpio_set_dir(DISPLAY_RESET_PIN, true);

  // display is also connected to i2c?
  i2c_init(BOARD_I2C_PORT, 400 * 1000);
  gpio_set_function(BOARD_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(BOARD_SCL_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(BOARD_SDA_PIN);
  gpio_pull_up(BOARD_SCL_PIN);

  // qspi
  qspi_config.n_bits = 16;
  qspi_config.qspi_sm_offset =
      pio_add_program(qspi_config.qspi_pio, &qspi_program);

  pio_qspi_init(qspi_config.qspi_pio, qspi_config.qspi_sm,
                qspi_config.qspi_sm_offset, qspi_config.n_bits, DISPLAY_SCK_PIN,
                DISPLAY_SIO0_PIN);
  // dma
  qspi_config.display_dma_tx = dma_claim_unused_channel(true);
  qspi_config.display_dma_tx_config =
      dma_channel_get_default_config(qspi_config.display_dma_tx);

  channel_config_set_transfer_data_size(&qspi_config.display_dma_tx_config,
                                        DMA_SIZE_16);
  channel_config_set_read_increment(&qspi_config.display_dma_tx_config, true);
  channel_config_set_write_increment(&qspi_config.display_dma_tx_config, false);
  channel_config_set_dreq(
      &qspi_config.display_dma_tx_config,
      pio_get_dreq(qspi_config.qspi_pio, qspi_config.qspi_sm, true));

  irq_set_enabled(DMA_IRQ_0, false);

  gpio_put(DISPLAY_RESET_PIN, 1);
  sleep_ms(50);
  gpio_put(DISPLAY_RESET_PIN, 0);
  sleep_ms(50);
  gpio_put(DISPLAY_RESET_PIN, 1);
  sleep_ms(300);

  QSPI_select();
  QSPI_write_spi_set_register(0x11);
  sleep_ms(120);
  QSPI_deselect();

  // reg=0xc4, data={0x80}
  // set spi mode -> SPI write SRAM enable
  QSPI_select();
  QSPI_write_spi_set_register(0xc4);
  QSPI_write_spi_byte(0x80);
  QSPI_deselect();

  // reg=0x44, data={0x01, 0xD7}
  // set tearing effect scan line -> on line 0x1D7=472
  QSPI_select();
  QSPI_write_spi_set_register(0x44);
  QSPI_write_spi_byte(0x01);
  QSPI_write_spi_byte(0xD7);
  QSPI_deselect();

  // QSPI_select();
  // QSPI_write_spi_set_register(0x44);
  // QSPI_write_spi_byte(0x01);
  // QSPI_write_spi_byte(0xD1);
  // QSPI_deselect();

  // reg=0x35, data={0x00}
  // Tearing effect line on
  QSPI_select();
  QSPI_write_spi_set_register(0x35);
  QSPI_write_spi_byte(0x00);
  QSPI_deselect();

  // reg=0x53, data={0x20}
  // write CTRL Display -> Dimming on
  QSPI_select();
  QSPI_write_spi_set_register(0x53);
  QSPI_write_spi_byte(0x20);
  sleep_ms(10);
  QSPI_deselect();

  // reg=0x29, data_bytes=0
  // display on, data doesn't matter
  QSPI_select();
  QSPI_write_spi_set_register(0x29);
  sleep_ms(10);
  QSPI_deselect();

  // reg=0x51, data={0xA0}
  // set brightness to 0xA0 (~62%)
  QSPI_select();
  QSPI_write_spi_set_register(0x51);
  QSPI_write_spi_byte(0x10);
  QSPI_deselect();

  // reg=0x20, data_bytes=0
  // display inversion off
  QSPI_select();
  QSPI_write_spi_set_register(0x20);
  QSPI_deselect();

  // reg=0x36, data={0x00}
  // memory data access control (row address increases vertical, column address
  // increases horizontal, RGB (not BGR)
  QSPI_select();
  QSPI_write_spi_set_register(0x36);
  QSPI_write_spi_byte(0x00);
  QSPI_deselect();

  // reg=0x3A, data={0x05}
  // interface pixel format -> IFPF used -> IFPF is used -> 16bit per pixel
  QSPI_select();
  QSPI_write_spi_set_register(0x3A);
  QSPI_write_spi_byte(0x05);
  QSPI_deselect();

  // display_clear();

    // reg=0x51, data={0xA0}
  // set brightness to 0xA0 (~62%)
  QSPI_select();
  QSPI_write_spi_set_register(0x51);
  QSPI_write_spi_byte(0xFF);
  QSPI_deselect();


}

void dma_handler_real(void) {

  if (dma_channel_get_irq0_status(qspi_config.display_dma_tx)) {
    UBaseType_t inter = vTaskEnterCriticalFromISR();
    dma_channel_acknowledge_irq0(qspi_config.display_dma_tx);
    QSPI_deselect();
    lv_display_flush_ready(qspi_config.lvgl_display);
    vTaskExitCriticalFromISR(inter);
  }
}

void dma_handler(void) {

  if (!dma_channel_is_busy(qspi_config.display_dma_tx)) {
    QSPI_deselect();
    lv_display_flush_ready(qspi_config.lvgl_display);
  }
}

void display_flush_cb(lv_display_t *_display, const lv_area_t *area,
                      uint8_t *px_map) {

  // not supported
  // if (lv_display_get_render_mode(qspi_config.lvgl_display) ==
  //     LV_DISPLAY_RENDER_MODE_PARTIAL) {
  //   return;
  // }

  uint16_t x1 = area->x1;
  uint16_t x2 = area->x2;
  uint16_t y1 = area->y1;
  uint16_t y2 = area->y2;

  // this is a restriction according to datasheet (only even number of rows or columns are allowed)
  // if ((area->x1 - area->x2 + 1) % 2 || (area->y1 - area->y2 + 1) % 2) {
  //   lv_display_flush_ready(qspi_config.lvgl_display);
  //   return;
  // }
  if (lv_display_get_render_mode(qspi_config.lvgl_display) ==
      LV_DISPLAY_RENDER_MODE_DIRECT) {

    if ((x1 - x2 + 1) % 2) {
      if (x2 < DISPLAY_WIDTH - 1)
        x2 += 1;
      else if (x1 > 0)
        x1 -= 1;
      else
        assert(0); // error, not possible
    }
    if ((y1 - y2 + 1) % 2) {
      if (y2 < DISPLAY_HEIGHT - 1)
        y2 += 1;
      else if (y1 > 0)
        y1 -= 1;
      else
        assert(0); // error, not possible
    }

    display_set_window(x1, y1, x2, y2);

    QSPI_select();
    QSPI_write_spi_set_pixel(0x2c);

    for (int y = y1; y < y2 + 1; y++) {

      uint16_t* ptr = &((uint16_t *)px_map)[(DISPLAY_WIDTH * y) + x1];

      uint16_t encoded_tranfer_cnt = (x2 + 1 - x1);
      dma_channel_configure(qspi_config.display_dma_tx,
                            &qspi_config.display_dma_tx_config,
                            &qspi_config.qspi_pio->txf[qspi_config.qspi_sm],
                            ptr, encoded_tranfer_cnt, true);

      // Waiting for DMA transfer to complete
      while (dma_channel_is_busy(qspi_config.display_dma_tx))
        taskYIELD();
    }
    lv_display_flush_ready(qspi_config.lvgl_display);

    QSPI_deselect();

  } else {

    display_set_window(x1, y1, x2, y2);

    QSPI_select();
    QSPI_write_spi_set_pixel(0x2c);

    uint32_t transfer_amount_byte = ((x2 + 1 - x1) * (y2 + 1 - y1)) * 2;
    dma_channel_configure(qspi_config.display_dma_tx,
                          &qspi_config.display_dma_tx_config,
                          &qspi_config.qspi_pio->txf[qspi_config.qspi_sm],
                          px_map,                      // read address
                          transfer_amount_byte / 2, // transfer words
                          true);                    // Start DMA transfer

    // assumed transfer time: 4bit per clk/2 -> 1byte clk/4

    uint32_t f_cpu = clock_get_hz(clk_sys);
    uint32_t cycle_cnt_transfer = transfer_amount_byte * 4;
    uint32_t time_ms =
        (cycle_cnt_transfer * 1000) / f_cpu; // this will always be zero?

    vTaskDelay(pdMS_TO_TICKS(time_ms + 1));

    while (dma_channel_is_busy(qspi_config.display_dma_tx))
        taskYIELD();

    lv_display_flush_ready(qspi_config.lvgl_display);

    QSPI_deselect();
  }
}

void display_prepeare_lvgl(lv_display_t *display) {
  // lv display config
  assert(qspi_config.n_bits == 16);
  qspi_config.lvgl_display = display;
  lv_display_set_flush_cb(display, display_flush_cb);

  // dma
  // interrupt doesnt seem to work with freertos
  // dma_channel_set_irq0_enabled(qspi_config.display_dma_tx, true);
  // irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
  // irq_set_priority(DMA_IRQ_0, 0);
  // irq_add_shared_handler(DMA_IRQ_0, dma_handler, 0);
}

void display_set_window(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
  // https://github.com/espressif/esp-iot-solution/blob/bd85f52467353dae3bd5f551054795287b0042a4/components/display/lcd/esp_lcd_co5300/esp_lcd_co5300_spi.c#L272

  x0 = x0 + 6;
  x1 = x1 + 6;

  QSPI_select();
  QSPI_write_spi_set_register(0x2a);
  QSPI_write_spi_byte(x0 >> 8);
  QSPI_write_spi_byte(x0 & 0xff);
  QSPI_write_spi_byte((x1) >> 8);
  QSPI_write_spi_byte((x1) & 0xff);
  QSPI_deselect();

  QSPI_select();
  QSPI_write_spi_set_register(0x2b);
  QSPI_write_spi_byte(y0 >> 8);
  QSPI_write_spi_byte(y0 & 0xff);
  QSPI_write_spi_byte((y1) >> 8);
  QSPI_write_spi_byte((y1) & 0xff);
  QSPI_deselect();

  // really needed if done again in display_cb?
  // QSPI_select();
  // QSPI_write_spi_set_register(0x2c);
  // QSPI_deselect();
}

void display_clear(void) {
  uint16_t image[DISPLAY_WIDTH] = {0};

  for (int i = 0; i < DISPLAY_WIDTH; i++) {
    image[i] = 0xF81F;
  }

  uint8_t *partial_image = (uint8_t *)(image);

  display_set_window(0, 0, DISPLAY_HEIGHT - 1, DISPLAY_WIDTH - 1);
  QSPI_select();
  QSPI_write_spi_set_pixel(0x2c);

  for (int i = 0; i < DISPLAY_HEIGHT; i++) {
    dma_channel_configure(
        qspi_config.display_dma_tx, &qspi_config.display_dma_tx_config,
        &qspi_config.qspi_pio
             ->txf[qspi_config.qspi_sm], // Destination pointer (PIO TX FIFO)
        partial_image,                   // Source pointer (data buffer)
        DISPLAY_WIDTH, // Data length (unit: number of transmissions)
        true);         // Start transferring immediately

    // Waiting for DMA transfer to complete
    while (dma_channel_is_busy(qspi_config.display_dma_tx))
      ;
  }

  QSPI_deselect();
}

void QSPI_write_spi_set_pixel(uint32_t addr) {
  // 1 WIRE CMD
  QSPI_write_spi_byte(0x32);

  // 1 WIRE ADDR
  QSPI_write_spi_byte(0x00);
  QSPI_write_spi_byte(addr);
  QSPI_write_spi_byte(0x00);
}

void QSPI_write_spi_set_register(uint32_t addr) {
  // 1 WIRE CMD
  QSPI_write_spi_byte(0x02);

  // 1 WIRE ADDR
  QSPI_write_spi_byte(0x00);
  QSPI_write_spi_byte(addr);
  QSPI_write_spi_byte(0x00);
}

void QSPI_write_spi_byte(uint8_t byte) {
  uint32_t data = 0;
  for (uint8_t i = 0; i < 8; i++) {
    data |= (byte & (1 << i) ? 1 : 0) << (i * 4);
  }

  if (qspi_config.n_bits == 8) {
    pio_sm_put_blocking(qspi_config.qspi_pio, qspi_config.qspi_sm,
                        (data << 0) & 0xFF000000);
    pio_sm_put_blocking(qspi_config.qspi_pio, qspi_config.qspi_sm,
                        (data << 8) & 0xFF000000);
    pio_sm_put_blocking(qspi_config.qspi_pio, qspi_config.qspi_sm,
                        (data << 16) & 0xFF000000);
    pio_sm_put_blocking(qspi_config.qspi_pio, qspi_config.qspi_sm,
                        (data << 24) & 0xFF000000);
  } else if (qspi_config.n_bits == 16) {
    pio_sm_put_blocking(qspi_config.qspi_pio, qspi_config.qspi_sm,
                        (data << 0) & 0xFFFF0000);
    pio_sm_put_blocking(qspi_config.qspi_pio, qspi_config.qspi_sm,
                        (data << 16) & 0xFFFF0000);
  }
}
