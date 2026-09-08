#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

typedef enum {
    VALVE_LED_HINT_OFF = 0,
} valve_led_hint_t;

typedef struct {
    int wire_count;
    int battery_adc_raw;
    int battery_adc_at_0v;
    int battery_adc_at_15v;
    char battery_profile[24];
} valve_battery_snapshot_t;

esp_err_t valve_engine_init(void);
/* Start the scan task after Wi-Fi/SoftAP so SPI work cannot block the console. */
esp_err_t valve_engine_start(void);

#define VALVE_STATE_JSON_MAX 8192
#define VALVE_CONFIG_JSON_MAX 32768

void valve_engine_get_state_json(char *out, size_t out_size);
void valve_engine_get_config_json(char *out, size_t out_size);
void valve_engine_get_default_config_json(char *out, size_t out_size);
int valve_engine_get_heartbeat_interval_ms(void);

esp_err_t valve_engine_handle_command_json(const char *json, char *response, size_t response_size);
esp_err_t valve_engine_handle_puzzle_command_json(const char *json, char *response, size_t response_size);
bool valve_engine_owns_puzzle(void);
bool valve_engine_pop_puzzle_pub(char *topic, size_t topic_size, char *payload, size_t payload_size);
esp_err_t valve_engine_apply_config_json(const char *json, bool persist, char *response, size_t response_size);
esp_err_t valve_engine_restore_defaults(bool persist, char *response, size_t response_size);

bool valve_engine_pop_event_json(char *out, size_t out_size);
bool valve_engine_pop_warning_message(char *out, size_t out_size);

void valve_engine_notify_wifi_connected(int rssi);
void valve_engine_notify_wifi_disconnected(void);
void valve_engine_set_wifi_ssid(const char *ssid);

valve_led_hint_t valve_engine_get_led_hint(void);
void valve_engine_get_battery_snapshot(valve_battery_snapshot_t *out);
