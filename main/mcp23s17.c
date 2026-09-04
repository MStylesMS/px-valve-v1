#include "mcp23s17.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mcp23s17";

#define PIN_MISO 12
#define PIN_MOSI 13
#define PIN_SCK  14
#define PIN_CS   15
#define PIN_RST  27

#define MCP_IODIRA 0x00
#define MCP_IODIRB 0x01
#define MCP_GPPUA  0x0C
#define MCP_GPPUB  0x0D
#define MCP_GPIOA  0x12
#define MCP_GPIOB  0x13
#define MCP_OLATA  0x14
#define MCP_OLATB  0x15
#define MCP_IOCON  0x0A

#define MCP_IOCON_HAEN  (1 << 3)
#define MCP_IOCON_SEQOP (1 << 5)

#define CHIP_COUNT 3
#define LOGICAL_MAX 48

static spi_device_handle_t s_spi;
static uint8_t s_iodir[CHIP_COUNT][2];
static uint8_t s_gppu[CHIP_COUNT][2];
static uint8_t s_olat[CHIP_COUNT][2];

static uint8_t opcode_write(int addr)
{
    return (uint8_t)(0x40 | ((addr & 0x07) << 1));
}

static uint8_t opcode_read(int addr)
{
    return (uint8_t)(opcode_write(addr) | 0x01);
}

static esp_err_t mcp_write(int addr, uint8_t reg, uint8_t value)
{
    spi_transaction_t t = {0};
    uint8_t tx[3] = { opcode_write(addr), reg, value };
    t.length = 24;
    t.tx_buffer = tx;
    return spi_device_polling_transmit(s_spi, &t);
}

static esp_err_t mcp_read(int addr, uint8_t reg, uint8_t *value)
{
    spi_transaction_t t = {0};
    uint8_t tx[3] = { opcode_read(addr), reg, 0 };
    uint8_t rx[3] = {0};
    t.length = 24;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    esp_err_t err = spi_device_polling_transmit(s_spi, &t);
    if (err == ESP_OK && value) {
        *value = rx[2];
    }
    return err;
}

static bool split_pin(int logical, int *chip, int *port, int *bit)
{
    if (logical < 0 || logical >= LOGICAL_MAX) {
        return false;
    }
    *chip = logical / 16;
    int local = logical % 16;
    *port = (local < 8) ? 0 : 1;
    *bit = local & 7;
    return true;
}

void mcp23s17_reset_pulse(void)
{
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(2));
}

esp_err_t mcp23s17_init(void)
{
    gpio_config_t rst = {
        .pin_bit_mask = 1ULL << PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst);
    gpio_set_level(PIN_RST, 1);
    mcp23s17_reset_pulse();

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    spi_device_interface_config_t dev = {
        .clock_speed_hz = 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 1,
        .flags = 0,
    };
    err = spi_bus_add_device(SPI2_HOST, &dev, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(err));
        return err;
    }

    /* HAEN=0 after reset: write IOCON via address 0 so every chip hears it. */
    uint8_t iocon = (uint8_t)(MCP_IOCON_HAEN | MCP_IOCON_SEQOP);
    (void)mcp_write(0, MCP_IOCON, iocon);
    for (int addr = 0; addr < CHIP_COUNT; addr++) {
        (void)mcp_write(addr, MCP_IOCON, iocon);
        s_iodir[addr][0] = 0xFF;
        s_iodir[addr][1] = 0xFF;
        s_gppu[addr][0] = 0xFF;
        s_gppu[addr][1] = 0xFF;
        s_olat[addr][0] = 0x00;
        s_olat[addr][1] = 0x00;
        (void)mcp_write(addr, MCP_IODIRA, 0xFF);
        (void)mcp_write(addr, MCP_IODIRB, 0xFF);
        (void)mcp_write(addr, MCP_GPPUA, 0xFF);
        (void)mcp_write(addr, MCP_GPPUB, 0xFF);
        (void)mcp_write(addr, MCP_OLATA, 0x00);
        (void)mcp_write(addr, MCP_OLATB, 0x00);
    }

    ESP_LOGI(TAG, "MCP23S17 cluster ready (HSPI 12/13/14/15 RST 27)");
    return ESP_OK;
}

esp_err_t mcp23s17_set_input_pullup(int logical)
{
    int chip, port, bit;
    if (!split_pin(logical, &chip, &port, &bit)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_iodir[chip][port] |= (uint8_t)(1u << bit);
    s_gppu[chip][port] |= (uint8_t)(1u << bit);
    uint8_t iodir_reg = (port == 0) ? MCP_IODIRA : MCP_IODIRB;
    uint8_t gppu_reg = (port == 0) ? MCP_GPPUA : MCP_GPPUB;
    esp_err_t err = mcp_write(chip, iodir_reg, s_iodir[chip][port]);
    if (err != ESP_OK) {
        return err;
    }
    return mcp_write(chip, gppu_reg, s_gppu[chip][port]);
}

esp_err_t mcp23s17_set_output(int logical, int level)
{
    int chip, port, bit;
    if (!split_pin(logical, &chip, &port, &bit)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_iodir[chip][port] &= (uint8_t)~(1u << bit);
    if (level) {
        s_olat[chip][port] |= (uint8_t)(1u << bit);
    } else {
        s_olat[chip][port] &= (uint8_t)~(1u << bit);
    }
    uint8_t iodir_reg = (port == 0) ? MCP_IODIRA : MCP_IODIRB;
    uint8_t olat_reg = (port == 0) ? MCP_OLATA : MCP_OLATB;
    esp_err_t err = mcp_write(chip, iodir_reg, s_iodir[chip][port]);
    if (err != ESP_OK) {
        return err;
    }
    return mcp_write(chip, olat_reg, s_olat[chip][port]);
}

int mcp23s17_read(int logical)
{
    int chip, port, bit;
    uint8_t value = 0;
    if (!split_pin(logical, &chip, &port, &bit)) {
        return -1;
    }
    uint8_t gpio_reg = (port == 0) ? MCP_GPIOA : MCP_GPIOB;
    if (mcp_read(chip, gpio_reg, &value) != ESP_OK) {
        return -1;
    }
    return (value >> bit) & 1;
}

int mcp23s17_get_mode(int logical)
{
    int chip, port, bit;
    if (!split_pin(logical, &chip, &port, &bit)) {
        return 1;
    }
    return (s_iodir[chip][port] >> bit) & 1;
}

int mcp23s17_get_olat(int logical)
{
    int chip, port, bit;
    if (!split_pin(logical, &chip, &port, &bit)) {
        return 0;
    }
    return (s_olat[chip][port] >> bit) & 1;
}
