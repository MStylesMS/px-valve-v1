#include "mcp23s17.h"

#include <stdio.h>
#include <string.h>

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

/* 24 bits at 1 MHz is ~24 us; 80 ms bounds a wedged controller. */
#define MCP_SPI_WAIT pdMS_TO_TICKS(80)

static spi_device_handle_t s_spi;
static uint8_t s_iodir[CHIP_COUNT][2];
static uint8_t s_gppu[CHIP_COUNT][2];
static uint8_t s_olat[CHIP_COUNT][2];
static uint8_t s_present;
static bool s_ready;
static bool s_bus_ok;
static char s_fault[128];
static char s_spi_error[48];

static uint8_t opcode_write(int addr)
{
    return (uint8_t)(0x40 | ((addr & 0x07) << 1));
}

static uint8_t opcode_read(int addr)
{
    return (uint8_t)(opcode_write(addr) | 0x01);
}

static void set_fault(const char *msg)
{
    strncpy(s_fault, msg ? msg : "", sizeof(s_fault) - 1);
    s_fault[sizeof(s_fault) - 1] = '\0';
}

static void refresh_fault(void)
{
    if (s_spi_error[0]) {
        snprintf(s_fault, sizeof(s_fault),
                 "Hardware: %s — MCP23S17 GPIO expanders unavailable.",
                 s_spi_error);
        return;
    }
    if (s_ready) {
        s_fault[0] = '\0';
        return;
    }
    char missing[24] = {0};
    int n = 0;
    int missing_count = 0;
    for (int addr = 0; addr < CHIP_COUNT; addr++) {
        if (s_present & (1u << addr)) {
            continue;
        }
        missing_count++;
        n += snprintf(missing + n, sizeof(missing) - (size_t)n, "%s%d",
                      n ? ", " : "", addr);
    }
    if (s_present == 0) {
        set_fault("Hardware: MCP23S17 GPIO expanders not found (chips 0–2 missing). Valve panel I/O disabled.");
    } else {
        snprintf(s_fault, sizeof(s_fault),
                 "Hardware: MCP23S17 chip%s %s missing. Valve panel I/O disabled.",
                 missing_count == 1 ? "" : "s",
                 missing);
    }
}

static esp_err_t mcp_xfer(const uint8_t *tx, uint8_t *rx)
{
    spi_transaction_t t = {0};
    spi_transaction_t *done = NULL;
    if (!s_spi) {
        return ESP_ERR_INVALID_STATE;
    }
    t.length = 24;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    /* IDF 6 rejects polling_start with a finite timeout ("timeout is not
     * available for polling transactions"), which made every probe fail
     * before touching the bus. Interrupt transactions accept a timeout. */
    esp_err_t err = spi_device_queue_trans(s_spi, &t, MCP_SPI_WAIT);
    if (err != ESP_OK) {
        return err;
    }
    err = spi_device_get_trans_result(s_spi, &done, MCP_SPI_WAIT);
    if (err != ESP_OK) {
        return err;
    }
    return (done == &t) ? ESP_OK : ESP_FAIL;
}

static esp_err_t mcp_write(int addr, uint8_t reg, uint8_t value)
{
    uint8_t tx[3] = { opcode_write(addr), reg, value };
    return mcp_xfer(tx, NULL);
}

static esp_err_t mcp_read(int addr, uint8_t reg, uint8_t *value)
{
    uint8_t tx[3] = { opcode_read(addr), reg, 0 };
    uint8_t rx[3] = {0};
    esp_err_t err = mcp_xfer(tx, rx);
    if (err == ESP_OK && value) {
        *value = rx[2];
    }
    return err;
}

