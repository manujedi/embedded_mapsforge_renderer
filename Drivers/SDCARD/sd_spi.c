#include "sd_spi.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <hardware/dma.h>
#include <pico/platform/panic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

// SD command tokens and constants
#define SD_TOKEN_START_BLOCK 0xFE

// Card state
static struct {
  spi_inst_t *spi;
  uint cs, sck, mosi, miso;
  uint32_t init_baud;
  uint32_t high_baud;
  bool sdhc; // true if SDHC/SDXC (block addressing)
  bool initialized;
} sd;

static struct {
  uint dma_tx;
  dma_channel_config dma_tx_config;
  uint dma_rx;
  dma_channel_config dma_rx_config;
} sd_dma;

static inline void cs_high(void) { gpio_put(sd.cs, 1); }
static inline void cs_low(void) { gpio_put(sd.cs, 0); }

static uint8_t xfer(uint8_t v) {
  uint8_t rx;
  spi_write_read_blocking(sd.spi, &v, &rx, 1);
  return rx;
}

static void clocks(uint32_t count) {
  while (count--)
    xfer(0xFF);
}

static uint8_t sd_send_cmd(uint8_t cmd, uint32_t arg, uint8_t crc) {
  xfer(0xFF);
  xfer(0x40 | cmd);
  xfer((arg >> 24) & 0xFF);
  xfer((arg >> 16) & 0xFF);
  xfer((arg >> 8) & 0xFF);
  xfer(arg & 0xFF);
  xfer(crc);

  for (int i = 0; i < 8; i++) {
    uint8_t r = xfer(0xFF);
    if ((r & 0x80) == 0)
      return r;
  }
  return 0xFF;
}

static bool wait_token(uint8_t token, uint32_t timeout_ms) {
  absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
  uint8_t b;
  do {
    b = xfer(0xFF);
    if (b == token)
      return true;
  } while (!time_reached(deadline));
  return false;
}

static bool wait_not_busy(uint32_t timeout_ms) {
  absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
  do {
    if (xfer(0xFF) == 0xFF)
      return true;
  } while (!time_reached(deadline));
  return false;
}

