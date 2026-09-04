#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t mcp23s17_init(void);
void mcp23s17_reset_pulse(void);

/* Logical pins 0–47 across three MCP23S17 chips at SPI addresses 0–2. */
esp_err_t mcp23s17_set_input_pullup(int logical);
esp_err_t mcp23s17_set_output(int logical, int level);
int mcp23s17_read(int logical); /* 0=LOW, 1=HIGH, -1=error */
int mcp23s17_get_mode(int logical); /* 0=output, 1=input */
int mcp23s17_get_olat(int logical);