static bool probe_chip(int addr)
{
    uint8_t a = 0xFF;
    uint8_t b = 0xFF;
    /* IODIRA is 0x00 in both BANK maps. OLATA readback rejected live chips. */
    if (mcp_write(addr, MCP_IODIRA, 0xA5) != ESP_OK) {
        return false;
    }
    if (mcp_read(addr, MCP_IODIRA, &a) != ESP_OK || a != 0xA5) {
        return false;
    }
    if (mcp_write(addr, MCP_IODIRA, 0x5A) != ESP_OK) {
        return false;
    }
    if (mcp_read(addr, MCP_IODIRA, &b) != ESP_OK || b != 0x5A) {
        return false;
    }
    return true;
}

static void apply_chip_defaults(void)
{
    uint8_t iocon = (uint8_t)(MCP_IOCON_HAEN | MCP_IOCON_SEQOP);
    (void)mcp_write(0, MCP_IOCON, iocon);
    for (int addr = 0; addr < CHIP_COUNT; addr++) {
        if ((s_present & (1u << addr)) == 0) {
            continue;
        }
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

bool mcp23s17_probe(void)
{
    uint8_t found = 0;
    if (!s_spi || !s_bus_ok) {
        s_present = 0;
        s_ready = false;
        refresh_fault();
        return false;
    }

    uint8_t iocon = (uint8_t)(MCP_IOCON_HAEN | MCP_IOCON_SEQOP);
    (void)mcp_write(0, MCP_IOCON, iocon);
    for (int addr = 0; addr < CHIP_COUNT; addr++) {
        (void)mcp_write(addr, MCP_IOCON, iocon);
        if (probe_chip(addr)) {
            found |= (uint8_t)(1u << addr);
        }
    }
    s_present = found;
    s_ready = (found == 0x07);
    if (s_ready) {
        apply_chip_defaults();
        ESP_LOGI(TAG, "MCP23S17 chips 0–2 present");
    } else {
        ESP_LOGW(TAG, "MCP23S17 probe mask=0x%02x (need 0x07)", found);
    }
    refresh_fault();
    return s_ready;
}

bool mcp23s17_ready(void)
{
    return s_ready;
}

uint8_t mcp23s17_present_mask(void)
{
    return s_present;
}

void mcp23s17_get_fault(char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }
    if (s_ready) {
        out[0] = '\0';
        return;
    }
    if (s_fault[0] == '\0') {
        refresh_fault();
    }
    strncpy(out, s_fault, out_size - 1);
    out[out_size - 1] = '\0';
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
    /* DMA + floating MISO hung polling_transmit on a bare DevKit. */
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        snprintf(s_spi_error, sizeof(s_spi_error), "SPI bus init failed (%s)",
                 esp_err_to_name(err));
        s_bus_ok = false;
        s_present = 0;
        s_ready = false;
        refresh_fault();
        return ESP_OK;
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
        snprintf(s_spi_error, sizeof(s_spi_error), "SPI device add failed (%s)",
                 esp_err_to_name(err));
        s_spi = NULL;
        s_bus_ok = false;
        s_present = 0;
        s_ready = false;
        refresh_fault();
        return ESP_OK;
    }

    s_bus_ok = true;
    (void)mcp23s17_probe();
    if (s_ready) {
        ESP_LOGI(TAG, "MCP23S17 cluster ready (HSPI 12/13/14/15 RST 27)");
    } else {
        ESP_LOGW(TAG, "MCP23S17 cluster missing — Wi-Fi/UI still run (%s)", s_fault);
    }
    return ESP_OK;
}

esp_err_t mcp23s17_set_input_pullup(int logical)
{
    int chip, port, bit;
    if (!s_ready || !split_pin(logical, &chip, &port, &bit)) {
        return s_ready ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_STATE;
    }
    if ((s_present & (1u << chip)) == 0) {
        return ESP_ERR_INVALID_STATE;
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
    if (!s_ready || !split_pin(logical, &chip, &port, &bit)) {
        return s_ready ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_STATE;
    }
    if ((s_present & (1u << chip)) == 0) {
        return ESP_ERR_INVALID_STATE;
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
    if (!s_ready || !split_pin(logical, &chip, &port, &bit)) {
        return -1;
    }
    if ((s_present & (1u << chip)) == 0) {
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