bool sd_spi_init(const sd_spi_config_t *cfg) {
  sd.spi = cfg->spi;
  sd.cs = cfg->cs_pin;
  sd.sck = cfg->sck_pin;
  sd.mosi = cfg->mosi_pin;
  sd.miso = cfg->miso_pin;
  sd.init_baud = cfg->init_baud_hz ? cfg->init_baud_hz : 400000;
  sd.high_baud = cfg->high_baud_hz ? cfg->high_baud_hz : 24000000;
  sd.sdhc = false;
  sd.initialized = false;

  gpio_init(sd.cs);
  gpio_set_dir(sd.cs, GPIO_OUT);
  gpio_put(sd.cs, 1);

  gpio_set_function(sd.sck, GPIO_FUNC_SPI);
  gpio_set_function(sd.mosi, GPIO_FUNC_SPI);
  gpio_set_function(sd.miso, GPIO_FUNC_SPI);

  spi_init(sd.spi, sd.init_baud);
  spi_set_format(sd.spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

  sd_dma.dma_rx = dma_claim_unused_channel(true);
  sd_dma.dma_tx = dma_claim_unused_channel(true);

  sd_dma.dma_tx_config = dma_channel_get_default_config(sd_dma.dma_tx);
  channel_config_set_transfer_data_size(&sd_dma.dma_tx_config, DMA_SIZE_8);
  channel_config_set_write_increment(&sd_dma.dma_tx_config, false);
  channel_config_set_dreq(&sd_dma.dma_tx_config, spi_get_dreq(sd.spi, true));
  // dma_channel_configure(sd_dma.dma_tx, &sd_dma.dma_tx_config,
  //                       &spi_get_hw(sd.spi)->dr, // write address
  //                       txbuf, // read address
  //                       TEST_SIZE, // element count (each element is of size
  //                       transfer_data_size) false); // don't start yet


  // We set the inbound DMA to transfer from the SPI receive FIFO to a memory
  // buffer paced by the SPI RX FIFO DREQ We configure the read address to
  // remain unchanged for each element, but the write address to increment (so
  // data is written throughout the buffer)
  sd_dma.dma_rx_config = dma_channel_get_default_config(sd_dma.dma_rx);
  channel_config_set_transfer_data_size(&sd_dma.dma_rx_config, DMA_SIZE_8);
  channel_config_set_read_increment(&sd_dma.dma_rx_config, false);
  channel_config_set_dreq(&sd_dma.dma_rx_config, spi_get_dreq(sd.spi, false));
  // dma_channel_configure(sd_dma.dma_rx, &sd_dma.dma_rx_config,
  //                       rxbuf, // write address
  //                       &spi_get_hw(sd.spi)->dr, // read address
  //                       TEST_SIZE, // element count (each element is of size
  //                       transfer_data_size) false); // don't start yet

  cs_high();
  clocks(10); // 80 clocks with CS high

  cs_low();

  uint8_t r1 = sd_send_cmd(0, 0, 0x95); // CMD0 GO_IDLE_STATE
  if (r1 != 0x01) {
    cs_high();
    return false;
  }

  r1 = sd_send_cmd(8, 0x000001AA, 0x87); // CMD8 SEND_IF_COND
  uint8_t r7[4] = {0};
  for (int i = 0; i < 4; i++)
    r7[i] = xfer(0xFF);
  if (r1 & 0x04) {
    cs_high();
    return false;
  } // illegal command -> likely v1, not supported here
  if (r7[2] != 0x01 || r7[3] != 0xAA) {
    cs_high();
    return false;
  }

  absolute_time_t deadline = make_timeout_time_ms(1000);
  do {
    sd_send_cmd(55, 0, 0xFF);               // CMD55 APP_CMD
    r1 = sd_send_cmd(41, 0x40000000, 0xFF); // ACMD41 HCS
    if (r1 == 0x00)
      break;
    sleep_ms(10);
  } while (!time_reached(deadline));

  if (r1 != 0x00) {
    cs_high();
    return false;
  }

  r1 = sd_send_cmd(58, 0, 0xFF); // CMD58 READ_OCR
  if (r1 != 0x00) {
    cs_high();
    return false;
  }
  uint8_t ocr[4];
  for (int i = 0; i < 4; i++)
    ocr[i] = xfer(0xFF);
  sd.sdhc = (ocr[0] & 0x40) != 0; // CCS bit

  cs_high();
  xfer(0xFF);

  spi_set_baudrate(sd.spi, sd.high_baud);
  sd.initialized = true;
  return true;
}

bool sd_is_sdhc(void) { return sd.sdhc; }

bool sd_read_block(uint32_t address, uint8_t *buf) {
  if (!sd.initialized)
    return false;
  uint32_t addr = sd.sdhc ? address : (address * 512u);
  cs_low();
  uint8_t r1 = sd_send_cmd(17, addr, 0xFF); // CMD17 READ_SINGLE_BLOCK
  if (r1 != 0x00) {
    cs_high();
    return false;
  }
  if (!wait_token(SD_TOKEN_START_BLOCK, 200)) {
    cs_high();
    return false;
  }

  // for (int i = 0; i < 512; i++) buf[i] = xfer(0xFF);
  uint8_t tmp = 0xff;
  channel_config_set_read_increment(&sd_dma.dma_tx_config, false); // change this to not need a 512 byte buffer
  dma_channel_configure(
      sd_dma.dma_tx, &sd_dma.dma_tx_config,
      &spi_get_hw(sd.spi)->dr, // write address
      &tmp,                    // read address
      512,    // element count (each element is of size transfer_data_size)
      false); // don't start yet

  channel_config_set_write_increment(&sd_dma.dma_rx_config, true);
  dma_channel_configure(
      sd_dma.dma_rx, &sd_dma.dma_rx_config,
      buf,                     // write address
      &spi_get_hw(sd.spi)->dr, // read address
      512,    // element count (each element is of size transfer_data_size)
      false); // don't start yet

  // start simultaneously
  dma_start_channel_mask((1u << sd_dma.dma_tx) | (1u << sd_dma.dma_rx));
  dma_channel_wait_for_finish_blocking(sd_dma.dma_rx);

  xfer(0xFF); // CRC
  xfer(0xFF);
  cs_high();
  xfer(0xFF);
  return true;
}

bool sd_read_blocks(uint32_t address, uint8_t *buf, uint16_t cnt) {
  if (!sd.initialized)
    return false;

  while (cnt) {
    bool ret = sd_read_block(address, buf);
    if (ret != true)
      return ret;
    address++;
    cnt--;
    buf += 512;
  }

  return true;
}

bool sd_write_block(uint32_t address, const uint8_t *buf) {
  if (!sd.initialized)
    return false;
  uint32_t addr = sd.sdhc ? address : (address * 512u);
  cs_low();
  uint8_t r1 = sd_send_cmd(24, addr, 0xFF); // CMD24 WRITE_SINGLE_BLOCK
  if (r1 != 0x00) {
    cs_high();
    return false;
  }

  xfer(SD_TOKEN_START_BLOCK);
  // for (int i = 0; i < 512; i++)
  //   xfer(buf[i]);


  uint8_t tmp = 0;
  channel_config_set_read_increment(&sd_dma.dma_tx_config, true);
  dma_channel_configure(
      sd_dma.dma_tx, &sd_dma.dma_tx_config,
      &spi_get_hw(sd.spi)->dr, // write address
      buf,                    // read address
      512,    // element count (each element is of size transfer_data_size)
      false); // don't start yet

  channel_config_set_write_increment(&sd_dma.dma_rx_config, false);
  dma_channel_configure(
      sd_dma.dma_rx, &sd_dma.dma_rx_config,
      &tmp,                     // write address
      &spi_get_hw(sd.spi)->dr, // read address
      512,    // element count (each element is of size transfer_data_size)
      false); // don't start yet

  // start simultaneously
  dma_start_channel_mask((1u << sd_dma.dma_tx) | (1u << sd_dma.dma_rx));
  dma_channel_wait_for_finish_blocking(sd_dma.dma_tx);
  dma_channel_wait_for_finish_blocking(sd_dma.dma_rx); //drain rx fifo


  xfer(0xFF); // dummy CRC
  xfer(0xFF);

  uint8_t resp = xfer(0xFF) & 0x1F;
  if (resp != 0x05) {
    cs_high();
    return false;
  }

  if (!wait_not_busy(500)) {
    cs_high();
    return false;
  }
  cs_high();
  xfer(0xFF);
  return true;
}

bool sd_write_blocks(uint32_t address, const uint8_t *buf, uint16_t cnt) {
  if (!sd.initialized)
    return false;

  while (cnt) {
    bool ret = sd_write_block(address, buf);
    if (ret != true)
      return ret;
    address++;
    cnt--;
    buf += 512;
  }

  return true;
}

bool sd_is_initialized() { return sd.initialized; }