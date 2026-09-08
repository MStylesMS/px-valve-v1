#include "px_system.h"
#include "valve_engine.h"
#include "web_ui.h"

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

static const char *TAG = "px-valve-v1";

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();

    ESP_LOGI(TAG, "Starting px-valve-v1");
    ESP_LOGI(TAG, "Build info: id=%s date=%s time=%s", app->version, app->date, app->time);
    ESP_LOGI(TAG, "Target: Valve32Prop 192.168.8.50 STA Paradox-TFD-1");

    ESP_ERROR_CHECK(px_system_init());
    {
        esp_err_t err = valve_engine_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "valve engine init failed (%s) — continuing so Wi-Fi/UI still come up",
                     esp_err_to_name(err));
        }
    }
    /* SoftAP/STA before any MCP I/O. scan_task is priority 4 and used to
     * preempt main (priority 1) and hang in SPI, so the AP never started. */
    ESP_ERROR_CHECK(web_ui_start());
    {
        esp_err_t err = valve_engine_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "valve scan task failed (%s)", esp_err_to_name(err));
        }
    }

    /* Confirm OTA image so a later crash does not roll back to a bad slot. */
    esp_ota_mark_app_valid_cancel_rollback();
}
